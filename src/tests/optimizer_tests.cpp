#include "replay/replay.h"
#include "core/artifacts.h"
#include "platform/process.h"
#include <fstream>
#include <future>
#include <thread>
#include <cstdlib>
#include <algorithm>

namespace gfx::tests {
namespace {
void SetEnvironment(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    if (*value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Json Load(const std::filesystem::path& file){std::ifstream in(file);return Json::parse(in);}
void Save(const std::filesystem::path& file,const std::string& value){std::ofstream out(file,std::ios::binary);out<<value;}
size_t Invocations(const std::filesystem::path& file){std::ifstream in(file);return std::count(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>(),'x');}
}
void RunOptimizerTests(const std::filesystem::path& root){
    const auto folder=std::filesystem::absolute(root/"optimizer-tests");std::filesystem::create_directories(folder);
    const auto helper=folder/"optimizer.exe",source=folder/"source.gfxr",counter=folder/"counter.txt";
    std::filesystem::copy_file(std::filesystem::u8path(NativeArguments()[0]),helper);
    Save(source,"original-a");
    SetEnvironment("GFXRECON_GUI_TEST_OPTIMIZER","copy");SetEnvironment("GFXRECON_GUI_TEST_OPTIMIZER_COUNTER",counter.u8string().c_str());
    struct Restore{~Restore(){SetEnvironment("GFXRECON_GUI_TEST_OPTIMIZER","");SetEnvironment("GFXRECON_GUI_TEST_OPTIMIZER_COUNTER","");}} restore;
    ReplayRequest request;request.capture=source;request.cacheDirectory=folder/"cache";request.expectedCaptureHash=Sha256File(source);
    std::map<std::string,std::string> tools{{"optimize",helper.u8string()}};
    auto run=[&](const char* name){
        request.output=folder/name;bool stoppedBeforeGpu=false;
        try{ReplayCapture(request,{},tools);}catch(const std::exception& e){stoppedBeforeGpu=std::string(e.what()).find("Configure the local replay")!=std::string::npos;}
        Require(stoppedBeforeGpu,"Optimizer fixture unexpectedly invoked a GPU backend");
        return Load(request.output/"request.json");
    };
    auto first=run("first");Require(first["optimization"]["status"]=="optimized","Derived copy was not prepared");
    Require(first["source_capture_sha256"]==request.expectedCaptureHash,"Original provenance was lost");
    Require(first["capture"]!=source.u8string(),"Optimizer overwrote original path");
    run("cached");Require(Invocations(counter)==1,"Unchanged preparation reran optimizer");
    {std::ofstream damage(std::filesystem::u8path(first["capture"].get<std::string>()),std::ios::binary|std::ios::app);damage<<'x';}
    run("repaired");Require(Invocations(counter)==2,"Corrupt derived copy was accepted");
    Save(source,"original-b");request.expectedCaptureHash=Sha256File(source);run("changed-source");
    Require(Invocations(counter)==3,"Changed source did not invalidate preparation");
    const auto helper2=folder/"optimizer-v2.exe";std::filesystem::copy_file(helper,helper2);
#ifdef __APPLE__
    // Change the executable identity without invalidating Apple Silicon code signing.
    Require(RunProcess({"/usr/bin/codesign", {"--force", "--sign", "-", "--identifier", "gfxrecon.test.optimizer.v2", helper2.u8string()}}).Success(), "Cannot sign modified optimizer fixture");
#else
    {std::ofstream version(helper2,std::ios::binary|std::ios::app);version<<'v';}
#endif
    tools["optimize"]=helper2.u8string();run("changed-tool");Require(Invocations(counter)==4,"Changed optimizer bytes did not invalidate preparation");
    SetEnvironment("GFXRECON_GUI_TEST_OPTIMIZER","noop");request.cacheDirectory=folder/"noop-cache";
    Require(run("noop")["optimization"]["status"]=="unchanged","No-change result was not retained");
    run("noop-cached");Require(Invocations(counter)==5,"No-change result reran optimizer");
    SetEnvironment("GFXRECON_GUI_TEST_OPTIMIZER","fail");request.cacheDirectory=folder/"fail-cache";
    Require(run("failed")["capture"]==source.u8string(),"Failed optimization did not fall back to original");
    request.optimize=false;request.cacheDirectory=folder/"original-cache";
    Require(run("original-mode")["capture"]==source.u8string()&&Invocations(counter)==6,"Original replay mode invoked optimizer");
    request.optimize=true;
    SetEnvironment("GFXRECON_GUI_TEST_OPTIMIZER","sleep");request.cacheDirectory=folder/"cancel-cache";request.output=folder/"cancelled";
    std::atomic_bool cancel{false};auto job=std::async(std::launch::async,[&]{try{ReplayCapture(request,{},tools,&cancel);}catch(...){return true;}return false;});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(Invocations(counter)<7&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cancel=true;Require(job.get(),"Cancelled optimizer published successful replay");
    for(const auto& file:std::filesystem::recursive_directory_iterator(request.cacheDirectory))
        Require(file.path().filename()!="manifest.json","Cancelled optimizer published a prepared copy");
    Require(Sha256File(source)==request.expectedCaptureHash,"Optimization modified original bytes");
    Require(folder.parent_path()==std::filesystem::absolute(root)&&!std::filesystem::is_symlink(folder),"Unsafe optimizer test cleanup");
    std::filesystem::remove_all(folder);
}
}
