#define NOMINMAX
#define UNICODE
#include <Windows.h>
#include <shellapi.h>
#include "vfx_workspace.hpp"
#include "editor_ui.hpp"
#include "native_authoring.hpp"
#include "package_file.hpp"
#include <darkangel/check.hpp>
#include <darkangel/cue_assets.hpp>
#include <darkangel/vfx_assets.hpp>
#include <darkangel/primitive_assets.hpp>
#include <imgui.h>
#include <array>

namespace darkangel::editor_app {
VfxWorkspace::VfxWorkspace(Controller& editor) : editor_(editor) {}
void VfxWorkspace::start(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, int color_format, int depth_format) {
    try {
        service = std::make_unique<VfxService>(device, context, color_format, depth_format);
    } catch (const std::exception& error) {
        failed = error.what();
        editor_.log(std::string("Visual effects unavailable: ") + error.what(), ConsoleSeverity::Warning);
    }
}
std::vector<AssetInfo> VfxWorkspace::effects() const {
    const auto& editor = editor_;
    std::vector<darkangel::AssetInfo> found;
    for (const auto& asset : editor.asset_inventory) {
        const auto extension = std::filesystem::path(asset.path).extension();
        if (extension == ".efkproj" || extension == ".efkefc")
            found.push_back(asset);
    }
    return found;
}
void VfxWorkspace::cook() {
    auto& editor = editor_;
    auto& vfx = service;
    auto& vfx_preview = state;
    auto& preview = vfx_preview;
    preview.cooked = false;
    preview.error.clear();
    if (!vfx || !editor.assets || preview.selected == AssetId{})
        return;
    if (preview.handle) {
        vfx->kill(preview.handle);
        preview.handle = 0;
    }
    try {
        std::error_code ignored;
        preview.seen = std::filesystem::last_write_time(editor.assets->source_root() / preview.path, ignored);
        const auto registry = editor.assets->cas_path().parent_path() / ("vfx-preview-" + AssetId::random().text() + ".json");
        ComposerPackageFile cleanup{registry};
        const std::array roots{preview.selected};
        darkangel::editor_app::package_authoring(*editor.assets, roots, registry);
        const auto cooked = darkangel::load_cooked_vfx(registry, editor.assets->cas_path(), preview.selected);
        vfx->load(cooked);
        preview.generation = cooked.generation;
        preview.textures = cooked.textures.size();
        preview.cooked = true;
        ++preview.cooks;
        preview.idle = 0;
    } catch (const std::exception& error) {
        preview.error = error.what();
    }
}
void VfxWorkspace::select(const AssetInfo& asset) {
    auto& vfx_preview = state;
    const auto vfx_cook = [this] { cook(); };
    vfx_preview.selected = asset.id;
    vfx_preview.path = asset.path;
    vfx_cook();
}
void VfxWorkspace::draw_cues() {
    auto& editor = editor_;
    auto& vfx_preview = state;
    const auto vfx_effects = [this] { return effects(); };
    const auto vfx_select = [this](const darkangel::AssetInfo& asset) { select(asset); };
    auto& preview = vfx_preview;
    const auto red = [](const std::string& text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, .45f, .35f, 1.f));
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::PopStyleColor();
    };
    ImGui::SeparatorText("Cues");
    ImGui::TextWrapped("A cue is what gameplay asks for by key: an effect, a sound or both, played on the actor.");
    std::vector<darkangel::AssetInfo> cues, sounds, trail_assets, decal_assets;
    for (const auto& asset : editor.asset_inventory) {
        const auto extension = std::filesystem::path(asset.path).extension();
        if (extension == ".dacue")
            cues.push_back(asset);
        else if (extension == ".wav" || extension == ".dasound")
            sounds.push_back(asset);
        else if (extension == ".datrail")
            trail_assets.push_back(asset);
        else if (extension == ".dadecal")
            decal_assets.push_back(asset);
    }
    if (cues.empty())
        ImGui::TextDisabled("This project has no cues yet.");
    for (const auto& asset : cues) {
        const auto draft = editor.authoring.drafts.find(asset.id);
        const bool dirty = draft != editor.authoring.drafts.end() && draft->second.dirty();
        if (ImGui::Selectable((asset.path + (dirty ? " *" : "") + "##cue").c_str(), asset.id == preview.cue)) {
            preview.cue = asset.id;
            preview.cue_error.clear();
        }
    }
    ImGui::SetNextItemWidth(220);
    ImGui::InputText("##cue-name", preview.cue_name, sizeof(preview.cue_name));
    ImGui::SameLine();
    if (ImGui::Button("New cue")) {
        try {
            preview.cue = editor.authoring.create_cue(*editor.assets, preview.cue_name);
            editor.refresh_assets();
            preview.cue_error.clear();
        } catch (const std::exception& error) {
            preview.cue_error = error.what();
        }
    }
    if (preview.cue != AssetId{}) {
        darkangel::editor_app::NativeDraft* draft{};
        try {
            draft = &editor.authoring.open(*editor.assets, preview.cue);
        } catch (const std::exception& error) {
            preview.cue_error = error.what();
            preview.cue = AssetId{};
        }
        if (draft) {
            auto& value = draft->value;
            ImGui::SeparatorText("Selected cue");
            ImGui::TextWrapped("%s", draft->asset.path.c_str());
            const auto field = [&](const char* label, const char* name) {
                char buffer[65]{};
                const auto current = value.value(name, std::string{});
                std::snprintf(buffer, sizeof(buffer), "%s", current.c_str());
                if (ImGui::InputText(label, buffer, sizeof(buffer)))
                    value[name] = std::string(buffer);
            };
            field("Name##cue", "name");
            field("Key##cue", "key");
            const auto pick = [&](const char* label, const char* name, const std::vector<darkangel::AssetInfo>& choices, bool effect) {
                const auto current = value.value(name, std::string{});
                if (ImGui::BeginCombo(label, current.empty() ? "(none)" : current.c_str())) {
                    if (ImGui::Selectable("(none)", current.empty()))
                        value[name] = "";
                    for (const auto& choice : choices)
                        if (ImGui::Selectable(choice.path.c_str(), choice.path == current)) {
                            value[name] = choice.path;
                            if (effect)
                                vfx_select(choice);
                        }
                    ImGui::EndCombo();
                }
            };
            pick("Effect##cue", "visual", vfx_effects(), true);
            pick("Sound##cue", "audio", sounds, false);
            // A trail and a decal are optional fields of the cue: absent when none is chosen.
            const auto optional = [&](const char* label, const char* name, const std::vector<darkangel::AssetInfo>& choices) {
                const auto current = value.value(name, std::string{});
                if (ImGui::BeginCombo(label, current.empty() ? "(none)" : current.c_str())) {
                    if (ImGui::Selectable("(none)", current.empty()))
                        value.erase(name);
                    for (const auto& choice : choices)
                        if (ImGui::Selectable(choice.path.c_str(), choice.path == current)) {
                            value[name] = choice.path;
                            preview.primitive = choice.id;
                        }
                    ImGui::EndCombo();
                }
            };
            optional("Weapon trail##cue", "trail", trail_assets);
            optional("Decal##cue", "decal", decal_assets);
            field("Socket or joint##cue", "socket");
            float offset[3]{};
            if (value.contains("offset") && value["offset"].is_array() && value["offset"].size() == 3)
                for (unsigned axis = 0; axis < 3; ++axis)
                    if (value["offset"][axis].is_number()) offset[axis] = value["offset"][axis].get<float>();
            if (ImGui::InputFloat3("Local offset##cue", offset, "%.2f"))
                value["offset"] = {offset[0], offset[1], offset[2]};
            float scale = float(value.value("scale", 1.0));
            if (ImGui::SliderFloat("Visual scale##cue", &scale, 0.01f, 10.0f, "%.2f"))
                value["scale"] = scale;
            int tail = int(value.value("tail_ticks", 30u));
            if (ImGui::InputInt("Tail (ticks)##cue", &tail))
                value["tail_ticks"] = unsigned(std::clamp(tail, 0, 600));
            bool predictable = value.value("predictable", false);
            if (ImGui::Checkbox("Starts on prediction (the owner's own cue)", &predictable))
                value["predictable"] = predictable;
            std::string problem;
            try {
                darkangel::decode_cue(value.dump());
            } catch (const std::exception& error) {
                problem = error.what();
            }
            if (!problem.empty())
                red(problem);
            ImGui::BeginDisabled(!draft->dirty() || !problem.empty());
            if (ImGui::Button("Save cue")) {
                try {
                    const auto path = draft->asset.path;
                    editor.authoring.save(*editor.assets, preview.cue);
                    editor.refresh_assets();
                    if (on_cue_sources_saved)
                        on_cue_sources_saved();
                    preview.cue_error.clear();
                    editor.log("Saved " + path);
                } catch (const std::exception& error) {
                    preview.cue_error = error.what();
                }
            }
            ImGui::EndDisabled();
            if (reveal_cue_form)
                ImGui::SetScrollHereY(1.f);
            try {
                editor.authoring.record_changes(*editor.assets, "Edit cue", ImGui::IsAnyItemActive());
            } catch (const std::exception& error) {
                preview.cue_error = error.what();
            }
        }
    }
    if (!preview.cue_error.empty())
        red(preview.cue_error);
    // Weapon trails and decals: native, small, and edited here because a cue is what names them.
    ImGui::SeparatorText("Trails and decals");
    ImGui::TextWrapped("A weapon trail is a strip swept by two points on a socket while its cue is live. A decal is a soft disc laid under "
                       "the actor when its cue starts.");
    for (const auto* list : {&trail_assets, &decal_assets})
        for (const auto& asset : *list) {
            const auto draft = editor.authoring.drafts.find(asset.id);
            const bool dirty = draft != editor.authoring.drafts.end() && draft->second.dirty();
            if (ImGui::Selectable((asset.path + (dirty ? " *" : "") + "##primitive").c_str(), asset.id == preview.primitive)) {
                preview.primitive = asset.id;
                preview.cue_error.clear();
            }
        }
    ImGui::SetNextItemWidth(160);
    ImGui::InputText("##primitive-name", preview.primitive_name, sizeof(preview.primitive_name));
    ImGui::SameLine();
    if (ImGui::Button("New trail")) {
        try {
            preview.primitive = editor.authoring.create_trail(*editor.assets, preview.primitive_name);
            editor.refresh_assets();
            preview.cue_error.clear();
        } catch (const std::exception& error) {
            preview.cue_error = error.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("New decal")) {
        try {
            preview.primitive = editor.authoring.create_decal(*editor.assets, preview.primitive_name);
            editor.refresh_assets();
            preview.cue_error.clear();
        } catch (const std::exception& error) {
            preview.cue_error = error.what();
        }
    }
    if (preview.primitive != AssetId{}) {
        darkangel::editor_app::NativeDraft* draft{};
        try {
            draft = &editor.authoring.open(*editor.assets, preview.primitive);
        } catch (const std::exception& error) {
            preview.cue_error = error.what();
            preview.primitive = AssetId{};
        }
        if (draft) {
            auto& value = draft->value;
            const bool trail = value.value("kind", std::string{}) == "trail";
            ImGui::SeparatorText(trail ? "Selected trail" : "Selected decal");
            ImGui::TextWrapped("%s", draft->asset.path.c_str());
            const auto text = [&](const char* label, const char* name) {
                char buffer[65]{};
                std::snprintf(buffer, sizeof(buffer), "%s", value.value(name, std::string{}).c_str());
                if (ImGui::InputText(label, buffer, sizeof(buffer)))
                    value[name] = std::string(buffer);
            };
            const auto floats = [&](const char* label, const char* name, int count, float least, float most) {
                float numbers[4]{};
                for (int index = 0; index < count; ++index)
                    numbers[index] = value[name].is_array() && value[name].size() == std::size_t(count) && value[name][index].is_number()
                                         ? value[name][index].get<float>()
                                         : 0.f;
                bool changed =
                    count == 3 ? ImGui::DragFloat3(label, numbers, .01f, least, most, "%.2f") : ImGui::ColorEdit4(label, numbers);
                if (changed) {
                    value[name] = nlohmann::json::array();
                    for (int index = 0; index < count; ++index)
                        value[name].push_back(double(std::round(std::clamp(numbers[index], least, most) * 1000) / 1000));
                }
            };
            const auto whole = [&](const char* label, const char* name, int least, int most) {
                int number = int(value.value(name, unsigned(least)));
                if (ImGui::InputInt(label, &number))
                    value[name] = unsigned(std::clamp(number, least, most));
            };
            text("Name##primitive", "name");
            if (trail) {
                text("Socket or joint##primitive", "socket");
                floats("Base offset (m)##primitive", "base", 3, -5, 5);
                floats("Tip offset (m)##primitive", "tip", 3, -5, 5);
                whole("Lifetime (ticks)##primitive", "lifetime_ticks", 1, 120);
                whole("Points##primitive", "max_points", 2, 64);
                floats("Colour##primitive", "color", 4, 0, 1);
            } else {
                float size = value.value("size", 1.f);
                if (ImGui::SliderFloat("Size (m)##primitive", &size, .05f, 10, "%.2f"))
                    value["size"] = double(std::round(size * 100) / 100);
                whole("Lifetime (ticks)##primitive", "lifetime_ticks", 1, 3600);
                whole("Fade (ticks)##primitive", "fade_ticks", 0, 3600);
                floats("Colour##primitive", "color", 4, 0, 1);
            }
            std::string problem;
            try {
                if (trail)
                    darkangel::decode_trail(value.dump());
                else
                    darkangel::decode_decal(value.dump());
            } catch (const std::exception& error) {
                problem = error.what();
            }
            if (!problem.empty())
                red(problem);
            ImGui::BeginDisabled(!draft->dirty() || !problem.empty());
            if (ImGui::Button("Save##primitive")) {
                try {
                    const auto path = draft->asset.path;
                    editor.authoring.save(*editor.assets, preview.primitive);
                    editor.refresh_assets();
                    if (on_cue_sources_saved)
                        on_cue_sources_saved();
                    preview.cue_error.clear();
                    editor.log("Saved " + path);
                } catch (const std::exception& error) {
                    preview.cue_error = error.what();
                }
            }
            ImGui::EndDisabled();
            try {
                editor.authoring.record_changes(*editor.assets, "Edit trail or decal", ImGui::IsAnyItemActive());
            } catch (const std::exception& error) {
                preview.cue_error = error.what();
            }
        }
    }
}
void VfxWorkspace::draw_panel() {
    auto& editor = editor_;
    auto& vfx = service;
    auto& vfx_failed = failed;
    auto& vfx_preview = state;
    const auto vfx_effects = [this] { return effects(); };
    const auto vfx_cook = [this] { cook(); };
    const auto vfx_select = [this](const darkangel::AssetInfo& asset) { select(asset); };
    const auto vfx_cues = [this] { draw_cues(); };
    auto& preview = vfx_preview;
    ImGui::TextWrapped("Effects are built in Effekseer. Pick one to preview it as the game draws it; \"Edit in Effekseer\" opens it there, "
                       "and saving it there updates the preview.");
    if (!vfx) {
        ImGui::TextWrapped("Visual effects are unavailable: %s", vfx_failed.c_str());
        return;
    }
    ImGui::SeparatorText("Effects");
    const auto effects = vfx_effects();
    if (effects.empty())
        ImGui::TextDisabled("This project has no effects yet.");
    // Opening the tab shows an effect playing straight away rather than an empty preview.
    if (preview.selected == AssetId{} && !effects.empty())
        vfx_select(effects.front());
    for (const auto& asset : effects)
        if (ImGui::Selectable(asset.path.c_str(), asset.id == preview.selected))
            vfx_select(asset);
    ImGui::SeparatorText("New effect");
    ImGui::SetNextItemWidth(220);
    ImGui::InputText("##vfx-name", preview.name, sizeof(preview.name));
    ImGui::SameLine();
    {
        const std::string name(preview.name);
        const bool valid = !name.empty() && name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == name.npos;
        const auto root = editor.assets->source_root();
        const auto relative = "vfx/" + name + ".efkproj";
        const auto starter = root / "vfx/placeholder/hit_spark.efkproj";
        ImGui::BeginDisabled(!valid || std::filesystem::exists(root / relative) || !std::filesystem::exists(starter));
        if (ImGui::Button("Create")) {
            try {
                std::filesystem::create_directories(root / "vfx/Texture");
                if (!std::filesystem::exists(root / "vfx/Texture/glow.png"))
                    std::filesystem::copy_file(root / "vfx/placeholder/Texture/glow.png", root / "vfx/Texture/glow.png");
                std::filesystem::copy_file(starter, root / relative);
                editor.assets->adopt(relative);
                editor.refresh_assets();
                for (const auto& asset : vfx_effects())
                    if (asset.path == relative)
                        vfx_select(asset);
                editor.log("Created " + relative + " from the placeholder spark. Open it in Effekseer to build the effect.");
            } catch (const std::exception& error) {
                preview.error = error.what();
            }
        }
        ImGui::EndDisabled();
        if (!valid)
            ImGui::TextDisabled("Lowercase letters, digits and underscores.");
        else if (std::filesystem::exists(root / relative))
            ImGui::TextDisabled("An effect with this name exists.");
    }
    if (preview.selected == AssetId{}) {
        vfx_cues();
        return;
    }
    ImGui::SeparatorText("Selected");
    ImGui::TextWrapped("%s", preview.path.c_str());
    if (ImGui::Button("Edit in Effekseer")) {
        const auto tool = darkangel::effekseer_tool();
        const auto file = (editor.assets->source_root() / preview.path).make_preferred();
        if (!std::filesystem::is_regular_file(tool))
            preview.error = "The Effekseer tool is missing from .tools";
        else {
            const std::wstring arguments = L"\"" + file.wstring() + L"\"";
            const auto started = reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", tool.wstring().c_str(), arguments.c_str(),
                                                                               tool.parent_path().wstring().c_str(), SW_SHOWNORMAL));
            if (started <= 32)
                preview.error = "Effekseer could not be started";
            else
                editor.log("Opened " + preview.path + " in Effekseer. Save it there to update the preview.");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cook again"))
        vfx_cook();
    ImGui::SameLine();
    if (ImGui::Button("Restart") && preview.cooked) {
        if (preview.handle)
            vfx->kill(preview.handle);
        preview.handle = 0;
        preview.idle = 1;
    }
    if (!preview.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, .45f, .35f, 1.f));
        ImGui::TextWrapped("Not cooked: %s", preview.error.c_str());
        ImGui::PopStyleColor();
        ImGui::TextWrapped("The preview keeps the last version that cooked.");
    } else if (preview.cooked)
        ImGui::TextWrapped("Cooked %u time(s) this session; %zu texture(s); product %.12s.", preview.cooks, preview.textures,
                           preview.generation.c_str());
    {
        const auto drawn = vfx->stats();
        ImGui::Text("%zu particle(s), %u quad(s) in %u draw(s)", drawn.instances, drawn.quads, drawn.draws);
        if (drawn.unsupported)
            ImGui::TextWrapped("%u batch(es) skipped: they need shading the renderer does not have.", drawn.unsupported);
    }
    ImGui::SeparatorText("Preview camera");
    ImGui::SliderFloat("Turn", &preview.yaw, -3.14f, 3.14f);
    ImGui::SliderFloat("Distance", &preview.distance, 1.f, 20.f);
    ImGui::SliderFloat("Height", &preview.height, 0.f, 3.f);
    vfx_cues();
}
void VfxWorkspace::preview_frame(unsigned frame, double step, bool shown_tab, const VfxView& shown) {
    auto& vfx = service;
    auto& vfx_preview = state;
    auto& editor = editor_;
    if (vfx && shown_tab && vfx_preview.selected != AssetId{}) {
        auto& preview = vfx_preview;
        if (editor.assets && frame % 30 == 0) {
            std::error_code ignored;
            const auto written = std::filesystem::last_write_time(editor.assets->source_root() / preview.path, ignored);
            if (!ignored && written != preview.seen)
                cook();
        }
        vfx->update(step);
        if (preview.cooked && !vfx->alive(preview.handle)) {
            preview.idle += step;
            if (preview.handle == 0 || preview.idle > .5) {
                preview.handle = vfx->play(preview.selected, {0, preview.height, 0});
                preview.idle = 0;
            }
        }
        try {
            vfx->draw(shown);
        } catch (const std::exception& error) {
            vfx_preview.error = error.what();
        }
    }
}
} // namespace darkangel::editor_app
