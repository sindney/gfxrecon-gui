#include "core/artifacts.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <cerrno>

namespace gfx::tests {
namespace {
namespace format { using HandleId = uint64_t; }
namespace util::platform {
constexpr auto FileSeekEnd=SEEK_END, FileSeekSet=SEEK_SET, FileSeekCurrent=SEEK_CUR;
void FileClose(FILE* file){fclose(file);}
#ifdef _WIN32
int FileOpen(FILE** file,const char* name,const char* mode){return fopen_s(file,name,mode);}
bool FileSeek(FILE* file,int64_t offset,int origin){return _fseeki64(file,offset,origin)==0;}
int64_t FileTell(FILE* file){return _ftelli64(file);}
#else
int FileOpen(FILE** file,const char* name,const char* mode){*file=fopen(name,mode);return *file?0:errno;}
bool FileSeek(FILE* file,int64_t offset,int origin){return fseeko(file,offset,origin)==0;}
int64_t FileTell(FILE* file){return ftello(file);}
#endif
size_t FileRead(void* value,size_t bytes,FILE* file){return fread(value,bytes,1,file);}
}
struct Reader {
    struct Options {std::string load_pipeline_cache_filename;} options_;
    bool Read(uint64_t id,std::vector<uint8_t>& cache_data) {
#define GFXRECON_LOG_WARNING(...) (void)0
#include "../../cmake/gui_pipeline_cache_loader.inl"
#undef GFXRECON_LOG_WARNING
    }
};
void Record(std::ofstream& file,uint64_t id,const std::string& value){
    const uint64_t bytes=value.size();file.write(reinterpret_cast<const char*>(&id),8);file.write(reinterpret_cast<const char*>(&bytes),8);file.write(value.data(),bytes);
}
void Require(bool result){if(!result)throw std::runtime_error("Indexed pipeline cache reader regression");}
}
void RunPipelineCacheTests(const std::filesystem::path& root){
    const auto file=root/"pipeline-reader.bin";Reader reader{{file.u8string()}};std::vector<uint8_t> data;
    {std::ofstream out(file,std::ios::binary);Record(out,1,"first");Record(out,9007199254740993ull,"large-id");Record(out,1,"latest");}
    Require(reader.Read(1,data)&&std::string(data.begin(),data.end())=="latest");
    Require(reader.Read(9007199254740993ull,data)&&std::string(data.begin(),data.end())=="large-id");
    Require(!reader.Read(2,data));
    {std::ofstream out(file,std::ios::binary|std::ios::app);Record(out,2,"appended");}
    Require(reader.Read(2,data)&&std::string(data.begin(),data.end())=="appended");
    {std::ofstream out(file,std::ios::binary|std::ios::trunc);out<<"bad";}
    Require(!reader.Read(1,data));
    {std::ofstream out(file,std::ios::binary|std::ios::trunc);uint64_t id=1,bytes=UINT64_MAX;out.write(reinterpret_cast<char*>(&id),8);out.write(reinterpret_cast<char*>(&bytes),8);}
    Require(!reader.Read(1,data));
    Require(Sha256Text("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::filesystem::remove(file);
}
}
