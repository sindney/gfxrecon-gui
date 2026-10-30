#include "capture/live_capture.h"
#include "core/artifacts.h"
#include "core/log.h"
#include "platform/device_lock.h"
#include <optional>
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
const char* kTrigger="debug.gfxrecon.capture_android_trigger";
const std::string kRecordingStart="Recording graphics API capture to ",kRecordingDone="Finished recording graphics API capture";
Json LoadJournal(const std::filesystem::path& file){std::ifstream in(file);if(!in)throw std::runtime_error("Cannot read capture journal "+file.u8string());return Json::parse(in);}
int64_t NowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
bool Cancelled(const AdbConnection& adb){return adb.cancel&&adb.cancel->load();}
// Sets a journaled property; refuses when something else changed it since the journal recorded it.
void SetJournalProperty(const AdbConnection& adb,const AndroidSessionState& state,const std::string& name,const std::string& value,bool cleanup){
    auto journal=LoadJournal(state.Journal());
    for(auto& entry:journal["changes"])if(entry["name"]==name){
        if(Trim(adb.Shell({"getprop",name},cleanup))!=entry["value"].get<std::string>())throw std::runtime_error("Runtime capture settings changed outside this session; recovery requires attention.");
        entry["value"]=value;entry["restored"]=false;AtomicJson(state.Journal(),journal);adb.Shell({"setprop",name,value},cleanup);return;}
    throw std::runtime_error("Runtime capture property is not owned by this session");
}
// Full layer log text from the device; empty before the layer creates it. A local copy is kept in the session directory.
std::string ReadAndroidLog(const AdbConnection& adb,const AndroidSessionState& state){
    const auto result=adb.TryShell({"run-as",state.package,"cat",state.relative+"/l.txt"});
    if(result.cancelled)throw std::runtime_error("Capture cancelled.");
    if(result.timedOut)throw std::runtime_error("Capture log read timed out. Check the selected device connection.");
    if(!result.Success()){
        if(result.output.find("No such file")!=std::string::npos)return {};
        throw std::runtime_error("Capture connection failed: "+result.output);
    }
    const auto local=state.directory/"layer.log";std::error_code ignored;
    if(!std::filesystem::exists(local,ignored)||std::filesystem::file_size(local,ignored)!=result.output.size()){std::ofstream out(local,std::ios::binary);out<<result.output;}
    return result.output;
}
bool Running(const AdbConnection& adb,const std::string& package,bool cleanup=false){return adb.TryShell({"pidof",package},cleanup).Success();}
void MarkEnded(const std::filesystem::path& directory){
    try{if(std::filesystem::is_regular_file(directory/"session.json")){auto state=LoadAndroidSession(directory);state.ended=true;SaveAndroidSession(state);}}catch(...){}
}
}
void RequireAndroidUnlocked(const AdbConnection& adb){
    const auto state=ParseAndroidLockState(adb.Shell({"dumpsys","window","policy"}),adb.serial);
    if(state.known&&state.locked)throw AndroidUnlockRequired(state,"Capture never enters credentials; unlock the phone, then retry.");
}
AndroidSessionState LoadAndroidSession(const std::filesystem::path& directory){
    std::ifstream in(directory/"session.json");if(!in)throw std::runtime_error("Not an Android capture session (session.json missing): "+directory.u8string());
    const auto j=Json::parse(in);AndroidSessionState state;state.directory=directory;
    state.serial=j.value("serial",std::string{});state.package=j.value("package",std::string{});state.relative=j.value("relative",std::string{});state.remote=j.value("remote",std::string{});
    state.frames=j.value("frames",0u);state.captures=j.value("captures",0u);state.logOffset=j.value("log_offset",uint64_t{0});state.lastTriggerMs=j.value("last_trigger_ms",int64_t{0});
    state.launched=j.value("launched",false);state.ended=j.value("ended",false);state.prepared=true;
    if(state.serial.empty()||state.package.empty()||state.relative.empty())throw std::runtime_error("Capture session.json is incomplete: "+directory.u8string());
    return state;
}
Json AndroidSessionJson(const AndroidSessionState& state){
    return {{"schema",1},{"serial",state.serial},{"package",state.package},{"relative",state.relative},{"remote",state.remote},{"frames",state.frames},{"captures",state.captures},
        {"log_offset",state.logOffset},{"last_trigger_ms",state.lastTriggerMs},{"launched",state.launched},{"ended",state.ended}};
}
void SaveAndroidSession(const AndroidSessionState& state){AtomicJson(state.directory/"session.json",AndroidSessionJson(state));}
void PrepareAndroidCapture(const AdbConnection& adb,const AndroidArmOptions& options,AndroidSessionState& state){
    DeviceLock deviceLock(adb.serial);
    ValidatePackage(options.package);
    if(options.frames>1000000)throw std::runtime_error("Choose a capture length of 1 to 1,000,000 frames");
    RequireAndroidUnlocked(adb);
    if(adb.Shell({"pm","path","com.lunarg.gfxreconstruct.replay"}).find("package:")==std::string::npos)throw std::runtime_error("Install the compatible replay/layer APK before capture");
    adb.Shell({"run-as",options.package,"id"});
    std::filesystem::create_directories(options.directory);
    if(std::filesystem::exists(options.directory/"device-journal.json"))throw std::runtime_error("Capture session directory already holds a device journal; choose a new directory or restore it first");
    std::ostringstream id;id<<std::hex<<static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count());
    state=AndroidSessionState{};state.directory=options.directory;state.serial=adb.serial;state.package=options.package;state.frames=options.frames;
    state.relative="files/g"+id.str();state.remote="/data/data/"+options.package+"/"+state.relative;
    if((state.remote+"/c.gfxr").size()>91)throw std::runtime_error("Capture path exceeds the Android property size limit");
    if(adb.TryShell({"run-as",options.package,"test","-e",state.relative}).Success())throw std::runtime_error("Capture staging directory already exists; start another session");
    adb.Shell({"run-as",options.package,"mkdir","-p",state.relative});
    Json journal={{"schema",2},{"serial",adb.serial},{"package",options.package},{"remote",state.remote},{"restored",false},{"live_session",true},{"changes",Json::array()}};
    auto add=[&](const char* kind,const char* name,const std::string& value){
        auto prior=std::string(kind)=="setting"?adb.Shell({"settings","get","global",name}):adb.Shell({"getprop",name});
        journal["changes"].push_back({{"kind",kind},{"name",name},{"value",value},{"previous",Trim(prior)},{"restored",false}});
    };
    add("setting","enable_gpu_debug_layers","1");add("setting","gpu_debug_app",options.package);
    add("setting","gpu_debug_layers","VK_LAYER_LUNARG_gfxreconstruct");add("setting","gpu_debug_layer_app","com.lunarg.gfxreconstruct.replay");
    add("property","debug.gfxrecon.capture_file",state.remote+"/c.gfxr");add("property","debug.gfxrecon.capture_frames","");
    add("property","debug.gfxrecon.capture_file_timestamp","true");add("property","debug.gfxrecon.log_file",state.remote+"/l.txt");
    add("property","debug.gfxrecon.log_file_flush_after_write","true");add("property","debug.gfxrecon.log_level","info");
    add("property","debug.gfxrecon.capture_trigger","");add("property",kTrigger,"false");
    add("property","debug.gfxrecon.capture_trigger_boundary","frames");
    add("property","debug.gfxrecon.capture_trigger_frames",options.frames?std::to_string(options.frames):"");
    add("property","debug.gfxrecon.capture_process_name",options.package);
    AtomicJson(state.Journal(),journal);SaveAndroidSession(state);state.prepared=true;
    for(const auto& e:journal["changes"]){const auto name=e["name"].get<std::string>(),value=e["value"].get<std::string>();
        if(e["kind"]=="setting")adb.Shell({"settings","put","global",name,value});else adb.Shell({"setprop",name,value});}
    if(!options.launch)return;
    auto activity=Trim(adb.Shell({"cmd","package","resolve-activity","--brief",options.package}));const auto line=activity.find_last_of('\n');if(line!=std::string::npos)activity=activity.substr(line+1);
    if(activity.find('/')==std::string::npos)throw std::runtime_error("No launchable activity was found");
    adb.Stop(options.package);
    std::vector<std::string> launch{"am","start","-W","-n",activity};if(!options.arguments.empty())launch.insert(launch.end(),{"--es","args",options.arguments});
    const auto output=adb.Shell(launch);if(output.find("Error:")!=std::string::npos||output.find("Exception")!=std::string::npos)throw std::runtime_error(output);
    state.launched=true;SaveAndroidSession(state);
}
bool WaitAndroidArmed(const AdbConnection& adb,AndroidSessionState& state,std::chrono::seconds timeout,const std::function<bool()>& stop){
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    for(;;){
        if(stop&&stop())return false;
        if(Cancelled(adb))throw std::runtime_error("Capture session cancelled while launching");
        if(!ReadAndroidLog(adb,state).empty())break;
        if(!Running(adb,state.package))throw std::runtime_error("Game exited before the capture layer became ready");
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Capture layer did not initialize; check Vulkan/layer compatibility");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if(ReadAndroidLog(adb,state).find(kRecordingStart)!=std::string::npos){
        adb.Stop(state.package);
        throw std::runtime_error("Conflicting layer settings started capture at launch. The launched app was closed; remove those settings before arming again.");
    }
    return true;
}
AndroidSessionState ArmAndroidCapture(const AdbConnection& adb,const AndroidArmOptions& options,const std::function<void(const std::string&)>& progress){
    DeviceLock deviceLock(adb.serial);
    AndroidSessionState state;
    auto cleanup=[&]{
        std::string pending;
        if(state.launched)try{adb.Stop(state.package);}catch(...){}
        if(state.prepared){
            try{SetAndroidTrigger(adb,state,false,true);RestoreAndroidJournal(adb,state.Journal());}catch(const std::exception& e){pending="\nRecovery pending: "+std::string(e.what());}
        }
        return pending;
    };
    try{
        if(progress)progress(options.launch?"Preparing capture settings and launching with tracking armed":"Preparing capture settings");
        PrepareAndroidCapture(adb,options,state);
        if(options.launch){
            if(progress)progress("Waiting for the capture layer to report armed");
            WaitAndroidArmed(adb,state,options.timeout);
        }
    }catch(const AndroidUnlockRequired&){cleanup();throw;}
    catch(const std::exception& error){
        const auto pending=cleanup();if(pending.empty())throw;
        throw std::runtime_error(std::string(error.what())+pending);
    }
    return state;
}
void SetAndroidTrigger(const AdbConnection& adb,AndroidSessionState& state,bool enabled,bool cleanup){
    if(enabled){
        RequireAndroidUnlocked(adb); // before anything is touched: a locked phone leaves the session armed
        while(!Cancelled(adb)){const auto since=NowMs()-state.lastTriggerMs;if(since<0||since>=1100)break;std::this_thread::sleep_for(std::chrono::milliseconds(25));}
        if(Cancelled(adb))throw std::runtime_error("Capture cancelled.");
    }
    SetJournalProperty(adb,state,kTrigger,enabled?"true":"false",cleanup);
    if(enabled){state.lastTriggerMs=NowMs();SaveAndroidSession(state);}
}
std::vector<AndroidLogLine> ReadAndroidLogLines(const AdbConnection& adb,const AndroidSessionState& state){
    const auto text=ReadAndroidLog(adb,state);
    if(text.size()<state.logOffset)throw std::runtime_error("Capture log was replaced unexpectedly");
    std::vector<AndroidLogLine> lines;size_t position=static_cast<size_t>(state.logOffset),newline;
    while((newline=text.find('\n',position))!=std::string::npos){lines.push_back({Trim(text.substr(position,newline-position)),position,newline+1});position=newline+1;}
    return lines;
}
void AdvanceAndroidLog(AndroidSessionState& state,uint64_t offset,bool save){state.logOffset=offset;if(save)SaveAndroidSession(state);}
AndroidCaptureFile CollectAndroidCapture(const AdbConnection& adb,AndroidSessionState& state,const std::string& remoteFile){
    if(remoteFile.empty())throw std::runtime_error("Capture finished without a reported file");
    if(remoteFile.rfind(state.remote+"/",0)!=0||remoteFile.find("..")!=std::string::npos||remoteFile.find('\\')!=std::string::npos)throw std::runtime_error("Unexpected capture output path");
    const auto number=state.captures+1;const auto directory=state.directory/"captures";std::filesystem::create_directories(directory);
    const auto target=directory/("capture-"+std::to_string(number)+".gfxr");
    adb.Get(state.package,remoteFile,target);
    const auto bytes=std::filesystem::file_size(target);
    if(bytes<=16)throw std::runtime_error("Capture output is empty");
    auto discard=[&](const std::string& reason){std::error_code ignored;std::filesystem::remove(target,ignored);throw std::runtime_error(reason);};
    // Best-effort device-side verification; a device without stat/sha256sum (or one too slow for the adb timeout) skips that check.
    const auto remoteSize=adb.TryShell({"run-as",state.package,"stat","-c","%s",remoteFile});
    if(remoteSize.Success()){const auto text=Trim(remoteSize.output);
        if(!text.empty()&&text.find_first_not_of("0123456789")==std::string::npos&&std::stoull(text)!=bytes)discard("Capture transfer was truncated: device reports "+text+" bytes, received "+std::to_string(bytes));}
    AndroidCaptureFile file{target,bytes,Sha256File(target),state.frames};
    const auto remoteHash=adb.TryShell({"run-as",state.package,"sha256sum",remoteFile});
    if(remoteHash.Success()&&remoteHash.output.size()>=64){auto hash=remoteHash.output.substr(0,64);
        if(hash.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos){std::transform(hash.begin(),hash.end(),hash.begin(),::tolower);
            if(hash!=file.sha256)discard("Capture transfer failed hash verification (device "+hash+", host "+file.sha256+")");}}
    AtomicJson(directory/("capture-"+std::to_string(number)+".json"),Json{{"owner","gfxrecon_gui"},{"complete",true},{"file",target.filename().u8string()},{"sha256",file.sha256},{"bytes",bytes},{"frames",state.frames},{"serial",adb.serial}});
    state.captures=number;SaveAndroidSession(state);
    return file;
}
std::vector<AndroidCaptureFile> TriggerAndroidCapture(const AdbConnection& adb,const std::filesystem::path& directory,uint32_t frames,std::chrono::seconds timeout,const std::function<void(const std::string&)>& progress){
    DeviceLock deviceLock(adb.serial);
    auto state=LoadAndroidSession(directory);
    if(state.serial!=adb.serial)throw std::runtime_error("Capture session belongs to device "+state.serial);
    if(state.ended||LoadJournal(state.Journal()).value("restored",false))throw std::runtime_error("Capture session has ended and its settings are restored; arm a new session");
    if(!state.frames)throw std::runtime_error("Session was armed for manual start/stop, which is only available in the GUI");
    if(frames&&frames!=state.frames)throw std::runtime_error("Frames per capture are fixed when arming ("+std::to_string(state.frames)+"); arm a new session for "+std::to_string(frames));
    RequireAndroidUnlocked(adb);
    if(ReadAndroidLog(adb,state).empty())throw std::runtime_error(Running(adb,state.package)?"The capture layer has not reported ready. Retry shortly; if the game was already running when armed, restart it (the layer loads at process start)":"The game is not running with capture armed. Launch it (arm without --no-launch), then trigger");
    std::vector<AndroidCaptureFile> files;
    std::string active;bool recording=false,requested=false,done=false;uint64_t recordingStart=state.logOffset;
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    auto process=[&](bool& finished){
        const auto lines=ReadAndroidLogLines(adb,state);uint64_t consumed=state.logOffset;
        for(const auto& line:lines){
            const auto at=line.text.find(kRecordingStart);
            if(at!=std::string::npos){active=line.text.substr(at+kRecordingStart.size());recording=true;recordingStart=line.begin;if(progress)progress("Recording "+std::to_string(state.frames)+" frames");}
            else if(line.text.find(kRecordingDone)!=std::string::npos&&recording){
                recording=false;if(progress)progress("Retrieving the finalized capture");
                SetAndroidTrigger(adb,state,false);
                state.logOffset=line.end;files.push_back(CollectAndroidCapture(adb,state,active));active.clear();
                if(requested)finished=true;
            }
            if(!recording)consumed=line.end;
        }
        state.logOffset=recording?recordingStart:consumed;SaveAndroidSession(state);
    };
    try{
        bool leftover=false;process(leftover); // finished captures an interrupted trigger left behind
        if(!recording){
            if(progress)progress("Requesting capture; waiting for the game's next frame");
            requested=true;SetAndroidTrigger(adb,state,true);
        }else requested=true; // an earlier trigger is still recording: wait for it instead of starting another
        while(!done){
            if(Cancelled(adb))throw std::runtime_error("Capture cancelled.");
            if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Capture timed out after "+std::to_string(timeout.count())+" seconds without reaching its frame count. Is the game rendering frames?");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            process(done);
            if(!done&&!Running(adb,state.package)){process(done);if(!done)throw std::runtime_error("Game exited while capturing; partial output stays on the device in "+state.remote);}
        }
    }catch(const AndroidUnlockRequired&){throw;}
    catch(...){
        if(requested&&!done)try{SetAndroidTrigger(adb,state,false,true);}catch(...){}
        throw;
    }
    return files;
}
Json AndroidCaptureStatus(const AdbConnection& adb,const std::filesystem::path& directory){
    auto state=LoadAndroidSession(directory);
    if(state.serial!=adb.serial)throw std::runtime_error("Capture session belongs to device "+state.serial);
    Json status=AndroidSessionJson(state);status["session"]=directory.u8string();status["journal"]=state.Journal().u8string();
    bool journalRestored=false;
    try{journalRestored=LoadJournal(state.Journal()).value("restored",false);}catch(...){status["journal_error"]="journal missing or unreadable";}
    status["restored"]=journalRestored;
    const auto policy=adb.TryShell({"dumpsys","window","policy"});
    status["device_reachable"]=policy.Success();
    if(policy.Success()){const auto lock=ParseAndroidLockState(policy.output,adb.serial);status["locked"]=lock.known?Json(lock.locked):Json("unknown");}
    status["game_running"]=Running(adb,state.package);
    bool layerReady=false,recording=false;
    if(!state.ended&&!journalRestored){
        const auto log=adb.TryShell({"run-as",state.package,"cat",state.relative+"/l.txt"});
        if(log.Success()&&!log.output.empty()){
            layerReady=true;std::istringstream lines(log.output.substr(std::min<size_t>(log.output.size(),static_cast<size_t>(state.logOffset))));std::string line;
            while(std::getline(lines,line)){if(line.find(kRecordingStart)!=std::string::npos)recording=true;else if(line.find(kRecordingDone)!=std::string::npos)recording=false;}
        }
        const auto trigger=adb.TryShell({"getprop",kTrigger});if(trigger.Success())status["trigger_property"]=Trim(trigger.output);
    }
    status["layer_ready"]=layerReady;status["recording"]=recording;
    Json files=Json::array();const auto captures=directory/"captures";
    if(std::filesystem::is_directory(captures)){
        std::vector<std::filesystem::path> list;for(const auto& entry:std::filesystem::directory_iterator(captures))if(entry.path().extension()==".gfxr")list.push_back(entry.path());
        std::sort(list.begin(),list.end());for(const auto& path:list)files.push_back({{"path",path.u8string()},{"bytes",std::filesystem::file_size(path)}});
    }
    status["capture_files"]=files;
    return status;
}
void RestoreAndroidJournal(const AdbConnection& adb,const std::filesystem::path& journalFile){
    try{RestoreAndroidCapture(adb,journalFile);}
    catch(const std::exception& error){
        const std::string message=error.what();
        if(message.rfind("Device settings changed outside",0)!=0)throw;
        Json conflicts=Json::array();
        try{const auto journal=LoadJournal(journalFile);for(const auto& entry:journal["changes"])if(!entry.value("restored",false)){
            const auto name=entry["name"].get<std::string>();
            const auto current=entry["kind"]=="setting"?adb.TryShell({"settings","get","global",name},true):adb.TryShell({"getprop",name},true);
            const auto now=current.Success()?Trim(current.output):std::string{};
            if(now!=entry["previous"].get<std::string>()&&now!=entry["value"].get<std::string>())conflicts.push_back({{"kind",entry["kind"]},{"name",name},{"session_value",entry["value"]},{"previous",entry["previous"]},{"current",now}});
        }}catch(const std::exception& e){LOGW("capture")<<e.what();}
        throw AndroidRestoreConflict(message,conflicts,journalFile);
    }
    MarkEnded(journalFile.parent_path());
}
Json EndAndroidCapture(const AdbConnection& adb,const std::filesystem::path& directory,bool closeGame){
    DeviceLock deviceLock(adb.serial);
    auto state=LoadAndroidSession(directory);
    if(state.serial!=adb.serial)throw std::runtime_error("Capture session belongs to device "+state.serial);
    auto journal=LoadJournal(state.Journal());
    bool running=Running(adb,state.package,true);
    if(!journal.value("restored",false)){
        try{SetAndroidTrigger(adb,state,false,true);}catch(...){} // a conflict here surfaces again from the restore below
        if(closeGame){adb.Stop(state.package);running=false;}
        if(PriorEnabled(journal)&&running){
            journal["requires_game_exit"]=true;AtomicJson(state.Journal(),journal);
            throw std::runtime_error("Prior runtime trigger was enabled. Close the launched game (capture end --close-game) before restoring those settings.");
        }
        RestoreAndroidJournal(adb,state.Journal());
    }else if(closeGame){adb.Stop(state.package);running=false;}
    state.ended=true;SaveAndroidSession(state);
    return {{"session",directory.u8string()},{"serial",state.serial},{"package",state.package},{"restored",true},{"captures",state.captures},{"game_running",running},{"game_closed",closeGame}};
}
struct LiveCaptureSession::Impl {
    LiveCaptureOptions options;
    std::atomic_bool abort{false};std::atomic_int command{0};
    AdbConnection adb;
    mutable std::mutex mutex;LiveCaptureState state;
    std::thread worker;std::unique_ptr<GameProcess> game;
    AndroidSessionState android;                       // Android: all session state lives in the shared stateless functions
    std::string activeFile,partial;size_t logOffset=0; // Windows layer-log cursor
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
    // New complete layer-log lines since the last call.
    std::vector<std::string> NewLines(){
        std::vector<std::string> lines;
        if(options.android){
            for(const auto& line:ReadAndroidLogLines(adb,android)){lines.push_back(line.text);AdvanceAndroidLog(android,line.end,false);}
            return lines;
        }
        const auto log=Text(options.capture.output/"layer.log");
        if(log.size()<logOffset)throw std::runtime_error("Capture log was replaced unexpectedly");partial+=log.substr(logOffset);logOffset=log.size();
        size_t newline;while((newline=partial.find('\n'))!=std::string::npos){lines.push_back(Trim(partial.substr(0,newline)));partial.erase(0,newline+1);}
        return lines;
    }
    void Trigger(bool enabled){
        if(options.android)SetAndroidTrigger(adb,android,enabled);else game->SetCaptureEnabled(enabled);
    }
    void Collect(){
        if(options.android){
            const auto file=CollectAndroidCapture(adb,android,activeFile);
            std::lock_guard<std::mutex> lock(mutex);state.captures.push_back(file.path);return;
        }
        if(activeFile.empty())throw std::runtime_error("Capture finished without a reported file");
        const auto number=Snapshot().captures.size()+1;const auto directory=options.capture.output/"captures";std::filesystem::create_directories(directory);
        const auto target=directory/("capture-"+std::to_string(number)+".gfxr");
        const auto source=std::filesystem::u8path(activeFile);
        if(std::filesystem::weakly_canonical(source.parent_path())!=std::filesystem::weakly_canonical(options.capture.output)||std::filesystem::is_symlink(source))throw std::runtime_error("Capture output escaped its session");
        auto pending=target;pending+=".partial";std::filesystem::copy_file(source,pending);
        if(Sha256File(source)!=Sha256File(pending))throw std::runtime_error("Capture changed during retrieval; wait between trigger presses");
        std::filesystem::rename(pending,target);
        if(std::filesystem::file_size(target)<=16)throw std::runtime_error("Capture output is empty");
        AtomicJson(directory/("capture-"+std::to_string(number)+".json"),Json{{"owner","gfxrecon_gui"},{"complete",true},{"file",target.filename().u8string()},{"sha256",Sha256File(target)},{"frames",options.frames},{"serial",adb.serial}});
        {std::lock_guard<std::mutex> lock(mutex);state.captures.push_back(target);}
    }
    void CloseGame(){if(options.android)adb.Stop(options.capture.package);else if(game)game->Stop();{std::lock_guard<std::mutex> lock(mutex);state.gameRunning=false;}}
    void Restore(){
        if(options.android&&prepared){
            const auto journalFile=android.Journal();
            if(Snapshot().gameRunning){
                auto journal=LoadJournal(journalFile);
                if(PriorEnabled(journal)){journal["requires_game_exit"]=true;AtomicJson(journalFile,journal);throw std::runtime_error("Prior runtime trigger was enabled. Close the launched game before restoring those settings.");}
            }
            RestoreAndroidJournal({adb.executable,adb.serial,nullptr,adb.transport},journalFile);
        }
    }
    void FinishSession(){if(closeOnEnd)CloseGame();Restore();Phase(LivePhase::Ended,"Capture session ended; "+std::string(Snapshot().gameRunning?"game continues running":"game is closed"));}
    void Run(){
        try{
            std::optional<DeviceLock> deviceLock;if(options.android)deviceLock.emplace(adb.serial);
            if(options.frames>1000000)throw std::runtime_error("Choose a capture length of 1 to 1,000,000 frames");
            std::filesystem::create_directories(options.capture.output);
            Phase(LivePhase::Starting,"Launching with capture tracking armed; no frames are recorded yet");
            if(options.android){
                AndroidArmOptions arm;arm.directory=options.capture.output;arm.package=options.capture.package;arm.arguments=options.capture.arguments;arm.frames=options.frames;
                PrepareAndroidCapture(adb,arm,android);prepared=android.prepared;launched=android.launched;
                WaitAndroidArmed(adb,android,std::chrono::seconds(120),[this]{
                    if(command.load()>=3){const int action=command.exchange(0);ending=true;closeOnEnd=action==4;return true;}return false;});
            }else{
                PrepareWindows();
                const auto readyDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
                while(!abort){
                    if(Text(options.capture.output/"layer.log").find("GfxReconGui capture control ready")!=std::string::npos)break;
                    if(command.load()>=3){const int action=command.exchange(0);ending=true;closeOnEnd=action==4;break;}
                    if(!Alive())throw std::runtime_error("Game exited before the capture layer became ready");if(std::chrono::steady_clock::now()>readyDeadline)throw std::runtime_error("Capture layer did not initialize; check Vulkan/layer compatibility");std::this_thread::sleep_for(std::chrono::milliseconds(100));}
                if(abort)throw std::runtime_error("Capture session cancelled while launching");
                if(Text(options.capture.output/"layer.log").find(kRecordingStart)!=std::string::npos){CloseGame();throw std::runtime_error("Conflicting layer settings started capture at launch. The launched app was closed; remove those settings before arming again.");}
            }
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
                const auto lines=NewLines();if(options.android&&!lines.empty())SaveAndroidSession(android);
                for(const auto& line:lines){
                    const auto at=line.find(kRecordingStart);
                    if(at!=std::string::npos){activeFile=line.substr(at+kRecordingStart.size());recording=true;lastRecord=std::chrono::steady_clock::now();if(stopDeadline!=std::chrono::steady_clock::time_point{})stopDeadline=lastRecord+std::chrono::seconds(30);Phase(LivePhase::Recording,"Recording "+std::string(options.frames?std::to_string(options.frames)+" frames":"until Stop capture"));}
                    if(line.find(kRecordingDone)!=std::string::npos&&recording){
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
            if(recording||options.android)try{if(options.android)SetAndroidTrigger(adb,android,false,true);else Trigger(false);}catch(...){}
            Restore();
        }catch(const std::exception& error){
            std::string detail=error.what();
            if(options.android){prepared=android.prepared;launched=android.launched;}
            if(game)game->SetCaptureEnabled(false);
            if(!abort&&!armed&&launched)try{CloseGame();}catch(...){}
            if(options.android&&prepared){try{SetAndroidTrigger(adb,android,false,true);Restore();}catch(const std::exception& restore){detail+="\nRecovery pending: "+std::string(restore.what());}}
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
