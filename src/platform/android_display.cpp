#include "platform/android_display.h"
#include "core/artifacts.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cctype>

namespace gfx {
namespace {
using Json=nlohmann::json;
std::string Trim(std::string text){const auto a=text.find_first_not_of(" \t\r");if(a==std::string::npos)return {};const auto b=text.find_last_not_of(" \t\r");return text.substr(a,b-a+1);}
Json Read(const std::filesystem::path& path){std::ifstream file(path);if(!file)throw std::runtime_error("Cannot read display recovery journal");return Json::parse(file);}
bool PanelOff(const std::string& dump,const std::string& physical){
    std::istringstream lines(dump);std::string line;bool device=false;
    while(std::getline(lines,line)){line=Trim(line);
        if(line.rfind("mPhysicalDisplayId=",0)==0)device=line.substr(19)==physical;
        if(device&&line=="mCommittedState=OFF")return true;
        if(device&&line=="mCommittedState=ON")return false;
    }
    throw std::runtime_error("Cannot verify the primary physical display power state. Disable screen-off replay for this device.");
}
}
AndroidPanelState ReadAndroidPanelState(const AdbConnection& adb,bool cleanup){
    const auto displays=adb.Shell({"cmd","display","get-displays"},cleanup);std::istringstream lines(displays);std::string line,id;
    while(std::getline(lines,line))if(line.rfind("Display id 0:",0)==0){
        const std::string prefix="uniqueId \"local:";const auto at=line.find(prefix);if(at==std::string::npos)break;
        const auto start=at+prefix.size(),end=line.find('"',start);if(end!=std::string::npos)id=line.substr(start,end-start);break;
    }
    if(id.empty()||id.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Cannot identify the primary physical display. Disable screen-off replay for this device.");
    return {id,PanelOff(adb.Shell({"dumpsys","display"},cleanup),id)};
}
void RestoreAndroidReplayDisplay(const AdbConnection& adb,const std::filesystem::path& path){
    auto state=Read(path);
    if(state.value("owner","")!="gfxrecon_gui"||state.value("version",0)!=1||state.value("serial","")!=adb.serial||state.value("display",-1)!=0||state.value("restore_command","")!="power-reset")
        throw std::runtime_error("Display recovery journal does not belong to this device.");
    if(state.value("restored",false))return;
    const auto panel=ReadAndroidPanelState(adb,true);
    if(panel.physicalId!=state.at("physical_id"))throw std::runtime_error("The primary display changed; display restoration requires manual review.");
    // A physical POWER press wins over this operation. Existing OFF states are
    // retained, and power-reset returns an owned change to the OS-managed state.
    if(panel.off&&!state.value("initial_off",false))adb.Shell({"cmd","display",state.at("restore_command").get<std::string>(),"0"},true);
    const auto restored=ReadAndroidPanelState(adb,true);
    if(restored.off&&!state.value("initial_off",false)){
        const auto policy=adb.Shell({"dumpsys","window","policy"},true);
        if(policy.find("screenState=SCREEN_STATE_OFF")==std::string::npos)
            throw std::runtime_error("The phone display did not return to its OS-managed state. Reconnect and restore the replay display from Tools and devices.");
    }
    state["restored"]=true;state["restored_panel_off"]=restored.off;
    state["phase"]="restored";AtomicJson(path,state);
}
struct AndroidReplayDisplay::Impl {
    AdbConnection adb;std::filesystem::path path;Json state;bool active=false;unsigned observations=0;
    Impl(const AdbConnection& connection,const std::filesystem::path& journal):adb(connection),path(journal){}
};
AndroidReplayDisplay::AndroidReplayDisplay(const AdbConnection& adb,const std::filesystem::path& path):impl_(std::make_unique<Impl>(adb,path)){}
AndroidReplayDisplay::~AndroidReplayDisplay(){if(impl_->active)try{Restore();}catch(const std::exception& e){std::fprintf(stderr,"[display-recovery] %s; journal: %s\n",e.what(),impl_->path.u8string().c_str());}}
void AndroidReplayDisplay::TurnOff(){
    auto& m=*impl_;
    if(m.active)return;
    const auto response=m.adb.TryShell({"cmd","display","help"});
    if(response.cancelled)throw std::runtime_error("Replay cancelled.");
    if(response.timedOut)throw std::runtime_error("Display capability check timed out.");
    const auto& help=response.output;
    // Use the capability advertised by the device, not an assumed Android version.
    if(help.find("power-off")==std::string::npos||help.find("power-reset")==std::string::npos)
        throw std::runtime_error("This phone lacks supported display-power controls. Disable screen-off replay, or use scrcpy -Sw externally.");
    const auto initial=ReadAndroidPanelState(m.adb);
    m.state={{"owner","gfxrecon_gui"},{"version",1},{"serial",m.adb.serial},{"display",0},{"physical_id",initial.physicalId},
        {"initial_off",initial.off},{"restore_command","power-reset"},{"restored",false},{"phase","prepared"},{"off_observations",0}};
    AtomicJson(m.path,m.state);m.active=true;
    m.adb.Shell({"cmd","display","power-off","0"});
    const auto panel=ReadAndroidPanelState(m.adb);
    if(!panel.off)throw std::runtime_error("The phone did not enter physical screen-off mode.");
    ++m.observations;m.state["off_observations"]=m.observations;m.state["phase"]="active";AtomicJson(m.path,m.state);
}
void AndroidReplayDisplay::Observe(){
    auto& m=*impl_;if(!m.active)return;
    if(PanelOff(m.adb.Shell({"dumpsys","display"}),m.state.at("physical_id"))){++m.observations;m.state["off_observations"]=m.observations;}
    else m.state["screen_turned_on"]=true;
    // Observe without fighting a user turning the display back on.
    AtomicJson(m.path,m.state);
}
void AndroidReplayDisplay::Restore(){auto& m=*impl_;if(!m.active)return;m.active=false;RestoreAndroidReplayDisplay(m.adb,m.path);}
unsigned AndroidReplayDisplay::OffObservations()const{return impl_->observations;}
}
