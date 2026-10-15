#pragma once
#include <darkangel/check.hpp>
#include "../apps/editor/native_authoring.hpp"
#include <darkangel/character_scene.hpp>
#include "../apps/editor/reference_picker.hpp"
#include "../apps/editor/character_preview.hpp"
#include "../apps/editor/effect_preview.hpp"
#include <darkangel/editor_document.hpp>
#include <darkangel/character_presentation.hpp>
#include <darkangel/hash.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <sqlite3.h>
#include <Windows.h>
using namespace darkangel;using namespace darkangel::editor_app;using Json=nlohmann::json;
namespace native_authoring_test {inline void check(bool v,const char* m){darkangel::require(v,m);}template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"Expected checked authoring rejection");}template<class F>void rejects_with(F f,std::string_view text){try{f();}catch(const std::exception& e){check(std::string_view(e.what()).find(text)!=std::string_view::npos,"Expected specific checked rejection");return;}throw std::runtime_error("Expected diagnostic rejection");}inline std::string read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}}

using namespace native_authoring_test;

struct NativeAuthoringCommandFixture {
    std::filesystem::path root,source;
    std::unique_ptr<AssetService> service;
    AssetId kit,heavy,burn,human;
    std::vector<AssetId> roots;
    NativeAuthoringCommandFixture();
    AssetId id(const char* file)const;
};
