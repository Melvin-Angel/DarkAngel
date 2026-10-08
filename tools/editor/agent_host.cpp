#include <darkangel/editor_rpc.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
namespace {std::atomic<bool> running{true};void stop(int){running=false;}}
int main(int argc,char** argv){try{
    if(argc<4 || argc>5)throw std::runtime_error("AgentHost DOCUMENT PROJECT_UUID NEW_DESCRIPTOR [--authoring]");bool edits=argc==5 && std::string_view(argv[4])=="--authoring";if(argc==5 && !edits)throw std::runtime_error("Unknown host option");auto project=darkangel::StableId::parse(argv[2]);if(!project)throw std::runtime_error("Zero project UUID");auto doc=darkangel::EditorDocument::open(argv[1]);darkangel::AgentEndpoint endpoint(*doc,project.text(),edits,argv[3]);std::signal(SIGINT,stop);std::signal(SIGTERM,stop);std::cerr<<"AgentHost ready ("<<(edits?"authoring":"inspection")<<"); explicit descriptor issued.\n";while(running){endpoint.poll();std::this_thread::sleep_for(std::chrono::milliseconds(2));}return 0;
}catch(const std::exception& e){std::cerr<<"AgentHost: "<<e.what()<<'\n';return 1;}}
