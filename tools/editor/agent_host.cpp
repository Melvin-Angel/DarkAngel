#include <darkangel/editor_rpc.hpp>
#include <darkangel/editor_document.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
namespace {std::atomic<bool> running{true};void stop(int){running=false;}}
int main(int argc,char** argv){try{
    if(argc<4 || argc>7)throw std::runtime_error("AgentHost DOCUMENT PROJECT_UUID NEW_DESCRIPTOR [--authoring] [--sources SOURCE_ROOT]");bool edits=false;std::optional<std::filesystem::path> sources;
    for(int index=4;index<argc;++index){const std::string_view option=argv[index];if(option=="--authoring")edits=true;else if(option=="--sources"&&index+1<argc)sources=argv[++index];else throw std::runtime_error("Unknown host option");}
    auto project=darkangel::StableId::parse(argv[2]);if(!project)throw std::runtime_error("Zero project UUID");
    // Linked prefabs (R4) resolve from the project's sources: the given mount, or the one the document lives in.
    if(!sources)sources=darkangel::EditorDocument::find_source_root(argv[1]);
    auto doc=darkangel::EditorDocument::open(argv[1],sources?darkangel::EditorDocument::project_links(*sources):darkangel::EditorDocument::LinkSource{});darkangel::AgentEndpoint endpoint(*doc,project.text(),edits,argv[3]);std::signal(SIGINT,stop);std::signal(SIGTERM,stop);std::cerr<<"AgentHost ready ("<<(edits?"authoring":"inspection")<<"); explicit descriptor issued.\n";while(running){endpoint.poll();std::this_thread::sleep_for(std::chrono::milliseconds(2));}return 0;
}catch(const std::exception& e){std::cerr<<"AgentHost: "<<e.what()<<'\n';return 1;}}
