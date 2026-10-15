#include "native_authoring_test_support.hpp"

void run_native_authoring_effect_preview() {
    NativeAuthoringCommandFixture fixture;
    auto& assets = *fixture.service;
    const auto& source = fixture.source;
    const auto& root = fixture.root;
    const auto kit = fixture.kit, heavy = fixture.heavy, burn = fixture.burn, human = fixture.human;
    const auto& roots = fixture.roots;
    auto id = [&](const char* file) { return fixture.id(file); };

    {
        NativeAuthoring ownership;
        auto status = ownership.duplicate(assets, burn, "source_preview_status", true);
        auto value = ownership.open(assets, status).value;
        value["granted_tags"] = {3};
        value["stacking"] = "refresh_per_source";
        value["duration_ticks"] = 60;
        value["remove_with_source"] = true;
        value["remove_on_death"] = true;
        ownership.apply(assets, status, ownership.revision(), value, "Source-owned status preview");
        ownership.add_effect_modifier(assets, status, 6);
        value = ownership.open(assets, status).value;
        value["modifiers"][0]["magnitude"] = 5;
        ownership.apply(assets, status, ownership.revision(), value, "Source-owned modifier preview");
        EffectPreview preview(assets, ownership, status);
        auto base = preview.attributes->value(6);
        rejects([&] { preview.apply(0); });
        check(preview.effects->snapshot().empty(), "Invalid simulated source published effect");
        preview.apply(1);
        auto first = preview.handle;
        preview.apply(2);
        preview.apply(1);
        check(preview.effects->snapshot().size() == 2 && preview.handle == first && preview.contributors(3).size() == 2 &&
                  preview.attributes->value(6) == base + 10,
              "Refresh-per-source lost independent grants/modifiers");
        preview.destroy_source(1);
        check(preview.effects->snapshot().size() == 1 && preview.effects->tags().has(3) &&
                  preview.contributors(3).front().credit.source_network == 2 && preview.attributes->value(6) == base + 5,
              "Removing source removed another source tag/modifier");
        preview.remove(preview.effects->snapshot().front().handle);
        check(!preview.effects->tags().has(3) && preview.attributes->value(6) == base, "Selected removal retained tag/modifier");
        preview.apply(1);
        preview.apply(2);
        preview.death_cleanup();
        check(preview.effects->snapshot().empty() && !preview.effects->tags().has(3) && preview.attributes->value(6) == base,
              "Death cleanup retained eligible contributions");
        preview.apply(2);
        preview.advance(60);
        check(preview.effects->snapshot().empty() && !preview.effects->tags().has(3), "Source-owned preview expiry retained state");
        value["remove_with_source"] = false;
        value["remove_on_death"] = false;
        ownership.apply(assets, status, ownership.revision(), value, "Retained status cleanup policies");
        EffectPreview retained(assets, ownership, status);
        retained.apply(2);
        retained.destroy_source(2);
        check(retained.effects->snapshot().size() == 1 && retained.last_operation.find("policy is disabled") != std::string::npos,
              "Disabled source cleanup lost effect or diagnostic");
        retained.death_cleanup();
        check(retained.effects->snapshot().size() == 1 && retained.last_operation.find("policy is disabled") != std::string::npos,
              "Disabled death cleanup lost effect or diagnostic");
        retained.destroy_source(3);
        check(retained.last_operation.find("No active effect") != std::string::npos, "Unrelated source cleanup lacked result");
        value["application"]["none"] = {3};
        ownership.apply(assets, status, ownership.revision(), value, "Blocked repeated status application");
        EffectPreview blocked(assets, ownership, status);
        blocked.apply(1);
        auto active = blocked.effects->snapshot();
        auto amount = blocked.attributes->value(6);
        try {
            blocked.apply(2);
            throw std::runtime_error("Blocked application unexpectedly succeeded");
        } catch (const std::exception& error) {
            check(std::string_view(error.what()).find("tag requirement") != std::string_view::npos,
                  "Blocked preview lost native rejection");
            blocked.record_failure("Apply effect", error.what(), 2);
        }
        check(blocked.effects->snapshot().size() == 1 && blocked.effects->snapshot().front().handle == active.front().handle &&
                  blocked.attributes->value(6) == amount &&
                  blocked.last_error.find("source_preview_status.daeffect") != std::string::npos &&
                  blocked.last_error.find("source 2") != std::string::npos,
              "Failed preview application lost state/context");
        blocked.remove(active.front().handle);
        blocked.apply(2);
        check(blocked.effects->snapshot().front().credit.activation == 2, "Failed native application consumed activation identity");
        auto frozen = blocked.cooked.definition;
        auto frozen_revision = blocked.authoring_revision;
        blocked.advance(30);
        blocked.reset();
        check(blocked.tick == 0 && blocked.effects->snapshot().empty() && blocked.attributes->value(6) == blocked.base[5] &&
                  blocked.last_error.empty() && blocked.cooked.definition == frozen && blocked.authoring_revision == frozen_revision,
              "Frozen reset changed source snapshot or retained simulation state");
        blocked.apply(2);
        check(blocked.effects->snapshot().front().credit.activation == 1, "Frozen reset retained activation counter");
        check(!std::filesystem::exists(source / ownership.open(assets, status).asset.path), "Effect ownership preview published source");
        std::cout
            << "Effect source preview: nonzero source fence, refresh identity, independent grants/modifiers, selected/source/death cleanup and expiry passed\n";
    }
    {
        NativeAuthoring templates;
        auto instant = templates.duplicate(assets, burn, "instant_template");
        auto instant_value = templates.open(assets, instant).value;
        instant_value["lifetime"] = "instant";
        instant_value["duration_ticks"] = 0;
        instant_value["period_ticks"] = 0;
        instant_value["execute_on_apply"] = true;
        instant_value["evaluator"] = 2;
        instant_value["stacking"] = "independent";
        instant_value["modifiers"] = Json::array();
        instant_value["granted_tags"] = Json::array();
        instant_value["cues"] = Json::array();
        instant_value["sources"].erase("cues");
        templates.apply(assets, instant, templates.revision(), instant_value, "Instant native template");
        auto status = templates.duplicate(assets, instant, "status_from_instant", true);
        auto status_value = templates.open(assets, status).value;
        check(status_value["lifetime"] == "finite" && status_value["duration_ticks"] == 60 &&
                  !status_value["execute_on_apply"].get<bool>() && status_value["evaluator"] == 0,
              "Ordinary template retained instant/execution policies");
        auto inventory_before = assets.assets();
        EffectPreview status_preview(assets, templates, status);
        status_preview.apply();
        check(status_preview.effects->snapshot().size() == 1, "Ordinary template failed native application");
        auto snapshot_revision = status_preview.authoring_revision;
        status_value["duration_ticks"] = 120;
        templates.apply(assets, status, templates.revision(), status_value, "Edit frozen preview lifetime");
        check(snapshot_revision != templates.revision() && status_preview.cooked.definition->duration_ticks == 60,
              "Draft edit silently changed frozen preview");
        status_preview.advance(60);
        check(status_preview.effects->snapshot().empty(), "Ordinary template failed native expiry");
        check(!std::filesystem::exists(source / templates.open(assets, instant).asset.path) &&
                  !std::filesystem::exists(source / templates.open(assets, status).asset.path),
              "Template preview published sources");
        auto inventory_after = assets.assets();
        check(inventory_before.size() == inventory_after.size(), "Template preview registered assets");
        for (unsigned i = 0; i < inventory_before.size(); ++i)
            check(inventory_before[i].id == inventory_after[i].id && inventory_before[i].generation == inventory_after[i].generation,
                  "Template preview published catalog heads");
    }
}
