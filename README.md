# DarkAngel Engine

Native C++20 engine and personal game-development editor for Ashen Roots. M0–M2 have verified initial-profile gates. M3 offline session infrastructure has scoped evidence while live EOS qualification remains externally blocked. M4/M5 are In progress; native combat/effects/status replication and the first Ability/Effect/action authoring and fresh-Play loop have scoped receipts. M6–M9 remain Not started. See [current status](docs/implementation/STATUS.md) for exact boundaries.

[Final engine vision](docs/vision/DarkAngel_Engine_Vision.md) defines the finished product and fifteen workflow targets. The [authoritative M0–M9 roadmap](docs/architecture/DarkAngel_Implementation_Handoff.md) defines delivery/acceptance, [locked architecture](docs/architecture/Decision_Log.txt) defines contracts, and [editor workflow plan](docs/implementation/EDITOR_WORKFLOW_PLAN.md) defines incremental authoring. Start current authoring work with [ability continuation](docs/implementation/ABILITY_AUTHORING_CONTINUE.md). Planned capabilities are not implementation claims. [Actor gameplay decisions and Ashen Roots traceability](docs/architecture/Actor_Gameplay_Design.md) records Character/Player/NPC ownership, mask grants, graph/reaction direction and unanswered design questions. [Current continuation](docs/implementation/CONTINUE.md) preserves the local checkpoint and next authoring priorities.

[Latest authoring checkpoint and usage handoff](docs/implementation/AUTHORING_USAGE_HANDOFF.md) records native Character/Player/input references, graph authoring and isolated previews, focused receipts and the next increment.

Start with [bootstrap instructions](docs/implementation/BOOTSTRAP.md), [current status](docs/implementation/STATUS.md) and the [M0 report](docs/implementation/M0_REPORT.md). All dependencies and tools are frozen in checked-in manifests/locks; bootstrap never resolves a moving branch or updates to a newer release.

Targets: static `DarkAngelFoundation`, static `DarkAngelRuntime`, console `DarkAngelHeadless`, `FlecsProbe`, `LuauProbe` and `SQLiteProbe`. Headless does not link the acquired renderer, editor, audio or networking SDKs.

Run `python scripts/verify_m1.py --profile m1-debug` (also `m1-relwithdebinfo` and `m1-release`) for native configure/build/CTest receipts. Run `python scripts/verify_m2.py` for the asset/device checks; provide `--fbx PATH` to convert one static prop with the installed Blender and test cooked-only graphical viewports. See [M1 report](docs/implementation/M1_REPORT.md), [M2 report](docs/implementation/M2_REPORT.md) and [JSON choice](docs/implementation/M1_JSON_CHOICE.md). Historical M0 presets remain usable and now resolve the Foundation's M1 dependency feature as well.

Publishing is outside the current task. Engine source licensing remains a product decision. Shipping and full game-production qualification remain roadmap gates.
