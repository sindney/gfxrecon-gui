#include "capture/live_capture.h"
#include "core/artifacts.h"
#include <map>
#include <thread>
#include <fstream>
#include <iostream>

namespace gfx::tests {
namespace {
void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void Await(LiveCaptureSession& session,F test){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(std::chrono::steady_clock::now()<end){const auto state=session.State();if(state.phase==LivePhase::Failed)throw std::runtime_error(state.detail);if(test(state))return;std::this_thread::sleep_for(std::chrono::milliseconds(50));}
    throw std::runtime_error("Live capture timed out: "+session.State().detail);
}
std::vector<std::string> Words(const std::string& text){
    std::vector<std::string> words;std::string word;char quote=0;bool active=false;
    for(char c:text){
        if(quote){if(c==quote)quote=0;else word+=c;}
        else if(c=='\''||c=='"'){quote=c;active=true;}
        else if(c==' '){if(active){words.push_back(word);word.clear();active=false;}}
        else{word+=c;active=true;}
    }
    if(active)words.push_back(word);return words;
}
// Phone whose layer reacts to capture_android_trigger: Recording line on enable, Finished a couple of log reads later.
struct FakePhone {
    std::map<std::string,std::string> settings{{"gpu_debug_app","prior.app"}},props{{"debug.gfxrecon.capture_frames","7-9"}};
    std::string relative,log;
    bool locked=false,started=false,running=false,corruptHash=false,neverStarts=false;
    int pendingFinish=0,captures=0,triggerSets=0,inputs=0;
    std::map<std::string,std::string> files;
    ProcessResult Run(const ProcessRequest& request,const std::atomic_bool*){
        Require(request.arguments.size()>=4&&request.arguments[1]=="phone-c","Fake phone: wrong device");
        const auto& type=request.arguments[2];const auto words=Words(request.arguments[3]);
        if(type=="exec-out"){std::ofstream out(request.stdoutFile,std::ios::binary);out<<files.at(words.back());return {0};}
        Require(type=="shell"&&!words.empty(),"Fake phone: unexpected transport");
        if(words[0]=="dumpsys")return {0,std::string("KeyguardServiceDelegate\n showing=")+(locked?"true":"false")+"\n secure=true"};
        if(words[0]=="input"||words[0]=="wm"){++inputs;return {0};}
        if(words[0]=="pm")return {0,"package:/data/app/replay.apk"};
        if(words[0]=="pidof")return running?ProcessResult{0,"4242"}:ProcessResult{1,""};
        if(words[0]=="cmd")return {0,"test.app/Activity\n"};
        if(words[0]=="settings"){
            if(words[1]=="get")return {0,(settings.count(words[3])?settings.at(words[3]):"null")+"\n"};
            if(words[1]=="put")settings[words[3]]=words[4];else settings.erase(words[3]);return {0};
        }
        if(words[0]=="getprop")return {0,props[words[1]]+"\n"};
        if(words[0]=="setprop"){
            props[words[1]]=words[2];
            if(words[1]=="debug.gfxrecon.capture_android_trigger"){
                ++triggerSets;
                if(words[2]=="true"){
                    const auto name="/data/data/test.app/"+relative+"/c_trigger_"+std::to_string(++captures)+".gfxr";
                    files[name]=std::string(4096+captures,'x')+std::to_string(captures);
                    log+="Recording graphics API capture to "+name+"\n";pendingFinish=2;
                }
            }
            return {0};
        }
        if(words[0]=="am"){
            if(words[1]=="force-stop"){running=false;return {0};}
            started=running=true;if(!neverStarts)log="layer initialized\n";return {0,"Success"};
        }
        if(words[0]=="run-as"){
            if(words[2]=="test")return {1};
            if(words[2]=="mkdir"){relative=words[4];return {0};}
            if(words[2]=="cat"){
                if(!started||neverStarts)return {1,"cat: No such file or directory"};
                if(pendingFinish>0&&--pendingFinish==0)log+="Finished recording graphics API capture\n";
                return {0,log};
            }
            if(words[2]=="stat")return {0,std::to_string(files.at(words.back()).size())+"\n"};
            if(words[2]=="sha256sum")return {0,(corruptHash?std::string(64,'0'):Sha256Text(files.at(words.back())))+"  "+words.back()+"\n"};
            return {0};
        }
        throw std::runtime_error("Fake phone: unexpected command "+request.arguments[3]);
    }
};
Json Load(const std::filesystem::path& file){std::ifstream in(file);return Json::parse(in);}
AdbConnection Connect(FakePhone& phone){return AdbConnection{"fake-adb","phone-c",nullptr,[&phone](const ProcessRequest& r,const std::atomic_bool* c){return phone.Run(r,c);}};}
}
void RunAndroidCaptureTests(const std::filesystem::path& root){
    using namespace std::chrono_literals;
    const auto base=root/"android-capture";std::filesystem::remove_all(base);std::filesystem::create_directories(base);
    // Stateless arm -> trigger -> trigger -> status -> end; every call takes a fresh connection and reads only the session directory.
    {
        FakePhone phone;const auto original=phone.settings,originalProps=phone.props;const auto dir=base/"flow";
        AndroidArmOptions arm;arm.directory=dir;arm.package="test.app";arm.frames=2;arm.timeout=5s;
        const auto armed=ArmAndroidCapture(Connect(phone),arm);
        Require(armed.launched&&phone.running&&phone.props["debug.gfxrecon.capture_android_trigger"]=="false","Arm did not launch with the trigger off");
        Require(phone.props["debug.gfxrecon.capture_trigger_frames"]=="2"&&phone.settings["gpu_debug_app"]=="test.app","Arm did not apply journaled settings");
        Require(phone.captures==0,"Arming recorded frames");const int baseline=phone.triggerSets;
        const auto journal=Load(dir/"device-journal.json");Require(journal["schema"]==2&&journal["live_session"]==true&&!journal["restored"].get<bool>()&&journal["changes"].size()==15,"Journal schema changed");
        const auto session=Load(dir/"session.json");Require(session["captures"]==0&&session["frames"]==2&&session["package"]=="test.app","session.json missing state");
        const auto status=AndroidCaptureStatus(Connect(phone),dir);
        Require(status["layer_ready"]==true&&status["game_running"]==true&&status["recording"]==false&&status["locked"]==false,"Status mismatch");
        const auto first=TriggerAndroidCapture(Connect(phone),dir,2,30s);Require(first.size()==1,"Trigger produced nothing");
        const auto second=TriggerAndroidCapture(Connect(phone),dir,0,30s);
        Require(second.size()==1&&phone.running,"Second trigger failed or closed the game");
        Require(first[0].path==dir/"captures"/"capture-1.gfxr"&&second[0].path==dir/"captures"/"capture-2.gfxr","Captures were not numbered across calls");
        Require(first[0].sha256!=second[0].sha256&&second[0].sha256==Sha256File(second[0].path)&&second[0].bytes==std::filesystem::file_size(second[0].path),"Capture hash/size mismatch");
        Require(Load(dir/"captures"/"capture-2.json")["sha256"]==second[0].sha256,"Capture sidecar missing hash");
        Require(phone.props["debug.gfxrecon.capture_android_trigger"]=="false"&&phone.triggerSets==baseline+4,"Trigger was not reset after each capture");
        bool mismatch=false;try{TriggerAndroidCapture(Connect(phone),dir,5,30s);}catch(const std::runtime_error&){mismatch=true;}
        Require(mismatch&&phone.captures==2,"Frame count other than the armed one was accepted");
        const auto ended=EndAndroidCapture(Connect(phone),dir,false);
        Require(ended["restored"]==true&&ended["game_running"]==true&&phone.running,"End closed the game without --close-game");
        Require(phone.settings==original,"Settings were not restored");
        for(const auto& [name,value]:phone.props){const auto at=originalProps.find(name);Require(value==(at==originalProps.end()?"":at->second),"Property not restored");}
        Require(Load(dir/"device-journal.json")["restored"]==true&&Load(dir/"session.json")["ended"]==true,"Journal/session not marked ended");
        bool refused=false;try{TriggerAndroidCapture(Connect(phone),dir,0,5s);}catch(const std::runtime_error&){refused=true;}
        Require(refused,"Trigger on an ended session was accepted");
        EndAndroidCapture(Connect(phone),dir,true);Require(!phone.running,"--close-game did not stop the game");
    }
    // Locked phone: unlock_device before the trigger property is touched; the session stays armed and works once unlocked.
    {
        FakePhone phone;const auto dir=base/"locked";
        AndroidArmOptions arm;arm.directory=dir;arm.package="test.app";arm.frames=2;arm.timeout=5s;ArmAndroidCapture(Connect(phone),arm);
        phone.locked=true;bool unlock=false;const int lockedBaseline=phone.triggerSets;
        try{TriggerAndroidCapture(Connect(phone),dir,0,5s);}catch(const AndroidUnlockRequired& e){unlock=e.state.serial=="phone-c";}
        Require(unlock&&phone.triggerSets==lockedBaseline&&phone.inputs==0,"Locked trigger touched the device or lost the serial");
        Require(!Load(dir/"device-journal.json")["restored"].get<bool>()&&phone.running,"Locked trigger ended the session");
        phone.locked=false;Require(TriggerAndroidCapture(Connect(phone),dir,0,30s).size()==1,"Armed session did not work after unlock");
        bool armLocked=false;FakePhone other;other.locked=true;AndroidArmOptions again;again.directory=base/"locked-arm";again.package="test.app";
        try{ArmAndroidCapture(Connect(other),again);}catch(const AndroidUnlockRequired&){armLocked=true;}
        Require(armLocked&&other.settings.size()==1&&other.settings["gpu_debug_app"]=="prior.app"&&!other.running&&other.inputs==0,"Arm on a locked phone changed settings or launched");
        EndAndroidCapture(Connect(phone),dir,true);
    }
    // Hash mismatch discards the transfer; a failed arm restores everything.
    {
        FakePhone phone;const auto dir=base/"hash";
        AndroidArmOptions arm;arm.directory=dir;arm.package="test.app";arm.frames=2;arm.timeout=5s;ArmAndroidCapture(Connect(phone),arm);
        phone.corruptHash=true;bool failed=false;
        try{TriggerAndroidCapture(Connect(phone),dir,0,30s);}catch(const std::runtime_error& e){failed=std::string(e.what()).find("hash")!=std::string::npos;}
        Require(failed&&!std::filesystem::exists(dir/"captures"/"capture-1.gfxr")&&phone.props["debug.gfxrecon.capture_android_trigger"]=="false","Corrupt transfer was kept or trigger left enabled");
        EndAndroidCapture(Connect(phone),dir,true);
        FakePhone silent;silent.neverStarts=true;const auto original=silent.settings;AndroidArmOptions never;never.directory=base/"never";never.package="test.app";never.timeout=1s;
        bool timedOut=false;try{ArmAndroidCapture(Connect(silent),never);}catch(const std::runtime_error&){timedOut=true;}
        Require(timedOut&&silent.settings==original&&!silent.running&&Load(base/"never"/"device-journal.json")["restored"]==true,"Failed arm left the device modified");
    }
    // Restore conflicts are reported structurally; recovery works from the same journal once resolved.
    {
        FakePhone phone;const auto original=phone.settings;const auto dir=base/"conflict";
        AndroidArmOptions arm;arm.directory=dir;arm.package="test.app";arm.frames=2;arm.timeout=5s;ArmAndroidCapture(Connect(phone),arm);
        phone.settings["gpu_debug_app"]="outside.app";bool conflict=false;
        try{EndAndroidCapture(Connect(phone),dir,false);}catch(const AndroidRestoreConflict& e){
            conflict=e.conflicts.size()==1&&e.conflicts[0]["name"]=="gpu_debug_app"&&e.conflicts[0]["current"]=="outside.app";if(!conflict)std::cerr<<e.conflicts.dump()<<std::endl;}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;}
        Require(conflict&&phone.settings["gpu_debug_app"]=="outside.app"&&!Load(dir/"device-journal.json")["restored"].get<bool>(),"Conflict not reported or overwritten");
        phone.settings["gpu_debug_app"]="test.app";RestoreAndroidJournal(Connect(phone),dir/"device-journal.json");
        Require(phone.settings==original&&Load(dir/"device-journal.json")["restored"]==true,"Recovery from the same journal failed");
    }
    // The GUI session runs the same functions and keeps its behaviour.
    {
        FakePhone phone;const auto original=phone.settings;
        LiveCaptureOptions options;options.android=true;options.frames=2;options.capture.package="test.app";options.capture.output=base/"gui";
        LiveCaptureSession session(options,Connect(phone));
        Await(session,[](const auto& s){return s.phase==LivePhase::Armed;});
        for(unsigned repetition=0;repetition<2;++repetition){
            session.Capture();Await(session,[&](const auto& s){return s.phase==LivePhase::Armed&&s.captures.size()==repetition+1;});
        }
        Require(session.State().gameRunning&&Load(base/"gui"/"session.json")["captures"]==2,"GUI session did not keep the game running or persist state");
        session.End(false);Await(session,[](const auto& s){return s.phase==LivePhase::Ended;});
        Require(phone.settings==original&&phone.running&&Load(base/"gui"/"device-journal.json")["restored"]==true,"GUI End did not restore settings");
        const auto files=session.State().captures;Require(files.size()==2&&files[0]!=files[1]&&std::filesystem::file_size(files[0])>4096,"GUI captures missing");
    }
    std::filesystem::remove_all(base);
}
void RunLiveCaptureTests(const std::string& game,const std::string& layer,const std::string& cwd,const std::string& convert,const std::filesystem::path& root){
    std::filesystem::create_directories(root);Json report=Json::array();
    for(const auto frames:{3u,0u}){
        LiveCaptureOptions options;options.process.executable=game;options.process.workingDirectory=cwd;options.layerDirectory=layer;options.frames=frames;
        options.capture.output=root/(frames?"bounded":"manual");LiveCaptureSession session(options);
        try{
            Await(session,[](const auto& s){return s.phase==LivePhase::Armed;});
            std::this_thread::sleep_for(std::chrono::seconds(1));
            for(const auto& file:std::filesystem::directory_iterator(options.capture.output))Require(file.path().extension()!=".gfxr","Arming recorded frames before trigger");
            for(unsigned repetition=0;repetition<(frames?2u:1u);++repetition){
                session.Capture();
                if(!frames){Await(session,[](const auto& s){return s.phase==LivePhase::Recording;});std::this_thread::sleep_for(std::chrono::milliseconds(200));session.StopCapture();}
                Await(session,[&](const auto& s){return s.phase==LivePhase::Armed&&s.captures.size()==repetition+1;});
                Require(session.State().gameRunning,"Finishing capture quit the game");
            }
            const auto captures=session.State().captures;
            session.End(false);Await(session,[](const auto& s){return s.phase==LivePhase::Ended;});Require(session.State().gameRunning,"Ending monitoring quit the game");
            session.End(true);Await(session,[](const auto& s){return !s.gameRunning;});
            for(const auto& file:captures){
                auto jsonl=file;jsonl+=".jsonl";const auto result=RunProcess({convert,{"--format","jsonl","--output",jsonl.u8string(),file.u8string()}});
                Require(result.Success(),"On-demand capture could not be converted");const auto capture=ReadCapture(jsonl);
                Require(frames?capture->frames==frames:capture->frames>0,"Captured frame count did not match the request");
                Require(capture->header.at("inspector_application_frames").at("1").get<uint64_t>()>3,"Capture was not delayed until gameplay");
                report.push_back({{"file",file.u8string()},{"frames",capture->frames},{"first_game_frame",capture->header.at("inspector_application_frames").at("1")},{"game_kept_running",true}});
            }
        }catch(...){session.End(true);std::this_thread::sleep_for(std::chrono::seconds(1));throw;}
    }
    std::ofstream output(root/"result.json");output<<report.dump(2);std::cout<<report.dump(2)<<'\n';
}
}
