# R2 source surface index

15 October 2026. Literal window, child, popup and combo entry points from the inspected editor source. This is a completeness aid for the workspace audit, not proof that every operation was exercised. Dynamic-titled authoring workspaces and reflected sections are described in the [R2 report](EDITOR_UX_OVERHAUL_REPORT.md). Each retained specialized adapter still needs its representative task receipt before broad R2 acceptance.

| Source | Entry | Label / stable ID |
| --- | --- | --- |
| `apps/editor/audio_workspace.cpp:230` | BeginChild | Audio sounds |
| `apps/editor/audio_workspace.cpp:332` | BeginCombo | Bus##sound |
| `apps/editor/audio_workspace.cpp:364` | BeginCombo | Effect##sound |
| `apps/editor/audio_workspace.cpp:416` | BeginChild | Audio mixer |
| `apps/editor/audio_workspace.cpp:508` | BeginChild | Audio profiles |
| `apps/editor/audio_workspace.cpp:548` | BeginCombo | Music##profile |
| `apps/editor/audio_workspace.cpp:572` | BeginCombo | ##add-ambient |
| `apps/editor/editor_assets.cpp:23` | BeginPopupModal | Import asset |
| `apps/editor/editor_assets.cpp:25` | BeginCombo | Item type |
| `apps/editor/editor_assets.cpp:39` | BeginChild | Import files |
| `apps/editor/editor_authoring.cpp:46` | BeginCombo | ##asset-reference |
| `apps/editor/editor_authoring.cpp:50` | BeginChild | ##reference-results |
| `apps/editor/editor_authoring.cpp:76` | Begin | Mask Editor |
| `apps/editor/editor_authoring.cpp:131` | BeginChild | Gameplay browser |
| `apps/editor/editor_authoring.cpp:160` | BeginChild | Gameplay properties |
| `apps/editor/editor_authoring.cpp:167` | BeginChild | Graph source |
| `apps/editor/editor_authoring.cpp:183` | BeginCombo | New selector condition |
| `apps/editor/editor_authoring.cpp:192` | BeginCombo | Inspect blend point |
| `apps/editor/editor_authoring.cpp:194` | BeginChild | Graph preview |
| `apps/editor/editor_authoring.cpp:245` | BeginCombo | Live when |
| `apps/editor/editor_authoring.cpp:246` | BeginCombo | Held input |
| `apps/editor/editor_authoring.cpp:247` | BeginCombo | Gameplay tag |
| `apps/editor/editor_authoring.cpp:285` | BeginCombo | ##add-child |
| `apps/editor/editor_authoring.cpp:298` | BeginCombo | Maximum statistic |
| `apps/editor/editor_authoring.cpp:318` | BeginCombo | Window |
| `apps/editor/editor_authoring.cpp:319` | BeginCombo | Registered tag |
| `apps/editor/editor_authoring.cpp:323` | BeginCombo | Add window tag |
| `apps/editor/editor_authoring.cpp:337` | BeginCombo | Targeting mode |
| `apps/editor/editor_authoring.cpp:348` | BeginCombo | Commit |
| `apps/editor/editor_authoring.cpp:350` | BeginCombo | Hit window |
| `apps/editor/editor_authoring.cpp:352` | BeginCombo | Add hit profile |
| `apps/editor/editor_authoring.cpp:375` | BeginCombo | Add cue |
| `apps/editor/editor_authoring.cpp:401` | BeginCombo | Execution |
| `apps/editor/editor_authoring.cpp:424` | BeginCombo | Inspect block |
| `apps/editor/editor_authoring.cpp:436` | BeginChild | Kit composition |
| `apps/editor/editor_authoring.cpp:448` | BeginCombo | Hit window |
| `apps/editor/editor_authoring.cpp:452` | BeginChild | Input Mapper |
| `apps/editor/editor_authoring.cpp:474` | BeginCombo | Action |
| `apps/editor/editor_items.cpp:18` | Begin | Items workspace |
| `apps/editor/editor_items.cpp:21` | BeginChild | Item list |
| `apps/editor/editor_items.cpp:23` | BeginPopup | New item |
| `apps/editor/editor_items.cpp:28` | BeginChild | Item details |
| `apps/editor/editor_items.cpp:35` | BeginPopupModal | Delete selected item? |
| `apps/editor/editor_ui.cpp:82` | BeginCombo | Audio profile |
| `apps/editor/editor_ui.cpp:97` | BeginCombo | Model |
| `apps/editor/editor_ui.cpp:127` | BeginCombo | Destination scene |
| `apps/editor/editor_ui.cpp:129` | BeginCombo | Entry point |
| `apps/editor/editor_ui.cpp:142` | BeginCombo | ##spawn-encounter |
| `apps/editor/editor_ui.cpp:207` | Begin | Party player |
| `apps/editor/editor_ui.cpp:236` | Begin | Workflow navigation |
| `apps/editor/editor_ui.cpp:242` | Begin | Assets workspace |
| `apps/editor/editor_ui.cpp:245` | Begin | Audio workspace |
| `apps/editor/editor_ui.cpp:248` | Begin | VFX workspace |
| `apps/editor/editor_ui.cpp:249` | BeginChild | VFX effects |
| `apps/editor/editor_ui.cpp:250` | BeginChild | VFX preview |
| `apps/editor/editor_ui.cpp:259` | Begin | Workspace |
| `apps/editor/editor_ui.cpp:264` | Begin | World Outliner |
| `apps/editor/editor_ui.cpp:269` | BeginPopup | Add NPC spawn point |
| `apps/editor/editor_ui.cpp:271` | Begin | Inspector |
| `apps/editor/editor_ui.cpp:279` | BeginCombo | ##spawn-npc |
| `apps/editor/editor_ui.cpp:290` | Begin | Content Browser |
| `apps/editor/editor_ui.cpp:291` | Begin | Console |
| `apps/editor/editor_ui.cpp:294` | Begin | Viewport |
| `apps/editor/editor_ui.cpp:315` | Begin | Place |
| `apps/editor/editor_ui.cpp:324` | BeginCombo | ##place-category |
| `apps/editor/editor_ui.cpp:326` | BeginPopup | Placement options |
| `apps/editor/editor_ui.cpp:326` | BeginCombo | Category |
| `apps/editor/editor_ui.cpp:327` | BeginChild | Place items |
| `apps/editor/editor_workspaces.cpp:27` | BeginChild | Console messages |
| `apps/editor/editor_workspaces.cpp:38` | Begin | Game dockspace |
| `apps/editor/editor_workspaces.cpp:42` | Begin | Game Viewport |
| `apps/editor/editor_workspaces.cpp:50` | Begin | Game Viewport 2 |
| `apps/editor/editor_workspaces.cpp:56` | Begin | Game Profiler |
| `apps/editor/editor_workspaces.cpp:57` | Begin | Gameplay Inspector |
| `apps/editor/editor_workspaces.cpp:58` | Begin | Input Monitor |
| `apps/editor/editor_workspaces.cpp:59` | Begin | Game Console |
| `apps/editor/editor_workspaces.cpp:76` | BeginChild | Authoring history |
| `apps/editor/editor_world_objects.cpp:73` | BeginCombo | Encounter completed |
| `apps/editor/editor_world_objects.cpp:78` | BeginCombo | Scene |
| `apps/editor/editor_world_objects.cpp:80` | BeginCombo | Entry point |
| `apps/editor/play_saves.cpp:44` | BeginPopupModal | Delete the save slot? |
| `apps/editor/runtime_inventory.cpp:52` | BeginCombo | ##grant-item |
| `apps/editor/scene_navigation.cpp:42` | BeginPopup | Scene navigation settings |
