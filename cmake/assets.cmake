if(DAE_ASSETS)
  find_package(unofficial-sqlite3 CONFIG REQUIRED)
  find_package(directxtex CONFIG REQUIRED)
  find_package(meshoptimizer CONFIG REQUIRED)
  find_path(DAE_CGLTF_INCLUDE_DIR cgltf.h REQUIRED)
  set(DAE_ASSET_SOURCES engine/assets/item_cooker.cpp engine/assets/asset_service.cpp engine/assets/assets_common.cpp engine/assets/gltf_cooker.cpp engine/assets/collision_cooker.cpp engine/assets/model_collision.cpp engine/assets/action_cooker.cpp engine/assets/external_import.cpp engine/assets/input_cooker.cpp engine/assets/cue_cooker.cpp engine/assets/audio_cooker.cpp engine/assets/primitive_cooker.cpp engine/assets/vfx_cooker.cpp engine/assets/sound_cooker.cpp engine/assets/ability_cooker.cpp engine/assets/effect_cooker.cpp engine/assets/prefab_cooker.cpp engine/assets/material_cooker.cpp engine/assets/ui_cooker.cpp)
  set(DAE_COOKER_INPUTS ${DAE_ASSET_SOURCES} engine/runtime/item_definition.cpp engine/runtime/inventory.cpp engine/runtime/include/darkangel/inventory.hpp engine/assets/include/darkangel/effect_assets.hpp engine/assets/include/darkangel/tag_assets.hpp engine/runtime/include/darkangel/effects.hpp engine/runtime/effects.cpp engine/runtime/include/darkangel/tags.hpp engine/runtime/tags.cpp engine/assets/include/darkangel/ability_assets.hpp engine/assets/include/darkangel/model_collision.hpp engine/runtime/include/darkangel/melee.hpp engine/runtime/ability.cpp engine/runtime/include/darkangel/ability.hpp engine/runtime/attributes.cpp engine/runtime/include/darkangel/attributes.hpp engine/runtime/input.cpp engine/runtime/include/darkangel/input.hpp engine/runtime/action.cpp engine/runtime/include/darkangel/action.hpp engine/runtime/joint_mask.cpp engine/runtime/include/darkangel/joint_mask.hpp engine/assets/joint_mask_cooker.cpp engine/assets/combat_kit_cooker.cpp engine/assets/actor_cooker.cpp engine/assets/include/darkangel/combat_kit_assets.hpp engine/assets/include/darkangel/actor_assets.hpp engine/assets/graph_cooker.cpp engine/assets/include/darkangel/graph_assets.hpp engine/runtime/animation_graph.cpp engine/runtime/include/darkangel/animation_graph.hpp engine/assets/include/darkangel/animation_assets.hpp engine/assets/animation_cooker.cpp engine/runtime/animation_source.cpp engine/runtime/collision_source.cpp engine/runtime/character_motor.cpp engine/runtime/include/darkangel/collision_asset.hpp engine/runtime/include/darkangel/character_motor.hpp engine/runtime/include/darkangel/motor_parameters.hpp engine/runtime/motor_state.cpp engine/assets/include/darkangel/prefab_assets.hpp engine/assets/include/darkangel/material_assets.hpp engine/assets/include/darkangel/ui_assets.hpp engine/runtime/assembly.cpp engine/runtime/include/darkangel/assembly.hpp engine/runtime/include/darkangel/animation.hpp engine/assets/include/darkangel/assets.hpp engine/assets/assets_internal.hpp engine/foundation/include/darkangel/asset_id.hpp vcpkg.json cmake/sources.lock.json)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DAE_COOKER_INPUTS})
  set(cooker_hashes "${CMAKE_BUILD_TYPE};${MSVC_VERSION};animation=${DAE_ANIMATION};physics=${DAE_PHYSICS}")
  foreach(source IN LISTS DAE_COOKER_INPUTS)
    file(SHA256 "${PROJECT_SOURCE_DIR}/${source}" source_hash)
    string(APPEND cooker_hashes ";${source_hash}")
  endforeach()
  string(SHA256 DAE_COOKER_BUILD_HASH "${cooker_hashes}")
  # Save slots (DAE-020): SQLite snapshots of an opaque versioned payload; no gameplay knowledge.
  add_library(DarkAngelPersistence STATIC engine/persistence/persistence.cpp)
  target_compile_features(DarkAngelPersistence PUBLIC cxx_std_20)
  target_include_directories(DarkAngelPersistence PUBLIC engine/persistence/include)
  target_link_libraries(DarkAngelPersistence PUBLIC DarkAngelFoundation PRIVATE unofficial::sqlite3::sqlite3 nlohmann_json::nlohmann_json)
  dae_test(PersistenceTests NAME M8.persistence
    SOURCES tests/persistence_tests.cpp
    LIBS DarkAngelPersistence unofficial::sqlite3::sqlite3 nlohmann_json::nlohmann_json)
  add_library(DarkAngelAssets STATIC ${DAE_ASSET_SOURCES})
  target_compile_features(DarkAngelAssets PUBLIC cxx_std_20)
  target_include_directories(DarkAngelAssets PUBLIC engine/assets/include PRIVATE "${DAE_CGLTF_INCLUDE_DIR}")
  target_link_libraries(DarkAngelAssets PUBLIC DarkAngelFoundation PRIVATE DarkAngelRuntime unofficial::sqlite3::sqlite3 nlohmann_json::nlohmann_json Microsoft::DirectXTex meshoptimizer::meshoptimizer ole32 windowscodecs bcrypt)
target_link_libraries(DarkAngelAssets PUBLIC DarkAngelInventorySession)
  target_compile_definitions(DarkAngelAssets PRIVATE DAE_COOKER_BUILD_HASH="${DAE_COOKER_BUILD_HASH}" NOMINMAX DAE_EFFEKSEER_TOOL="${PROJECT_SOURCE_DIR}/.tools/effekseer-editor/Effekseer1.80.7Win/Tool/Effekseer.exe")
  target_compile_options(DarkAngelAssets PRIVATE /W4 /utf-8 /EHsc)
  if(DAE_ANIMATION)
    target_sources(DarkAngelAssets PRIVATE engine/assets/animation_cooker.cpp engine/assets/joint_mask_cooker.cpp engine/assets/graph_cooker.cpp engine/assets/combat_kit_cooker.cpp engine/assets/actor_cooker.cpp)
    target_compile_definitions(DarkAngelAssets PRIVATE DAE_ANIMATION=1)
    target_link_libraries(DarkAngelAssets PRIVATE ozz_animation_offline DarkAngelAnimation)
  endif()
  if(DAE_PHYSICS)
    target_compile_definitions(DarkAngelAssets PRIVATE DAE_COLLISION_MESH=1)
    target_link_libraries(DarkAngelAssets PRIVATE DarkAngelPhysics)
  endif()
  add_executable(AssetTool tools/cooker/main.cpp)
  target_link_libraries(AssetTool PRIVATE DarkAngelAssets DarkAngelRuntime)
  if(TARGET DarkAngelEditorService AND DAE_PHYSICS)
    target_link_libraries(AssetTool PRIVATE DarkAngelEditorService)
    target_compile_definitions(AssetTool PRIVATE DAE_SCENE_COLLISION_BAKE=1)
  endif()
  target_compile_options(AssetTool PRIVATE /W4 /utf-8 /EHsc)
  target_sources(DarkAngelAssets PRIVATE engine/assets/tag_codegen.cpp)
  if(TARGET AshenRootsCombat)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(DAE_ROYAL_TAG_OUTPUT "${PROJECT_BINARY_DIR}/generated/royal-tags")
    add_custom_command(OUTPUT "${DAE_ROYAL_TAG_OUTPUT}/ashen_roots/royal_tags.hpp" "${DAE_ROYAL_TAG_OUTPUT}/royal_tags.luau"
      COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/generate_royal_tags.py" --asset-tool "$<TARGET_FILE:AssetTool>" --destination "${DAE_ROYAL_TAG_OUTPUT}"
      DEPENDS AssetTool scripts/generate_royal_tags.py content/royal_district/combat/state.datags
      VERBATIM)
    target_sources(AshenRootsCombat PRIVATE "${DAE_ROYAL_TAG_OUTPUT}/ashen_roots/royal_tags.hpp")
    target_include_directories(AshenRootsCombat PRIVATE "${DAE_ROYAL_TAG_OUTPUT}")
  endif()
  if(BUILD_TESTING)
    # R5 materials: material instances, model slot assignments, propagation, texture closure and repair.
    dae_test(MaterialTests NAME R5.materials
      SOURCES tests/material_tests.cpp
      LIBS DarkAngelAssets DarkAngelRuntime nlohmann_json::nlohmann_json
      DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_compile_options(MaterialTests PRIVATE /W4 /utf-8 /EHsc)
    set_tests_properties(R5.materials PROPERTIES TIMEOUT 120 LABELS "R5;asset")
    # R4 prefabs: resolver placement/identity/patches, document links and the scene-side prefab commands.
    dae_test(PrefabTests NAME R4.prefabs
      SOURCES tests/prefab_tests.cpp apps/editor/prefab_commands.cpp
      LIBS DarkAngelAssets DarkAngelEditorService DarkAngelRuntime nlohmann_json::nlohmann_json)
    target_compile_options(PrefabTests PRIVATE /W4 /utf-8 /EHsc)
    set_tests_properties(R4.prefabs PROPERTIES TIMEOUT 120 LABELS "R4;editor;asset")
    dae_test(TagCodegenTests NAME M5.tag_constants
      SOURCES tests/tag_codegen_tests.cpp
      LIBS DarkAngelAssets DarkAngelRuntime nlohmann_json::nlohmann_json unofficial::luau::Luau.Compiler unofficial::luau::Luau.VM
      DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
    dae_test(NativeSourceEditTests NAME M5.native_source_edit
      SOURCES tests/native_source_edit_tests.cpp
      LIBS DarkAngelAssets DarkAngelRuntime nlohmann_json::nlohmann_json unofficial::sqlite3::sqlite3
      DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
    if(DAE_ANIMATION)
      dae_test(AnimationTests NAME M5.canonical_rig
        SOURCES tests/animation_tests.cpp
        LIBS DarkAngelAssets DarkAngelAnimation DarkAngelEditorService nlohmann_json::nlohmann_json
        DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
      add_executable(AnimationClipTests tests/animation_clip_tests.cpp)
      target_link_libraries(AnimationClipTests PRIVATE DarkAngelAssets DarkAngelAnimation nlohmann_json::nlohmann_json)
      target_include_directories(AnimationClipTests PRIVATE "${DAE_CGLTF_INCLUDE_DIR}")
      target_compile_definitions(AnimationClipTests PRIVATE DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
      add_test(NAME M5.clips COMMAND AnimationClipTests)
      add_test(NAME M5.omni_clips COMMAND AnimationClipTests --omni)
      set_tests_properties(M5.omni_clips PROPERTIES TIMEOUT 60 LABELS "M5;animation;asset")
      dae_test(CombatKitAssetTests NAME M5.combat_kit_assets
        SOURCES tests/combat_kit_asset_tests.cpp
        LIBS DarkAngelAssets DarkAngelAnimation DarkAngelRuntime nlohmann_json::nlohmann_json
        DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
      dae_test(GraphAssetTests NAME M5.graph_assets
        SOURCES tests/graph_asset_tests.cpp
        LIBS DarkAngelAssets DarkAngelAnimation nlohmann_json::nlohmann_json
        DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
      dae_test(AnimationGraphTests NAME M5.graph_runtime
        SOURCES tests/animation_graph_tests.cpp
        LIBS DarkAngelAssets DarkAngelAnimation
        DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
      set_tests_properties(M5.graph_runtime PROPERTIES TIMEOUT 60 LABELS "M5;animation")
      if(TARGET DarkAngelCharacterScene)
        if(TARGET DarkAngelNetwork)
          add_executable(AuthoredEffectGnsTests tests/authored_effect_gns_tests.cpp)
          target_link_libraries(AuthoredEffectGnsTests PRIVATE DarkAngelNetwork DarkAngelAssets)
        endif()
        dae_test(GameplayDebugTests NAME M5.gameplay_debug
          SOURCES tests/gameplay_debug_tests.cpp
          LIBS DarkAngelRuntime)
        set_tests_properties(M5.gameplay_debug PROPERTIES TIMEOUT 30 LABELS "M5;editor")
        dae_test(ActorAssetTests NAME M5.actor_assets
          SOURCES tests/actor_asset_tests.cpp
          LIBS DarkAngelAssets DarkAngelRuntime DarkAngelAnimation nlohmann_json::nlohmann_json
          ARGS "${PROJECT_BINARY_DIR}/authoring-fixture.json")
        set_tests_properties(M5.actor_assets PROPERTIES FIXTURES_REQUIRED NativeAuthoringFixture TIMEOUT 90 LABELS "M5;asset")
        dae_test(AttributeAuthoringTests NAME M5.attribute_authoring
          SOURCES tests/attribute_authoring_tests.cpp apps/editor/native_authoring.cpp
          LIBS DarkAngelAssets DarkAngelEditorService nlohmann_json::nlohmann_json
          ARGS "${PROJECT_BINARY_DIR}/authoring-fixture.json")
        set_tests_properties(M5.attribute_authoring PROPERTIES FIXTURES_REQUIRED NativeAuthoringFixture TIMEOUT 90 LABELS "M5;editor;asset")
        dae_test(ComposerStructureTests NAME M5.composer_structure
          SOURCES tests/composer_structure_tests.cpp apps/editor/native_authoring.cpp
          LIBS DarkAngelAssets DarkAngelEditorService nlohmann_json::nlohmann_json
          ARGS "${PROJECT_BINARY_DIR}/authoring-fixture.json")
        set_tests_properties(M5.composer_structure PROPERTIES FIXTURES_REQUIRED NativeAuthoringFixture TIMEOUT 60 LABELS "M5;editor;asset")
        dae_test(ComposerPreviewTests NAME M5.composer_preview
          SOURCES tests/composer_preview_tests.cpp apps/editor/character_preview.cpp apps/editor/inventory_resources.cpp apps/editor/native_authoring.cpp
          LIBS AshenRootsCombat DarkAngelAssets DarkAngelCharacterScene DarkAngelEditorService nlohmann_json::nlohmann_json
          ARGS "${PROJECT_BINARY_DIR}/authoring-fixture.json")
        dae_test(NativeAuthoringTests NAME M5.native_authoring
          SOURCES tests/native_authoring_tests.cpp
          LIBS NativeAuthoringCommandSupport AshenRootsCombat DarkAngelAssets DarkAngelCharacterScene DarkAngelEditorService nlohmann_json::nlohmann_json unofficial::sqlite3::sqlite3
          DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}"
          ARGS --fixture-only)
        set_tests_properties(M5.native_authoring PROPERTIES FIXTURES_SETUP NativeAuthoringFixture)

        add_library(NativeAuthoringCommandSupport STATIC apps/editor/native_authoring.cpp apps/editor/character_preview.cpp apps/editor/inventory_resources.cpp tests/native_authoring_fixture.cpp tests/native_authoring_commands_tests.cpp tests/native_authoring_graph_commands_tests.cpp tests/native_authoring_effect_preview_tests.cpp)
        target_link_libraries(NativeAuthoringCommandSupport PUBLIC AshenRootsCombat DarkAngelAssets DarkAngelCharacterScene DarkAngelEditorService nlohmann_json::nlohmann_json unofficial::sqlite3::sqlite3)
        target_compile_definitions(NativeAuthoringCommandSupport PRIVATE DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
        dae_test(NativeAuthoringCommandsTests NAME M5.native_authoring_commands
          SOURCES tests/native_authoring_suite_main.cpp
          LIBS NativeAuthoringCommandSupport
          DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}" DAE_NATIVE_SUITE=run_native_authoring_commands)
        set_tests_properties(M5.native_authoring_commands PROPERTIES TIMEOUT 300 LABELS "M5;editor;asset")
        dae_test(NativeAuthoringGraphCommandsTests NAME M5.native_authoring_graph_commands
          SOURCES tests/native_authoring_suite_main.cpp
          LIBS NativeAuthoringCommandSupport
          DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}" DAE_NATIVE_SUITE=run_native_authoring_graph_commands)
        set_tests_properties(M5.native_authoring_graph_commands PROPERTIES TIMEOUT 300 LABELS "M5;editor;asset")
        dae_test(NativeAuthoringEffectPreviewTests NAME M5.native_authoring_effect_preview
          SOURCES tests/native_authoring_suite_main.cpp
          LIBS NativeAuthoringCommandSupport
          DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}" DAE_NATIVE_SUITE=run_native_authoring_effect_preview)
        set_tests_properties(M5.native_authoring_effect_preview PROPERTIES TIMEOUT 300 LABELS "M5;editor;asset")

        set_tests_properties(M5.composer_preview PROPERTIES FIXTURES_REQUIRED NativeAuthoringFixture TIMEOUT 30 LABELS "M5;editor;animation")
        dae_test(RoyalCollisionTests NAME M4.royal_mesh_collision
          SOURCES tests/royal_collision_tests.cpp apps/editor/character_preview.cpp apps/editor/inventory_resources.cpp apps/editor/native_authoring.cpp
          LIBS AshenRootsCombat DarkAngelAssets DarkAngelCharacterScene DarkAngelEditorService nlohmann_json::nlohmann_json
          DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
        dae_test(CharacterCombatSceneTests NAME M5.character_combat_scene
          SOURCES tests/character_combat_scene_tests.cpp
          LIBS AshenRootsCombat DarkAngelAssets DarkAngelCharacterScene nlohmann_json::nlohmann_json
          DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
        dae_test(PartySceneTests NAME M8.party_scene
          SOURCES tests/party_scene_tests.cpp
          LIBS DarkAngelAssets DarkAngelCharacterScene nlohmann_json::nlohmann_json
          DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
        set_tests_properties(M8.party_scene PROPERTIES TIMEOUT 120 LABELS "M8;network")
        dae_test(SceneDelayTests NAME M8.scene_delay
          SOURCES tests/scene_delay_tests.cpp
          LIBS DarkAngelAssets DarkAngelCharacterScene nlohmann_json::nlohmann_json
          DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
        set_tests_properties(M8.scene_delay PROPERTIES TIMEOUT 120 LABELS "M8;network")
        dae_test(SceneTransitionTests NAME M8.scene_transition
          SOURCES tests/scene_transition_tests.cpp
          LIBS DarkAngelAssets DarkAngelCharacterScene nlohmann_json::nlohmann_json
          DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
        set_tests_properties(M8.scene_transition PROPERTIES TIMEOUT 120 LABELS "M8;network")
        if(TARGET DarkAngelNavigation)
          dae_test(NavigationTests NAME M6.navigation
            SOURCES tests/navigation_tests.cpp
            LIBS AshenRootsCombat DarkAngelAssets DarkAngelCharacterScene DarkAngelNavigation nlohmann_json::nlohmann_json
            DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
        endif()
        if(TARGET DarkAngelBehavior)
          dae_test(BehaviorTreeTests NAME M6.behavior_tree
            SOURCES tests/behavior_tree_tests.cpp
            LIBS AshenRootsCombat DarkAngelAssets DarkAngelCharacterScene DarkAngelBehavior nlohmann_json::nlohmann_json
            DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
        endif()
        dae_test(ObservedCharacterPoseTests NAME M5.observed_character_pose
          SOURCES tests/observed_character_pose_tests.cpp
          LIBS AshenRootsCombat DarkAngelCharacterScene DarkAngelAssets nlohmann_json::nlohmann_json
          DEFINITIONS DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
        set_tests_properties(M5.observed_character_pose PROPERTIES FIXTURES_REQUIRED NativeAuthoringFixture TIMEOUT 30 LABELS "M5;animation;presentation")
        dae_test(ReactionPresentationTests NAME M5.reaction_presentation
          SOURCES tests/reaction_presentation_tests.cpp
          LIBS AshenRootsCombat DarkAngelCharacterScene DarkAngelAssets nlohmann_json::nlohmann_json
          DEFINITIONS DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
        set_tests_properties(M5.reaction_presentation PROPERTIES FIXTURES_REQUIRED NativeAuthoringFixture TIMEOUT 30 LABELS "M5;animation;presentation;effects")
        dae_test(RoyalHeavySceneTests NAME M5.royal_heavy_scene
          SOURCES tests/royal_heavy_scene_tests.cpp
          LIBS AshenRootsCombat DarkAngelAssets nlohmann_json::nlohmann_json
          DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
        dae_test(CharacterSceneTests NAME M5.character_scene
          SOURCES tests/character_scene_tests.cpp
          LIBS DarkAngelAssets DarkAngelCharacterScene
          DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
        set_tests_properties(M5.character_scene PROPERTIES TIMEOUT 60 LABELS "M5;animation;physics;network")
      endif()
      dae_test(JointMaskTests NAME M5.joint_masks
        SOURCES tests/joint_mask_tests.cpp
        LIBS DarkAngelAssets DarkAngelAnimation DarkAngelEditorService nlohmann_json::nlohmann_json
        DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
      set_tests_properties(M5.joint_masks PROPERTIES TIMEOUT 60 LABELS "M5;animation;asset;authoring")
      dae_test(RenderableHumanTests NAME M5.renderable_human
        SOURCES tests/renderable_human_tests.cpp
        LIBS DarkAngelAssets DarkAngelAnimation nlohmann_json::nlohmann_json
        DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
      set_tests_properties(M5.renderable_human PROPERTIES TIMEOUT 60 LABELS "M5;animation;asset")
      set_tests_properties(M5.canonical_rig PROPERTIES TIMEOUT 30 LABELS "M5;animation;asset")
    endif()
    dae_test(EffectAssetTests NAME M5.effect_assets
      SOURCES tests/effect_asset_tests.cpp
      LIBS DarkAngelAssets DarkAngelRuntime nlohmann_json::nlohmann_json)
    dae_test(AbilityAssetTests NAME M5.ability_assets
      SOURCES tests/ability_asset_tests.cpp
      LIBS DarkAngelAssets DarkAngelRuntime nlohmann_json::nlohmann_json
      DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
    if(DAE_PHYSICS)
      dae_test(CollisionAssetTests NAME M4.collision_assets
        SOURCES tests/collision_asset_tests.cpp
        LIBS DarkAngelAssets DarkAngelPhysics DarkAngelEditorService nlohmann_json::nlohmann_json
        DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
      if(DAE_ANIMATION)
        dae_test(ActionClipBindingTests NAME M5.action_clip_binding
          SOURCES tests/action_clip_binding_tests.cpp
          LIBS DarkAngelAssets DarkAngelAnimation DarkAngelPhysics nlohmann_json::nlohmann_json
          DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
      endif()
      dae_test(ActionTimelineTests NAME M5.action_timeline
        SOURCES tests/action_timeline_tests.cpp
        LIBS DarkAngelAssets DarkAngelPhysics DarkAngelEditorService nlohmann_json::nlohmann_json
        DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
      dae_test(ModelCollisionTests NAME M4.model_collision
        SOURCES tests/model_collision_tests.cpp
        LIBS DarkAngelAssets DarkAngelPhysics nlohmann_json::nlohmann_json
        DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
      dae_test(CollisionMeshTests NAME M4.collision_meshes
        SOURCES tests/collision_mesh_tests.cpp
        LIBS DarkAngelAssets DarkAngelPhysics DarkAngelEditorService nlohmann_json::nlohmann_json
        DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
      dae_test(CollisionStreamTests NAME M4.collision_stream
        SOURCES tests/collision_stream_tests.cpp
        LIBS DarkAngelRuntime DarkAngelPhysics
        DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
      dae_test(CollisionObserverTests NAME M4.collision_observer
        SOURCES tests/collision_observer_tests.cpp
        LIBS DarkAngelRuntime)
      set_tests_properties(M4.collision_assets PROPERTIES TIMEOUT 30 LABELS "M4;asset")
    endif()
    add_executable(AssetTests tests/asset_tests.cpp)
    target_link_libraries(AssetTests PRIVATE DarkAngelAssets)
    target_compile_definitions(AssetTests PRIVATE DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
    target_compile_options(AssetTests PRIVATE /W4 /utf-8 /EHsc)
    add_test(NAME M2.asset_pipeline COMMAND AssetTests)
    add_executable(ExternalImportTests tests/external_import_tests.cpp)
    target_link_libraries(ExternalImportTests PRIVATE DarkAngelAssets nlohmann_json::nlohmann_json)
    target_compile_definitions(ExternalImportTests PRIVATE DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
    if(DAE_ANIMATION)
      target_compile_definitions(ExternalImportTests PRIVATE DAE_ANIMATION=1)
      target_link_libraries(ExternalImportTests PRIVATE DarkAngelAnimation)
    endif()
    add_test(NAME M5.external_import COMMAND ExternalImportTests)
    dae_test(AttributeTests NAME M5.attributes
      SOURCES tests/attribute_tests.cpp
      LIBS DarkAngelRuntime)
    dae_test(CombatKitTests NAME M5.combat_kit
      SOURCES tests/combat_kit_tests.cpp
      LIBS DarkAngelRuntime)
    set_tests_properties(M5.attributes M5.combat_kit PROPERTIES TIMEOUT 30 LABELS "M5;combat;foundation")
    dae_test(InputTests NAME M4.input_actions
      SOURCES tests/input_tests.cpp
      LIBS DarkAngelAssets DarkAngelEditorService nlohmann_json::nlohmann_json
      DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
    dae_test(CueAssetTests NAME M7.cue_assets
      SOURCES tests/cue_asset_tests.cpp
      LIBS DarkAngelAssets DarkAngelRuntime nlohmann_json::nlohmann_json
      DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
    dae_test(PresentationPrimitiveTests NAME M7.presentation_primitives
      SOURCES tests/presentation_primitive_tests.cpp
      LIBS DarkAngelAssets DarkAngelRuntime nlohmann_json::nlohmann_json
      DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
    set_tests_properties(M7.cue_assets PROPERTIES TIMEOUT 30 LABELS "M7;cue;asset")
    dae_test(VfxAssetTests NAME M7.vfx_assets
      SOURCES tests/vfx_asset_tests.cpp
      LIBS DarkAngelAssets
      DEFINITIONS DAE_SOURCE_DIR="${PROJECT_SOURCE_DIR}" DAE_BINARY_DIR="${PROJECT_BINARY_DIR}")
    set_tests_properties(M7.vfx_assets PROPERTIES TIMEOUT 300 LABELS "M7;vfx;asset")
    set_tests_properties(M4.input_actions PROPERTIES TIMEOUT 30 LABELS "M4;input;asset;editor")
    set_tests_properties(M2.asset_pipeline PROPERTIES TIMEOUT 30 LABELS "M2;asset")
    if(EXISTS "${PROJECT_SOURCE_DIR}/.cache/fixtures/horned-mask.glb")
      add_test(NAME M2.real_prop COMMAND AssetTests "${PROJECT_SOURCE_DIR}/.cache/fixtures/horned-mask.glb")
      set_tests_properties(M2.real_prop PROPERTIES TIMEOUT 30 LABELS "M2;asset;local-fixture")
    endif()
  endif()
endif()
