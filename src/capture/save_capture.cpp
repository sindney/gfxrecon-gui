#include "capture/save_capture.h"
#include "core/artifacts.h"
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gfx {
CaptureSaveResult SaveCaptureFile(const std::filesystem::path& input, std::filesystem::path destination,
    const std::string& expectedHash, bool replaceExisting, const std::atomic_bool* cancel) {
    const auto source=std::filesystem::absolute(input).lexically_normal();
    if(destination.empty()||destination.filename().empty())throw std::runtime_error("Choose a capture filename");
    if(destination.extension().empty())destination+=".gfxr";
    auto extension=destination.extension().u8string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension!=".gfxr")throw std::runtime_error("Save the capture with a .gfxr extension");
    destination.replace_extension(".gfxr");
    destination=std::filesystem::absolute(destination).lexically_normal();
    if(!std::filesystem::is_regular_file(source)||!std::filesystem::file_size(source))throw std::runtime_error("The completed capture is missing or empty");
    const auto hash=Sha256File(source,cancel);
    if(!expectedHash.empty()&&hash!=expectedHash)throw std::runtime_error("Capture changed after completion; the original was not overwritten");
    if(std::filesystem::exists(destination)&&std::filesystem::equivalent(source,destination))return {source,destination,hash};
    if(std::filesystem::exists(destination)&&(!replaceExisting||!std::filesystem::is_regular_file(destination)))
        throw std::runtime_error("The destination already exists; choose another name or confirm replacement in Save As");
    std::filesystem::create_directories(destination.parent_path());
    auto pending=destination;pending+=std::string(".")+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".partial";
    struct Cleanup {std::filesystem::path file;~Cleanup(){std::error_code ignored;std::filesystem::remove(file,ignored);}} cleanup{pending};
    {
        std::ifstream in(source,std::ios::binary);std::ofstream out(pending,std::ios::binary);
        if(!in||!out)throw std::runtime_error("Cannot open capture save destination");
        std::vector<char> buffer(1024*1024);
        while(in.read(buffer.data(),buffer.size())||in.gcount()) {
            if(cancel&&cancel->load())throw std::runtime_error("Capture save cancelled");
            if(!out.write(buffer.data(),in.gcount()))throw std::runtime_error("Capture save failed; check space and destination permissions");
        }
        if(in.bad())throw std::runtime_error("Cannot read completed capture");
        out.close();if(!out)throw std::runtime_error("Cannot finalize capture copy");
    }
    if(Sha256File(pending,cancel)!=hash||Sha256File(source,cancel)!=hash)throw std::runtime_error("Capture changed during saving; destination was not replaced");
    if(cancel&&cancel->load())throw std::runtime_error("Capture save cancelled");
#ifdef _WIN32
    const DWORD flags=MOVEFILE_WRITE_THROUGH|(replaceExisting?MOVEFILE_REPLACE_EXISTING:0);
    if(!MoveFileExW(pending.c_str(),destination.c_str(),flags))throw std::runtime_error("Cannot publish capture copy (Windows error "+std::to_string(GetLastError())+")");
#else
    if(std::filesystem::exists(destination)&&!replaceExisting)throw std::runtime_error("Capture destination appeared during saving");
    std::filesystem::rename(pending,destination);
#endif
    return {source,destination,hash};
}
}
