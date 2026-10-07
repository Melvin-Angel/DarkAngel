# DarkAngel Engine

Native C++20 engine for Ashen Roots. This repository stops at M0: dependency/tool bootstrap, small headless console shell, real dependency probes and packaging verification. The supplied decision log and implementation handoff in `docs/architecture/` govern later milestones.

Start with [bootstrap instructions](docs/implementation/BOOTSTRAP.md), [current status](docs/implementation/STATUS.md) and the [M0 report](docs/implementation/M0_REPORT.md). All dependencies and tools are frozen in checked-in manifests/locks; bootstrap never resolves a moving branch or updates to a newer release.

Targets: static `DarkAngelFoundation`, static `DarkAngelRuntime`, console `DarkAngelHeadless`, `FlecsProbe`, `LuauProbe` and `SQLiteProbe`. Headless does not link the acquired renderer, editor, audio or networking SDKs.

No remote repository, publishing, gameplay implementation or native plugin ABI is included. Engine source licensing remains a product decision.
