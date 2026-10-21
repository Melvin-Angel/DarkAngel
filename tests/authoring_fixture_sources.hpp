#pragma once
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
// Keep the real Royal authoring closure, without unrelated bulk libraries.
// These files otherwise get copied and inspected again by every transaction.
// A separate production-mount snapshot test covers the complete project size.
inline void copy_authoring_fixture_sources(const std::filesystem::path& content,const std::filesystem::path& destination){
 std::filesystem::create_directories(destination);
 for(std::filesystem::recursive_directory_iterator entry(content),end;entry!=end;++entry){
  const auto relative=entry->path().lexically_relative(content);const auto path=relative.generic_string();
  if(path=="royal_crown"||path=="animation/unity"){entry.disable_recursion_pending();continue;}
  const auto target=destination/relative;
  if(entry->is_directory()){std::filesystem::create_directories(target);continue;}
  if(!entry->is_regular_file())continue;
  const auto extension=entry->path().extension().string();
  if(extension.starts_with(".da")&&extension!=".daimport"&&extension!=".dascene"){
   std::ifstream input(entry->path(),std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(input),{}};
   if(bytes.find("animation/unity/")!=bytes.npos||bytes.find("royal_crown/")!=bytes.npos)throw std::runtime_error("Authoring fixture references an excluded library; extend the fixture closure");
  }
  std::filesystem::copy_file(entry->path(),target);
 }
}
