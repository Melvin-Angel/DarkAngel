#include "native_authoring_test_support.hpp"

void run_native_authoring_commands() {
    NativeAuthoringCommandFixture fixture;
    auto& assets = *fixture.service;
    const auto& source = fixture.source;
    const auto& root = fixture.root;
    const auto kit = fixture.kit, heavy = fixture.heavy, burn = fixture.burn, human = fixture.human;
    const auto& roots = fixture.roots;
    auto id = [&](const char* file) { return fixture.id(file); };

    {
        NativeAuthoring independent;
        auto template_value = independent.open(assets, heavy).value;
        auto old_action = AssetId::parse(template_value.at("action").get<std::string>());
        auto action_before = independent.open(assets, old_action).value;
        auto copy = independent.duplicate_ability_with_action(assets, heavy, "independent_attack", true);
        auto copied = independent.open(assets, copy).value;
        auto action = AssetId::parse(copied.at("action").get<std::string>());
        check(action != old_action && copied.at("sources").at("action") == independent.open(assets, action).asset.path &&
                  copied.at("costs").empty(),
              "Independent creation lost paired references or blank defaults");
        check(independent.history().size() == 1 && independent.history().front().assets.size() == 2,
              "Independent creation must be one paired command");
        independent.undo(assets);
        check(!independent.drafts.contains(copy) && !independent.drafts.contains(action), "Paired creation Undo retained half a source");
        independent.redo(assets);
        check(independent.open(assets, copy).value == copied, "Paired creation Redo lost references");
        auto block = independent.open(assets, action).value.at("blocks").at(1);
        auto key = block.at("id").get<unsigned>();
        independent.set_action_block_times(assets, action, key, block.at("begin").get<unsigned>(),
                                           block.at("end").get<unsigned>() + action_tick_units);
        check(independent.open(assets, old_action).value == action_before, "Independent timing changed template Composer");
        auto count = independent.history().size(), revision = independent.revision();
        rejects([&] { independent.duplicate_ability_with_action(assets, heavy, "independent_attack", true); });
        check(independent.history().size() == count && independent.revision() == revision,
              "Failed paired creation changed history/revision");
        auto collision = independent.duplicate(assets, heavy, "ability_only_collision");
        auto collision_count = independent.history().size(), collision_revision = independent.revision(),
             draft_count = independent.drafts.size();
        rejects([&] { independent.duplicate_ability_with_action(assets, heavy, "ability_only_collision", true); });
        check(independent.history().size() == collision_count && independent.revision() == collision_revision &&
                  independent.drafts.size() == draft_count,
              "Second-source collision left a partial Composer");
        independent.undo(assets);
        check(!independent.drafts.contains(collision), "Collision fixture cleanup failed");
        auto summary = independent.validate(assets, copy, roots);
        check(summary.sources == 2 && !std::filesystem::exists(source / independent.open(assets, copy).asset.path),
              "Paired private validation published source");
    }
    {
        NativeAuthoring assigning;
        auto character = assigning.create_character(assets, human, "kit_assignment_character");
        auto player = assigning.create_player(assets, character, kit, "kit_assignment_player");
        auto copied = assigning.duplicate(assets, kit, "kit_assignment_copy");
        auto before = assigning.open(assets, player).value;
        auto kit_before = read(source / assigning.open(assets, kit).asset.path);
        auto history = assigning.history().size();
        rejects_with([&] { assigning.assign_player_kit(assets, player, burn); }, "Select a Player");
        check(assigning.open(assets, player).value == before && assigning.history().size() == history,
              "Rejected Player Kit assignment changed source/history");
        assigning.assign_player_kit(assets, player, copied);
        auto assigned = assigning.open(assets, player).value;
        check(assigned.at("loadout").at("kit") == copied.text() &&
                  assigned.at("sources").at("kit") == assigning.open(assets, copied).asset.path &&
                  assigned.at("character") == before.at("character"),
              "Player Kit assignment lost reference/locator/presentation ownership");
        assigning.undo(assets);
        check(assigning.open(assets, player).value == before, "Player Kit Undo lost original loadout");
        assigning.redo(assets);
        check(assigning.open(assets, player).value == assigned, "Player Kit Redo lost reference locator");
        auto summary = assigning.validate(assets, player, roots);
        check(summary.sources == 3 && !std::filesystem::exists(source / assigning.open(assets, player).asset.path) &&
                  read(source / assigning.open(assets, kit).asset.path) == kit_before,
              "Player Kit validation published or changed shared source");
        std::cout
            << "Player Kit assignment: typed rejection, paired source identity/locator, retained Character, Undo/Redo and private three-source closure passed\n";
    }
    {
        NativeAuthoring comparison;
        auto& opened = comparison.open(assets, heavy);
        auto path = source / opened.asset.path;
        auto baseline = read(path);
        auto revision = comparison.revision();
        auto same = comparison.compare_source(assets, heavy);
        check(same.exists && same.matches && !same.pending && same.disk_sha == sha256(baseline),
              "Source comparison lost matching baseline");
        std::ofstream(path, std::ios::binary) << baseline << "\n";
        auto changed = comparison.compare_source(assets, heavy);
        check(changed.exists && !changed.matches && comparison.revision() == revision && comparison.history().empty() &&
                  comparison.open(assets, heavy).saved == baseline,
              "Source comparison adopted external bytes or changed history");
        std::ofstream(path, std::ios::binary) << baseline;
        auto pending = comparison.duplicate(assets, heavy, "comparison_pending");
        auto pending_path = source / comparison.open(assets, pending).asset.path;
        auto absent = comparison.compare_source(assets, pending);
        check(absent.pending && !absent.exists && absent.matches, "Pending comparison required a published source");
        std::ofstream(pending_path, std::ios::binary) << comparison.open(assets, pending).saved;
        auto collision = comparison.compare_source(assets, pending);
        check(collision.pending && collision.exists && !collision.matches, "Pending comparison accepted path collision");
        std::ofstream(pending_path, std::ios::binary) << std::string(65537, 'x');
        rejects_with([&] { comparison.compare_source(assets, pending); }, "size limit");
        std::filesystem::remove(pending_path);
        check(read(path) == baseline, "Read-only comparison changed restored source");
        std::cout
            << "Source disk comparison: exact baseline/external edit, pending absence/collision, bounded read and unchanged revision/history passed\n";
    }
}
