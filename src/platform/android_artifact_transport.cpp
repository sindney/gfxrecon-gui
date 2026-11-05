// Unprivileged Android artifact-stream encoder. No replay APK changes are needed.
#include <lz4frame.h>
#define XXH_NAMESPACE LZ4_
#include <xxhash.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#ifdef __ANDROID__
#include <unistd.h>
#include <poll.h>
#elif defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif

namespace {
constexpr uint64_t kMaxBytes = 64ull * 1024 * 1024 * 1024;
struct File { FILE* value; ~File(){if(value)std::fclose(value);} };
struct Encoder { LZ4F_compressionContext_t value=nullptr; ~Encoder(){LZ4F_freeCompressionContext(value);} };
struct Hash { XXH64_state_t* value=XXH64_createState(); ~Hash(){XXH64_freeState(value);} };
bool Connected(){
#ifdef __ANDROID__
    pollfd fd{STDOUT_FILENO,POLLOUT,0};
    return poll(&fd,1,0)>=0&&!(fd.revents&(POLLERR|POLLHUP|POLLNVAL));
#else
    return true;
#endif
}
int Encode(){
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
#ifdef __ANDROID__
    char name[]="/data/local/tmp/gfxrecon-artifacts-XXXXXX";
    const int fd=mkstemp(name);if(fd<0)return 2;
    unlink(name); // Also releases the spool on cancellation/process termination.
    File spool{fdopen(fd,"w+b")};if(!spool.value){close(fd);return 2;}
#else
    File spool{std::tmpfile()};
#endif
    Encoder encoder;Hash hash;
    if(!spool.value||!hash.value||XXH64_reset(hash.value,0)||LZ4F_isError(LZ4F_createCompressionContext(&encoder.value,LZ4F_VERSION)))return 2;
    LZ4F_preferences_t prefs{};
    prefs.frameInfo.blockSizeID=LZ4F_max64KB;
    prefs.frameInfo.blockChecksumFlag=LZ4F_blockChecksumEnabled;
    prefs.frameInfo.contentChecksumFlag=LZ4F_contentChecksumEnabled;
    prefs.autoFlush=1;
    std::array<char,65536> input;
    std::array<char,131072> output;
    uint64_t raw=0,packed=0;
    auto save=[&](size_t size){
        if(LZ4F_isError(size)||size>output.size()||std::fwrite(output.data(),1,size,spool.value)!=size)return false;
        packed+=size;return true;
    };
    if(!save(LZ4F_compressBegin(encoder.value,output.data(),output.size(),&prefs)))return 3;
    for(size_t size;(size=std::fread(input.data(),1,input.size(),stdin));){
        if(!Connected()||size>kMaxBytes-raw)return 4;
        raw+=size;XXH64_update(hash.value,input.data(),size);
        if(!save(LZ4F_compressUpdate(encoder.value,output.data(),output.size(),input.data(),size,nullptr)))return 3;
    }
    if(std::ferror(stdin)||!Connected()||!save(LZ4F_compressEnd(encoder.value,output.data(),output.size(),nullptr))||std::fflush(spool.value))return 4;
    // The content hash precedes every compressed byte. Length and frame-end
    // checks make missing/truncated/trailing payloads invalid as well.
    if(std::printf("GFXRECON_TRANSFER_1%c%s%c%llu%c%llu%c%016llx%c",0,"lz4",0,
        static_cast<unsigned long long>(raw),0,static_cast<unsigned long long>(packed),0,
        static_cast<unsigned long long>(XXH64_digest(hash.value)),0)<0)return 5;
    std::rewind(spool.value);
    for(size_t size;(size=std::fread(output.data(),1,output.size(),spool.value));)
        if(std::fwrite(output.data(),1,size,stdout)!=size)return 5;
    return std::ferror(spool.value)||std::fflush(stdout)?5:0;
}
}
int main(int argc,char** argv){
    if(argc==2&&std::strcmp(argv[1],"--version")==0){std::puts("gfxrecon-artifact-transport protocol 1; LZ4; XXH64");return 0;}
    if(argc!=1)return 1;
    const int status=Encode();
    if(status)std::fprintf(stderr,"Artifact compression failed (%d)\n",status);
    return status;
}
