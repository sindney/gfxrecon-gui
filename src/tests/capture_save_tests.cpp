#include "capture/save_capture.h"
#include "core/artifacts.h"
#include <fstream>
#include <vector>
#include <stdexcept>

namespace gfx::tests {
namespace {
void Require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class F>void Rejected(F work){bool failed=false;try{work();}catch(...){failed=true;}Require(failed,"Invalid capture save unexpectedly succeeded");}
}
void RunCaptureSaveTests(const std::filesystem::path& root){
    const auto folder=std::filesystem::absolute(root/"capture-save-tests");std::filesystem::create_directories(folder);
    const auto source=folder/"original.gfxr";
    {std::ofstream out(source,std::ios::binary);std::vector<char> bytes(3*1024*1024);for(size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<char>(i%251);out.write(bytes.data(),bytes.size());}
    const auto hash=Sha256File(source);
    Require(SaveCaptureFile(source,source,hash).destination==source,"Default save changed capture location");
    const auto chosen=folder/std::filesystem::u8path(u8"chosen folder/雪地 & frame 01");
    const auto saved=SaveCaptureFile(source,chosen,hash);
    Require(saved.destination.extension()==".gfxr"&&saved.sha256==hash&&Sha256File(saved.destination)==hash,"Renamed Unicode copy differs from original");
    const auto occupied=folder/"occupied.gfxr";
    {std::ofstream out(occupied);out<<"keep this file";}
    const auto previous=Sha256File(occupied);
    Rejected([&]{SaveCaptureFile(source,occupied,hash);});
    Require(Sha256File(occupied)==previous,"Unconfirmed save overwrote destination");
    SaveCaptureFile(source,occupied,hash,true);Require(Sha256File(occupied)==hash,"Confirmed replacement failed");
    Rejected([&]{SaveCaptureFile(source,folder/"changed.gfxr","incorrect hash");});
    std::atomic_bool cancel{true};Rejected([&]{SaveCaptureFile(source,folder/"cancelled.gfxr",hash,false,&cancel);});
    Require(!std::filesystem::exists(folder/"cancelled.gfxr"),"Cancelled save published a file");
    Rejected([&]{SaveCaptureFile(source,occupied/"invalid.gfxr",hash);});
    Rejected([&]{SaveCaptureFile(source,folder/"wrong.txt",hash);});
    Rejected([&]{SaveCaptureFile(folder/"missing.gfxr",folder/"missing-copy.gfxr");});
    SaveCaptureFile(source,folder/"upper.GFXR",hash);
    Require(Sha256File(source)==hash,"Saving modified the original capture");
    for(const auto& file:std::filesystem::recursive_directory_iterator(folder))Require(file.path().extension()!=".partial","Failed save left partial output");
    Require(folder.parent_path()==std::filesystem::absolute(root)&&!std::filesystem::is_symlink(folder),"Unsafe save-test cleanup");
    std::filesystem::remove_all(folder);
}
}
