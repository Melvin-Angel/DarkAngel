#pragma once
#include "audio_cue_sink.hpp"
#include <darkangel/audio_director.hpp>
#include <darkangel/asset_id.hpp>
#include <darkangel/world.hpp>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace darkangel {
class CharacterSceneSession;
}
namespace darkangel::editor_app {
class Controller;
class RecordingCueSink;
struct EditorOptions;
// The process's audio: one AudioService made when something first needs to be heard, the music/ambience director of a
// Play or a profile audition, the generations of loaded sounds, and the Audio tab's state and panel.
class AudioWorkspace {
  public:
    AudioWorkspace(Controller&, const EditorOptions&, RecordingCueSink& recording);
    // The project's mixer: its open draft when there is one, else audio/project.damix as last read from disk.
    AudioMix project_mix();
    // Makes the service when needed, and again when the mixer's structure changed while nothing plays. False when unavailable.
    bool ensure();
    // Cooks and loads sounds by identity (WAVs, sound definitions, whole profiles). Returns the profiles.
    std::map<AssetId, AudioProfile> load(std::vector<AssetId> wanted);
    // The Audio tab. Profile audition is disabled while Play runs.
    void draw_panel(bool play_running);
    // Once a frame: the mixer as the project (or its open draft) has it, encounter profiles layered while their
    // encounter runs, and the engine's clock outside Play. tab_shown: the Audio tab is the open workspace.
    void frame(unsigned frame, double seconds, bool tab_shown, CharacterSceneSession* session);

    struct State {
        AssetId sound, profile;
        std::uint64_t audition{};
        bool profile_audition{};
        char sound_name[49]{"new_sound"}, profile_name[49]{"new_profile"}, import_path[512]{};
        std::string error;
        darkangel::AudioMix file_mix;
        std::filesystem::file_time_type file_seen{};
        bool file_known{};
    };
    std::unique_ptr<AudioService> service;
    std::map<std::string, AssetId> play_sounds;
    AudioCueSink cue_sounds;
    std::string failed;
    bool tried{};
    std::unique_ptr<AudioDirector> director;
    std::map<AssetId, AudioProfile> play_profiles;
    std::map<StableId, AssetId> encounter_profiles;
    std::optional<AudioMix> applied;
    bool in_play{};
    std::map<AssetId, std::string> generations;
    State state;
    // Called after a sound source is saved, so the next Play cooks its cues again.
    std::function<void()> on_cue_sources_saved;
    const std::string mix_path = "audio/project.damix";

  private:
    Controller& editor_;
    const EditorOptions& options_;
};
} // namespace darkangel::editor_app
