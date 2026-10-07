# DarkAngel Engine

Native C++20 engine for Ashen Roots. M0 is verified. M1 implements metadata, Flecs-backed worlds, versioned scenes, strict Luau module packages, declared state, staged reloads and bounded tasks. M2 now includes a SQLite asset catalog, static glTF cooker, immutable cooked registry/CAS and a Diligent D3D12/Vulkan editor viewport with native property transactions. M1/M2 handoff gates are verified for the supported profile. M3 has started with the shared transport interface and bounded LocalLoopback adapter. Broader production features and remaining M3 work are tracked in [status](docs/implementation/STATUS.md).

Start with [bootstrap instructions](docs/implementation/BOOTSTRAP.md), [current status](docs/implementation/STATUS.md) and the [M0 report](docs/implementation/M0_REPORT.md). All dependencies and tools are frozen in checked-in manifests/locks; bootstrap never resolves a moving branch or updates to a newer release.

Targets: static `DarkAngelFoundation`, static `DarkAngelRuntime`, console `DarkAngelHeadless`, `FlecsProbe`, `LuauProbe` and `SQLiteProbe`. Headless does not link the acquired renderer, editor, audio or networking SDKs.

Run `python scripts/verify_m1.py --profile m1-debug` (also `m1-relwithdebinfo` and `m1-release`) for native configure/build/CTest receipts. Run `python scripts/verify_m2.py` for the asset/device checks; provide `--fbx PATH` to convert one static prop with the installed Blender and test cooked-only graphical viewports. See [M1 report](docs/implementation/M1_REPORT.md), [M2 report](docs/implementation/M2_REPORT.md) and [JSON choice](docs/implementation/M1_JSON_CHOICE.md). Historical M0 presets remain usable and now resolve the Foundation's M1 dependency feature as well.

No remote repository, publishing, gameplay implementation or native plugin ABI is included. Engine source licensing remains a product decision.
