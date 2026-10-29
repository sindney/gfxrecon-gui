#include "capture/live_capture.h"
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
