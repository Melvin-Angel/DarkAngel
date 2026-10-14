# Generated tag constants

9 October 2026, following Royal heavy/Burn checkpoint `0c42c43`. M4/M5 remain **In progress**.

The existing prepared `TagAsset` now exports C++ `TagId` constants and a strict Luau module from the same native dictionary. Both include the registry UUID, generation fingerprint and each tag's persistent authoring key. Output order follows runtime IDs. Generated identifiers use an ASCII name plus the ID, so keywords, punctuation, Unicode bytes and names that normalize alike cannot introduce executable text or collide. Registry/key/generation validation runs before export.

`AssetTool tag-constants registry CAS tag-UUID output-directory` loads the existing source-free registry/CAS product and publishes both outputs into one immutable content-addressed directory. The final directory appears only after both files are complete. Warm exports reuse exact bytes; conflicting existing artifacts reject without replacing them. `.datags` and its stable UUID keys remain authoritative; generated files are derived build outputs.

Royal's build uses the native cook/package/export path to generate its C++ header and Luau module. Ashen Roots validates the compiled registry generation when preparing combat, then uses the typed invulnerability constant. Changed tag products require compatible rebuilt game consumers. Builds without the game asset profile retain the independent character-scene engine target. AssetTool explicitly links its newly required Runtime tag API in the animation-disabled profile.

The Luau module freezes the registry, ID and key tables and exposes a typed `matches(registry, generation)` check. `TagId` is currently a number alias. These constants do not add a native ability/effect scripting service or automatically grant script authority; checked gameplay composition remains required.

## Verification

73/73 complete optimized native tests pass. Dedicated native/full-editor/animation-disabled exporter fixtures, strict Luau analyzer and sandbox execution, the compiled Royal consumer and stale-generation rejection, native CLI publication, source-free reproduction, atomic conflict preservation, existing Headless/GNS compatibility and D3D12/Vulkan light/heavy checks pass. Reproduce `python scripts/verify_tag_constants.py --build`; source, binary and generated artifact hashes are in `evidence/tag-constants.json` with linked product receipts.

## Remaining gates

Checked Luau ability/effect composition and reflected authoring remain required. Next provider check is GNS joining an already-active effect state with dense owner fragmentation, audience privacy, surviving source credit and persistent cue expiry. This is distinct from live EOS qualification, full delayed Jolt prediction, graphical remote play and the complete fault/clock/streaming/performance matrix. Utility dodge, motor/status/equipment/combo/projectile rules, typed graph/layer/mask/socket authoring, persistent cue visual/audio/one-shot adapters and graphical RmlUi bars also remain open. M6 has not started.
