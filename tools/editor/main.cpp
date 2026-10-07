#include <darkangel/editor_document.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv){try{
    if(argc<3)throw std::runtime_error("EditorCli validate DOCUMENT | resolve DOCUMENT OUTPUT | set DOCUMENT OBJECT TYPE PROPERTY VALUE [definition]");
    auto doc=darkangel::EditorDocument::open(argv[2]);std::string mode=argv[1];
    if(mode=="validate"){doc->scripts();std::cout<<"Valid document; objects="<<doc->plan().origins.size()<<" fingerprint="<<doc->plan().fingerprint<<'\n';}
    else if(mode=="resolve"){if(argc!=4)throw std::runtime_error("resolve requires output");auto scripts=doc->scripts();darkangel::CookedScene scene{doc->plan(),scripts};std::ofstream out(argv[3],std::ios::binary);out<<scene.serialize();if(!out.good())throw std::runtime_error("Cooked scene output failed");std::cout<<"Cooked spawn plan/script bundle; required scripts="<<scripts.size()<<'\n';}
    else if(mode=="set"){if(argc!=7 && argc!=8)throw std::runtime_error("set arguments");darkangel::PropertyChange change{darkangel::StableId::parse(argv[3]),static_cast<darkangel::TypeId>(std::stoul(argv[4])),static_cast<darkangel::PropertyId>(std::stoul(argv[5])),std::stod(argv[6])};auto edit=doc->prepare(doc->revision(),{&change,1},argc==8 && std::string_view(argv[7])=="definition"?darkangel::EditScope::Definition:darkangel::EditScope::Placement);doc->commit(edit);doc->save(argv[2]);std::cout<<edit.summary<<'\n';}
    else throw std::runtime_error("Unknown editor command");return 0;
}catch(const std::exception& error){std::cerr<<"EditorCli: "<<error.what()<<'\n';return 1;}}
