#include "capture/live_capture.h"
#include "core/artifacts.h"
#include "core/log.h"
#include <fstream>
#include <sstream>
#include <mutex>
#include <thread>
#include <algorithm>
#include <set>
#include <cstdlib>

namespace gfx {
namespace {
std::string Text(const std::filesystem::path& file) {
    if(!std::filesystem::exists(file))return {};
    if(std::filesystem::file_size(file)>8*1024*1024)throw std::runtime_error("Capture log exceeds the supported 8 MiB limit");
    std::ifstream in(file,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};
}
std::string Trim(std::string value){while(!value.empty()&&(value.back()=='\r'||value.back()=='\n'||value.back()==' '))value.pop_back();return value;}
void ValidatePackage(const std::string& package) {
    if(package.empty()||package.find('.')==std::string::npos||package.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._")!=std::string::npos)
        throw std::runtime_error("Choose a valid debuggable Android package");
}
bool PriorEnabled(const Json& journal) {
    for(const auto& entry:journal.at("changes"))if(entry["name"]=="debug.gfxrecon.capture_android_trigger")return entry["previous"]=="true"||entry["previous"]=="1";
    return false;
}
}
struct LiveCaptureSession::Impl {
    LiveCaptureOptions options;
    std::atomic_bool abort{false};std::atomic_int command{0};
    AdbConnection adb;
    mutable std::mutex mutex;LiveCaptureState state;
    std::thread worker;std::unique_ptr<GameProcess> game;
    Json journal;std::filesystem::path journalFile;
    std::string relative,remote,activeFile;size_t logOffset=0;std::string partial;
    bool recording=false,ending=false,closeOnEnd=false,prepared=false,launched=false,armed=false;
    std::chrono::steady_clock::time_point lastTrigger{},lastRecord{},stopDeadline{};
    explicit Impl(LiveCaptureOptions o,const AdbConnection& device):options(std::move(o)),adb{device.executable,device.serial,&abort,device.transport}{worker=std::thread([this]{Run();});}
    ~Impl(){abort=true;if(worker.joinable())worker.join();}
    LiveCaptureState Snapshot()const{std::lock_guard<std::mutex> lock(mutex);return state;}
    void Phase(LivePhase phase,const std::string& detail){
        {std::lock_guard<std::mutex> lock(mutex);state.phase=phase;state.detail=detail;}
        if(phase==LivePhase::Failed){LOGE("capture")<<detail;}else{LOGI("capture")<<detail;}
        if(options.capture.progress)options.capture.progress(detail);
    }
    bool Alive(){
        const bool alive=options.android?adb.TryShell({"pidof",options.capture.package}).Success():game&&game->Running();
        {std::lock_guard<std::mutex> lock(mutex);state.gameRunning=alive;}return alive;
    }
    void SaveJournal(){if(!journalFile.empty())AtomicJson(journalFile,journal);}
    void Property(const std::string& name,const std::string& value,bool cleanup=false){
        for(auto& entry:journal["changes"])if(entry["name"]==name){
            if(Trim(adb.Shell({"getprop",name},cleanup))!=entry["value"].get<std::string>())throw std::runtime_error("Runtime capture settings changed outside this session; recovery requires attention.");
            entry["value"]=value;entry["restored"]=false;SaveJournal();adb.Shell({"setprop",name,value},cleanup);return;}
        throw std::runtime_error("Runtime capture property is not owned by this session");
    }
    void PrepareAndroid(){
        ValidatePackage(options.capture.package);
        if(adb.Shell({"dumpsys","window","policy"}).find("showing=true")!=std::string::npos)throw std::runtime_error("Phone is locked. Unlock it before launching the game.");
        if(adb.Shell({"pm","path","com.lunarg.gfxreconstruct.replay"}).find("package:")==std::string::npos)throw std::runtime_error("Install the compatible replay/layer APK before capture");
        adb.Shell({"run-as",options.capture.package,"id"});
        std::ostringstream id;id<<std::hex<<static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count());
        relative="files/g"+id.str();remote="/data/data/"+options.capture.package+"/"+relative;
        if((remote+"/c.gfxr").size()>91)throw std::runtime_error("Capture path exceeds the Android property size limit");
        if(adb.TryShell({"run-as",options.capture.package,"test","-e",relative}).Success())throw std::runtime_error("Capture staging directory already exists; start another session");
        adb.Shell({"run-as",options.capture.package,"mkdir","-p",relative});
        journalFile=options.capture.output/"device-journal.json";
        journal={{"schema",2},{"serial",adb.serial},{"package",options.capture.package},{"remote",remote},{"restored",false},{"live_session",true},{"changes",Json::array()}};
        auto add=[&](const char* kind,const char* name,const std::string& value){
            auto prior=std::string(kind)=="setting"?adb.Shell({"settings","get","global",name}):adb.Shell({"getprop",name});
            journal["changes"].push_back({{"kind",kind},{"name",name},{"value",value},{"previous",Trim(prior)},{"restored",false}});
        };
        add("setting","enable_gpu_debug_layers","1");add("setting","gpu_debug_app",options.capture.package);
        add("setting","gpu_debug_layers","VK_LAYER_LUNARG_gfxreconstruct");add("setting","gpu_debug_layer_app","com.lunarg.gfxreconstruct.replay");
        add("property","debug.gfxrecon.capture_file",remote+"/c.gfxr");add("property","debug.gfxrecon.capture_frames","");
        add("property","debug.gfxrecon.capture_file_timestamp","true");add("property","debug.gfxrecon.log_file",remote+"/l.txt");
        add("property","debug.gfxrecon.log_file_flush_after_write","true");add("property","debug.gfxrecon.log_level","info");
        add("property","debug.gfxrecon.capture_trigger","");add("property","debug.gfxrecon.capture_android_trigger","false");
        add("property","debug.gfxrecon.capture_trigger_boundary","frames");
        add("property","debug.gfxrecon.capture_trigger_frames",options.frames?std::to_string(options.frames):"");
        add("property","debug.gfxrecon.capture_process_name",options.capture.package);
        SaveJournal();prepared=true;
        for(const auto& e:journal["changes"]){const auto name=e["name"].get<std::string>(),value=e["value"].get<std::string>();
            if(e["kind"]=="setting")adb.Shell({"settings","put","global",name,value});else adb.Shell({"setprop",name,value});}
        auto activity=Trim(adb.Shell({"cmd","package","resolve-activity","--brief",options.capture.package}));const auto line=activity.find_last_of('\n');if(line!=std::string::npos)activity=activity.substr(line+1);
        if(activity.find('/')==std::string::npos)throw std::runtime_error("No launchable activity was found");
        adb.Stop(options.capture.package);
        std::vector<std::string> launch{"am","start","-W","-n",activity};if(!options.capture.arguments.empty())launch.insert(launch.end(),{"--es","args",options.capture.arguments});
        const auto output=adb.Shell(launch);if(output.find("Error:")!=std::string::npos||output.find("Exception")!=std::string::npos)throw std::runtime_error(output);
        launched=true;
    }
    void PrepareWindows(){
        if(!std::filesystem::is_regular_file(std::filesystem::u8path(options.layerDirectory)/"VkLayer_gfxreconstruct.json"))throw std::runtime_error("Configure the Vulkan capture layer directory");
        const auto capability=std::filesystem::u8path(options.layerDirectory)/"capture-control.json";
        if(!std::filesystem::is_regular_file(capability))throw std::runtime_error("Build/configure the GfxReconGui host capture layer for on-demand control.");
        {std::ifstream in(capability);const auto info=Json::parse(in);if(info.value("gui_capture_protocol",0)!=1||info.value("layer_sha256",std::string{})!=Sha256File(std::filesystem::u8path(options.layerDirectory)/"VkLayer_gfxreconstruct.dll"))throw std::runtime_error("Capture-layer control capability does not match its binary; rebuild the host tools.");}
        auto request=options.process;request.stdoutFile=options.capture.output/"game.log";
        const auto inheritedPath=std::getenv("VK_LAYER_PATH"),inheritedLayers=std::getenv("VK_INSTANCE_LAYERS");
        request.environment["VK_LAYER_PATH"]=options.layerDirectory+(inheritedPath&&*inheritedPath?std::string(";")+inheritedPath:"");
        request.environment["VK_INSTANCE_LAYERS"]=std::string("VK_LAYER_LUNARG_gfxreconstruct")+(inheritedLayers&&*inheritedLayers?std::string(";")+inheritedLayers:"");
        request.environment["GFXRECON_CAPTURE_FILE"]=(options.capture.output/"capture.gfxr").u8string();
        request.environment["GFXRECON_CAPTURE_FRAMES"]="";request.environment["GFXRECON_CAPTURE_TRIGGER"]="GFXRECON_GUI";
        request.environment["GFXRECON_CAPTURE_TRIGGER_BOUNDARY"]="frames";
        request.environment["GFXRECON_CAPTURE_TRIGGER_FRAMES"]=options.frames?std::to_string(options.frames):"";
        request.environment["GFXRECON_CAPTURE_FILE_TIMESTAMP"]="true";request.environment["GFXRECON_QUIT_AFTER_CAPTURE_FRAMES"]="false";
        request.environment["GFXRECON_LOG_FILE"]=(options.capture.output/"layer.log").u8string();
        request.environment["GFXRECON_LOG_FILE_FLUSH_AFTER_WRITE"]="true";request.environment["GFXRECON_LOG_LEVEL"]="info";
        game=std::make_unique<GameProcess>(request);prepared=launched=true;
    }
    std::string ReadLog(){
        if(!options.android)return Text(options.capture.output/"layer.log");
        const auto result=adb.TryShell({"run-as",options.capture.package,"cat",relative+"/l.txt"});
        if(!result.Success()){
            if(result.output.find("No such file")!=std::string::npos)return {};
            throw std::runtime_error("Capture connection failed: "+result.output);
        }
        {std::ofstream local(options.capture.output/"layer.log",std::ios::binary);local<<result.output;}return result.output;
    }
    void Trigger(bool enabled){
        if(options.android){
            if(enabled&&adb.Shell({"dumpsys","window","policy"}).find("showing=true")!=std::string::npos)throw std::runtime_error("Phone is locked. Unlock it before triggering capture.");
            Property("debug.gfxrecon.capture_android_trigger",enabled?"true":"false");
        }else game->SetCaptureEnabled(enabled);
    }
    void Collect(){
        if(activeFile.empty())throw std::runtime_error("Capture finished without a reported file");
        const auto number=Snapshot().captures.size()+1;const auto directory=options.capture.output/"captures";std::filesystem::create_directories(directory);
        const auto target=directory/("capture-"+std::to_string(number)+".gfxr");
        if(options.android){
            if(activeFile.rfind(remote+"/",0)!=0||activeFile.find("..")!=std::string::npos||activeFile.find('\\')!=std::string::npos)throw std::runtime_error("Unexpected capture output path");
            adb.Get(options.capture.package,activeFile,target);
        }else{
            const auto source=std::filesystem::u8path(activeFile);
            if(std::filesystem::weakly_canonical(source.parent_path())!=std::filesystem::weakly_canonical(options.capture.output)||std::filesystem::is_symlink(source))throw std::runtime_error("Capture output escaped its session");
            auto pending=target;pending+=".partial";std::filesystem::copy_file(source,pending);
            if(Sha256File(source)!=Sha256File(pending))throw std::runtime_error("Capture changed during retrieval; wait between trigger presses");
            std::filesystem::rename(pending,target);
        }
        if(std::filesystem::file_size(target)<=16)throw std::runtime_error("Capture output is empty");
        AtomicJson(directory/("capture-"+std::to_string(number)+".json"),Json{{"owner","gfxrecon_gui"},{"complete",true},{"file",target.filename().u8string()},{"sha256",Sha256File(target)},{"frames",options.frames},{"serial",adb.serial}});
        {std::lock_guard<std::mutex> lock(mutex);state.captures.push_back(target);}
    }
    void CloseGame(){if(options.android)adb.Stop(options.capture.package);else if(game)game->Stop();{std::lock_guard<std::mutex> lock(mutex);state.gameRunning=false;}}
    void Restore(){
        if(options.android&&prepared){
            if(PriorEnabled(journal)&&Snapshot().gameRunning){journal["requires_game_exit"]=true;SaveJournal();throw std::runtime_error("Prior runtime trigger was enabled. Close the launched game before restoring those settings.");}
            RestoreAndroidCapture({adb.executable,adb.serial,nullptr,adb.transport},journalFile);
        }
    }
    void FinishSession(){if(closeOnEnd)CloseGame();Restore();Phase(LivePhase::Ended,"Capture session ended; "+std::string(Snapshot().gameRunning?"game continues running":"game is closed"));}
    void Run(){
        try{
            if(options.frames>1000000)throw std::runtime_error("Choose a capture length of 1 to 1,000,000 frames");
            std::filesystem::create_directories(options.capture.output);
            Phase(LivePhase::Starting,"Launching with capture tracking armed; no frames are recorded yet");
            if(options.android)PrepareAndroid();else PrepareWindows();
            const auto readyDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
            while(!abort){
                const auto log=ReadLog();if(options.android?!log.empty():log.find("GfxReconGui capture control ready")!=std::string::npos)break;
                if(command.load()>=3){const int action=command.exchange(0);ending=true;closeOnEnd=action==4;break;}
                if(!Alive())throw std::runtime_error("Game exited before the capture layer became ready");if(std::chrono::steady_clock::now()>readyDeadline)throw std::runtime_error("Capture layer did not initialize; check Vulkan/layer compatibility");std::this_thread::sleep_for(std::chrono::milliseconds(100));}
            if(abort)throw std::runtime_error("Capture session cancelled while launching");
            if(ReadLog().find("Recording graphics API capture to ")!=std::string::npos){CloseGame();throw std::runtime_error("Conflicting layer settings started capture at launch. The launched app was closed; remove those settings before arming again.");}
            Alive();if(ending)FinishSession();else{armed=true;Phase(LivePhase::Armed,"Armed. Play the game, then trigger capture." );}
            while(!abort){
                const int action=command.exchange(0);const auto phase=Snapshot().phase;
                if(phase==LivePhase::Ended){if(action==4){CloseGame();Restore();Phase(LivePhase::Ended,"Launched game closed and settings restored");}std::this_thread::sleep_for(std::chrono::milliseconds(100));continue;}
                if(action==1&&phase==LivePhase::Armed){
                    while(!abort&&std::chrono::steady_clock::now()-lastTrigger<std::chrono::milliseconds(1100))std::this_thread::sleep_for(std::chrono::milliseconds(25));
                    Trigger(true);lastTrigger=std::chrono::steady_clock::now();Phase(LivePhase::Triggering,"Capture requested; waiting for the game's next frame");
                }
                if(action==2||action==3||action==4){
                    ending=action>=3;closeOnEnd=action==4;
                    if(recording){Trigger(false);stopDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);Phase(LivePhase::Stopping,"Stopping at a frame boundary; the game will keep running");}
                    else if(phase==LivePhase::Armed&&ending){FinishSession();continue;}
                    else if(phase==LivePhase::Triggering) {Trigger(false);ending=action>=3;closeOnEnd=action==4;stopDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);}
                }
                const auto log=ReadLog();if(log.size()<logOffset)throw std::runtime_error("Capture log was replaced unexpectedly");partial+=log.substr(logOffset);logOffset=log.size();
                size_t newline;
                while((newline=partial.find('\n'))!=std::string::npos){auto line=Trim(partial.substr(0,newline));partial.erase(0,newline+1);
                    const std::string start="Recording graphics API capture to ";const auto at=line.find(start);
                    if(at!=std::string::npos){activeFile=line.substr(at+start.size());recording=true;lastRecord=std::chrono::steady_clock::now();if(stopDeadline!=std::chrono::steady_clock::time_point{})stopDeadline=lastRecord+std::chrono::seconds(30);Phase(LivePhase::Recording,"Recording "+std::string(options.frames?std::to_string(options.frames)+" frames":"until Stop capture"));}
                    if(line.find("Finished recording graphics API capture")!=std::string::npos&&recording){
                        recording=false;Phase(LivePhase::Retrieving,"Retrieving finalized capture; the game remains running");
                        Trigger(false);Collect();stopDeadline={};
                        if(ending)FinishSession();else Phase(LivePhase::Armed,"Capture complete. Game is still running; capture again when ready.");
                    }
                }
                if(recording&&stopDeadline!=std::chrono::steady_clock::time_point{}&&Snapshot().phase!=LivePhase::Stopping){Trigger(false);Phase(LivePhase::Stopping,"Waiting for capture to finish at a frame boundary");}
                if(stopDeadline!=std::chrono::steady_clock::time_point{}&&std::chrono::steady_clock::now()>stopDeadline){
                    if(!recording){stopDeadline={};if(ending)FinishSession();else Phase(LivePhase::Armed,"Capture request cancelled before the game produced a frame.");}
                    else throw std::runtime_error("Game did not reach a capture boundary after Stop; incomplete output was retained");
                }
                if(!Alive()){Restore();Phase(LivePhase::Ended,recording?"Game exited while recording. Partial output was retained and not imported.":"Game exited; settings restored");recording=false;}
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
            }
            if(Snapshot().phase==LivePhase::Ended)return;
            if(recording||options.android)try{if(options.android)Property("debug.gfxrecon.capture_android_trigger","false",true);else Trigger(false);}catch(...){}
            Restore();
        }catch(const std::exception& error){
            std::string detail=error.what();
            if(game)game->SetCaptureEnabled(false);
            if(!abort&&!armed&&launched)try{CloseGame();}catch(...){}
            if(options.android&&prepared){try{Property("debug.gfxrecon.capture_android_trigger","false",true);Restore();}catch(const std::exception& restore){detail+="\nRecovery pending: "+std::string(restore.what());}}
            Phase(LivePhase::Failed,detail);
            while(!abort){if(command.exchange(0)==4){try{CloseGame();Restore();Phase(LivePhase::Ended,"Game closed; settings restored");}catch(const std::exception& e){Phase(LivePhase::Failed,e.what());}}std::this_thread::sleep_for(std::chrono::milliseconds(100));}
        }
    }
};
LiveCaptureSession::LiveCaptureSession(LiveCaptureOptions options,const AdbConnection& device):impl_(std::make_unique<Impl>(std::move(options),device)){}
LiveCaptureSession::~LiveCaptureSession()=default;
LiveCaptureState LiveCaptureSession::State()const{return impl_->Snapshot();}
void LiveCaptureSession::Capture(){if(State().phase==LivePhase::Armed){int empty=0;impl_->command.compare_exchange_strong(empty,1);}}
void LiveCaptureSession::StopCapture(){impl_->command=2;}
void LiveCaptureSession::End(bool closeGame){impl_->command=closeGame?4:3;}
bool LiveCaptureSession::Active()const{const auto phase=State().phase;return phase!=LivePhase::Ended&&phase!=LivePhase::Failed;}
}
