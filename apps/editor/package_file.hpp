#pragma once
#include <filesystem>
#include <system_error>

namespace darkangel::editor_app {
// A temporary package registry written for one cook; removed when the scope ends, whether or not the cook succeeded.
struct ComposerPackageFile {
    std::filesystem::path path;
    ~ComposerPackageFile() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};
} // namespace darkangel::editor_app
