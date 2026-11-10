#include "util/logging.h"
#include <fstream>
#include <iterator>
#include <iostream>

// Opt-in backend regression: Vulkan capability probing can initialize the
// logger more than once before the real renderer starts using the same file.
int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"Usage: gfxrecon-capture-logging-test NEW_LOG_FILE\n";return 2;}
    auto& manager=gfxrecon::util::logging::LoggingManager::GetSingleton();
    manager.UpdateFileTarget(true,argv[1],true,true,true);
    manager.LogMessage(gfxrecon::util::LoggingSeverity::kInfo,"probe instance");
    manager.UpdateFileTarget(false,argv[1],true,true,true);
    manager.UpdateFileTarget(true,argv[1],true,true,true);
    manager.LogMessage(gfxrecon::util::LoggingSeverity::kInfo,"renderer instance");
    std::ifstream input(argv[1]);const std::string text(std::istreambuf_iterator<char>(input),{});
    const bool valid=text.find("probe instance")!=std::string::npos&&text.find("renderer instance")!=std::string::npos;
    std::cout<<"Repeated capture logger initialization: "<<(valid?"passed":"failed")<<'\n';
    return valid?0:1;
}
