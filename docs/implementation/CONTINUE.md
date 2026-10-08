# Development continuation

Updated 8 October 2026. Work in `C:\DarkAngel`. Commit coherent checkpoints locally; never push (no remote). Preserve existing work. Finish M3 and editor appearance before M4. Commit the completed M3 boundary before starting M4; the user requests a new chat for M4 when possible.

Read STATUS.md, M3_REPORT.md, the implementation handoff, and DAE-009/014 in the architecture decision log. M3 is still in progress; do not mark it verified based on compilation or offline tests. EOS live login/lobbies/P2P requires a configured Epic deployment/public-client policy and supported identity provider. An asynchronous question for that setup has been issued. No credentials are committed.

Checkpoints: `bf0254c` preserves incoming M1/M2 reports and Loopback cleanup; `09617d5` adds bounded authoritative WorldSession and atomic chunked late join. Native agent Debug tests and real MCP/pipe flow passed. Theme/editor and SDK provider integration are in progress; verify current sources and receipts before claiming gates.

Useful commands: `python scripts/check_native.py --targets SessionTests`; `python scripts/check_native.py --targets AgentTests AgentHost --tests '^M3.agent' --receipt m3-agent`; pinned `.tools/node/node-v24.21.0-win-x64/node.exe tools/agent-bridge/verify.mjs`. Final integration should run the appropriate full acceptance once and inspect D3D12/Vulkan captures. Add accurate report/evidence and checkpoint commits as each slice passes.

The M3 preset keeps vcpkg buildtrees under `%LOCALAPPDATA%/DarkAngel/vcpkg-buildtrees`: VS Code C# tooling auto-loaded extracted protobuf C# projects inside `.tools/vcpkg/buildtrees`, produced bin/obj files and prevented CMake renames. Moving dependency buildtrees outside the workspace resolves that contention without altering sources/pins or terminating the user's apps. No vcpkg extraction patch remains.

Usage was checked during work: five-hour window 33% consumed (67% remaining) at this checkpoint. Query current limits again; these values are a historical checkpoint, not a guarantee. Keep this note updated before a limit interruption. No M4 chat has been created because M3 is not complete.
