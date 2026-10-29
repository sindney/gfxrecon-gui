#include "replay/replay.h"
#include "core/artifacts.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace gfx::tests {
void ProfileReplay(const std::filesystem::path& source, const std::filesystem::path& jsonl,
                   const std::string& serial, const std::filesystem::path& root, const std::filesystem::path& toolFile, bool optimize) {
    std::ifstream input(toolFile); const auto tools = Json::parse(input).get<std::map<std::string,std::string>>();
    const auto capture = ReadCapture(jsonl); Json report = Json::array();
    std::filesystem::create_directories(root);
    auto run = [&](const char* label, const Json& dump, bool reuse, uint32_t frame = 1,
                   const std::map<uint64_t,std::filesystem::path>& overrides = {}) {
        ReplayRequest request; request.capture=source; request.output=root/label; request.cacheDirectory=root/"cache";
        request.optimize=optimize;
        request.timeout=std::chrono::seconds(300); request.dump=dump; request.outputsOnly=!dump.is_null(); request.reuseArtifacts=reuse;
        request.frame=frame;
        request.overrides=overrides;
        const auto completed=request.output/"manifest.json";
        if(std::filesystem::is_regular_file(completed)) {
            std::ifstream previous(completed);auto data=Json::parse(previous);
            const auto backend=tools.at(serial=="local"?"replay":"replay_apk");
            if(!data.value("complete",false)||data.value("source_capture_sha256",data.at("capture_sha256"))!=Sha256File(source)||data.at("backend_sha256")!=Sha256File(std::filesystem::u8path(backend)))
                throw std::runtime_error("Cannot resume a profile with different capture/backend bytes");
            for(const auto& artifact:data.at("artifacts"))
                if(Sha256File(request.output/std::filesystem::u8path(artifact.at("file").get<std::string>()))!=artifact.at("sha256"))
                    throw std::runtime_error("Cannot resume a profile with damaged completed artifacts");
            data["label"]=label;data["image_sha256"]=Sha256File(request.output/data.at("image").get<std::string>());
            report.push_back(data);AtomicJson(root/"result.json",report);
            std::cout<<label<<": resumed verified checkpoint, "<<data["elapsed_seconds"]<<" seconds\n"<<std::flush;
            return data;
        }
        if(std::filesystem::exists(request.output))request.output+=std::string("-retry-")+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto result=ReplayCapture(request, {serial!="local",serial=="local"?"":serial},tools);
        if(serial!="local" && std::filesystem::exists(request.output/"launch-arguments.json")) {
            std::ifstream arguments(request.output/"launch-arguments.json");
            if(Json::parse(arguments).at("args").get<std::string>().find("  ")!=std::string::npos)
                throw std::runtime_error("Android SDK command contains an empty positional argument");
        }
        std::ifstream manifest(request.output/"manifest.json"); auto data=Json::parse(manifest);
        data["label"]=label;data["image_sha256"]=result.image.empty()?"":Sha256File(result.image);
        if(result.image.empty())throw std::runtime_error("Selected draw produced no color output; inspect metadata before enabling its path");
        report.push_back(data);AtomicJson(root/"result.json",report);
        std::cout<<label<<": "<<data["elapsed_seconds"]<<" seconds, cached="<<data["artifact_cache_hit"]<<'\n'<<std::flush;
        return data;
    };
    const auto cold=run("frame-cold",Json(),false), warm=run("frame-warm",Json(),false), cached=run("frame-cached",Json(),true);
    if(cold["image_sha256"]!=warm["image_sha256"]||cold["image_sha256"]!=cached["image_sha256"]||!cached["artifact_cache_hit"].get<bool>())
        throw std::runtime_error("Frame cache changed rendered output or failed to reuse it");
    size_t index=0;
    for(size_t i=0;i<capture->executions.size();++i)if(capture->events[capture->executions[i].event].block==47145){index=i;break;}
    const auto first=run("draw-a",capture->DumpRequest(index),true,capture->executions[index].frame);
    const auto again=run("draw-a-cached",capture->DumpRequest(index),true,capture->executions[index].frame);
    if(first["image_sha256"]!=again["image_sha256"]||!again["artifact_cache_hit"].get<bool>())throw std::runtime_error("Draw cache did not preserve selected output");
    if(capture->executions.size()>index+1)run("draw-b",capture->DumpRequest(index+1),true,capture->executions[index+1].frame);
    for(size_t i=capture->executions.size();i>0;--i) {
        const auto& execution=capture->executions[i-1];
        const auto& event=capture->events[execution.event];
        if(execution.frame==1&&(event.name=="vkCmdDraw"||event.name=="vkCmdDrawIndexed")) {
            if(i-1!=index) {
                const auto visible=run("draw-visible",capture->DumpRequest(i-1),true,1);
                if(optimize) {
                    const auto stages=capture->pipelines.find(event.pipeline);
                    if(stages!=capture->pipelines.end())for(const auto& stage:stages->second)if(stage.stage.find("FRAGMENT")!=std::string::npos) {
                        const auto binary=jsonl.parent_path()/"shaders"/("sh"+std::to_string(stage.module));
                        if(std::filesystem::is_regular_file(binary)) {
                            const auto replaced=run("draw-visible-override",capture->DumpRequest(i-1),false,1,{{stage.module,binary}});
                            if(replaced["image_sha256"]!=visible["image_sha256"])throw std::runtime_error("Unchanged original shader replacement changed optimized output");
                        }
                        break;
                    }
                }
            }
            break;
        }
    }
    // Damage a cached result and ensure it is rejected and regenerated.
    const auto key=Sha256Text(first.at("request_identity").dump());
    if(!std::filesystem::is_regular_file(root/"draw-a-repaired/manifest.json")) {
        std::ofstream damage(root/"cache/results"/key/first.at("image").get<std::string>(),std::ios::binary|std::ios::app);damage<<'x';
    }
    const auto repaired=run("draw-a-repaired",capture->DumpRequest(index),true,capture->executions[index].frame);
    if(repaired["artifact_cache_hit"].get<bool>()||repaired["image_sha256"]!=first["image_sha256"])throw std::runtime_error("Corrupt cached output was accepted or repair changed the image");
}
}
