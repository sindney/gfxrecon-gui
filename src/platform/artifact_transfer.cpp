#include "platform/artifact_transfer.h"
#include <lz4frame.h>
#define XXH_NAMESPACE LZ4_
#include <xxhash.h>
#include <array>
#include <fstream>
#include <charconv>
#include <stdexcept>
#include <algorithm>

namespace gfx {
namespace {
std::string Field(std::istream& input){
    std::string value;char c;
    while(input.get(c)){if(!c)return value;if(value.size()>=255)throw std::runtime_error("Artifact transfer header is too long");value+=c;}
    throw std::runtime_error("Artifact transfer header was truncated");
}
uint64_t Number(const std::string& text,int base=10){
    uint64_t number=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number,base);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw std::runtime_error("Invalid artifact transfer length or hash");
    return number;
}
}
bool DecodeArtifactTransfer(const std::filesystem::path& wire,const std::filesystem::path& decoded,
                            ArtifactTransferStats& stats,const std::atomic_bool* cancel){
    auto check=[&]{if(cancel&&cancel->load())throw std::runtime_error("Artifact transfer cancelled");};check();
    std::ifstream input(wire,std::ios::binary);stats.wireBytes=std::filesystem::file_size(wire);
    const auto magic=Field(input);
    if(magic=="GFXRECON_ARTIFACTS_1"){stats.rawBytes=stats.wireBytes;return false;}
    if(magic!="GFXRECON_TRANSFER_1"||Field(input)!="lz4")throw std::runtime_error("Invalid artifact compression protocol");
    const uint64_t raw=Number(Field(input)),packed=Number(Field(input));const auto hashText=Field(input);
    if(hashText.size()!=16||hashText.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid artifact content hash");
    const auto expected=Number(hashText,16);const auto offset=input.tellg();
    if(offset<0||uint64_t(offset)>stats.wireBytes||!raw||raw>64ull*1024*1024*1024||!packed||packed!=stats.wireBytes-uint64_t(offset))
        throw std::runtime_error("Incomplete artifact transfer: payload length mismatch");
    struct Context{LZ4F_decompressionContext_t value=nullptr;~Context(){LZ4F_freeDecompressionContext(value);}} context;
    struct Hash{XXH64_state_t* value=XXH64_createState();~Hash(){XXH64_freeState(value);}} hash;
    if(!hash.value||XXH64_reset(hash.value,0)||LZ4F_isError(LZ4F_createDecompressionContext(&context.value,LZ4F_VERSION)))throw std::runtime_error("Cannot initialize artifact decoder");
    struct Remove{std::filesystem::path path;bool keep=false;~Remove(){if(!keep){std::error_code e;std::filesystem::remove(path,e);}}} cleanup{decoded};
    std::ofstream output(decoded,std::ios::binary);
    std::array<char,65536> compressed,bytes;size_t used=0,position=0;uint64_t remaining=packed,written=0;
    for(;;){
        check();
        if(position==used&&remaining){
            used=static_cast<size_t>(std::min<uint64_t>(remaining,compressed.size()));position=0;
            if(!input.read(compressed.data(),static_cast<std::streamsize>(used)))throw std::runtime_error("Artifact transfer was truncated");remaining-=used;
        }
        size_t source=used-position,destination=bytes.size();
        const auto hint=LZ4F_decompress(context.value,bytes.data(),&destination,compressed.data()+position,&source,nullptr);
        if(LZ4F_isError(hint))throw std::runtime_error("Corrupt LZ4 artifact transfer");position+=source;
        if(destination>raw-written)throw std::runtime_error("Artifact transfer exceeds its declared content size");
        if(!output.write(bytes.data(),static_cast<std::streamsize>(destination)))throw std::runtime_error("Cannot save decoded artifacts");
        XXH64_update(hash.value,bytes.data(),destination);written+=destination;
        if(!hint){if(remaining||position!=used)throw std::runtime_error("Trailing bytes after LZ4 artifact frame");break;}
        if(!source&&!destination)throw std::runtime_error("Incomplete LZ4 artifact frame");
    }
    output.close();check();
    if(!output||written!=raw||XXH64_digest(hash.value)!=expected)throw std::runtime_error("Artifact transfer content checksum or size mismatch");
    stats.codec="lz4";stats.integrity="xxh64";stats.rawBytes=raw;cleanup.keep=true;return true;
}
}
