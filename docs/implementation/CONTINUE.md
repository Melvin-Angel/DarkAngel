# Development continuation

Updated 8 October 2026 in `C:\DarkAngel`. **M3 is blocked only on live EOS acceptance for the initial milestone profile; M4 has not started.** Preserve work, commit coherent local checkpoints, and never push (no remote). These user instructions supersede the pasted handoff's older no-commit instruction.

Read STATUS.md, M3_REPORT.md, M3_ONLINE_ACCEPTANCE.md, the implementation handoff and DAE-009/014. All independent work is committed through `a447242`; the following evidence/status checkpoint contains final receipts. Earlier checkpoints: `bf0254c`, `09617d5`, `0bdc017`, `06a7c15`. Use `git log -6 --oneline` to find the latest documentation commit.

Final acceptance passed on current sources:
- `python scripts/verify_m3.py`: optimized 26/26 native tests, separate GNS UDP host/client processes, official SDK MCP flow, Shipping configure/build/run/target exclusion and rejection of Shipping with authoring/agent enabled.
- `python scripts/verify_m2.py`: 28/28 editor/asset/foundation tests, glTF validator, cook/package, D3D12/Vulkan captures, UI/CLI normalized transaction equivalence and cooked Headless.
- `python scripts/verify_m3_editor.py`: current D3D12/Vulkan and 960x720 captures plus actual native editor scoped pipe/MCP transaction/retry/shared undo. Captures were inspected; Inspector values remain readable.
- `python scripts/record_m3.py`: exact current source/binary/evidence hashes. Do not use the older record_implementation.py for M3; it assumes historical test counts and would overwrite the current receipts.

Remaining input is an Epic product/sandbox/deployment, public-client policy/ID/secret permitting Connect/Lobbies/P2P, selected external identity provider, and two authorized tokens. Keep private inputs in ignored `.cache/online` or user-data storage; do not paste secrets into chat or commit them. An earlier asynchronous setup question received no answer. SDK and OnlineProbe are already built. Follow M3_ONLINE_ACCEPTANCE.md to run host and exact-lobby client and qualify admission/content mismatch, nonmember rejection, expiry/refresh, reconnect and relay. SDK smoke/compilation is not online acceptance. Do not mark M3 Verified without those service checks.

The pinned MCP SDK 1.32.1 actually negotiates 2025-11-25, asserted in the real SDK acceptance. This is the tested older-host compatibility profile permitted by DAE-014; its target 2026-07-28 is unsupported by the frozen pin and remains unverified. No dependency upgrade is authorized. Physical LAN/poor-network production qualification, larger world/scripts/resources, deltas and future registered agent jobs remain explicitly outside the verified initial slice.

Known build fixes are committed: GNS's static-protobuf import selection is a checksum-verified local vcpkg overlay, with exact exception record in cmake/m3-gns-port-exception.json. M3 dependency buildtrees live outside the workspace under `%LOCALAPPDATA%/DarkAngel/vcpkg-buildtrees` because VS Code C# tooling locked protobuf extraction files. No upstream extraction patch or user-process termination was used. MSVC activation uses a unique owned temporary script. EOS initialize is process-wide and shutdown final; ordinary Headless/Loopback/GNS never initialize EOS.

Five-hour usage was checked at this final checkpoint: 10% consumed, 90% remaining; weekly 12% consumed. The five-hour window reset during this continuation; these are historical readings, query fresh limits on resume. No reset credit was consumed.

After real required M3 acceptance succeeds, update reports/evidence and **commit before M4**. The user explicitly asks to start M4 in a new chat if possible. Call list_projects and create_thread with the existing DarkAngel project, local environment, a clear M4 prompt grounded in its handoff/DAE contracts, and references to the committed M3 receipts. Until then, continue M3 here and do not create a premature M4 task.
