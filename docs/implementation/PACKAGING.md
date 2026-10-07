# M0 console package

The HeadlessShell component contains only the console bootstrap and its explicit metadata inventory. Probe executables and acquired SDKs/tools are excluded. The shell links static Foundation/Runtime libraries with /MD in optimized builds. The tested toolset is MSVC 14.51.36231; the launch machine has Microsoft Visual C++ x64 runtime DLLs 14.51.36247.0. Install a compatible serviced x64 Visual C++ Redistributable before running on another machine. No redistributable or development kit is included in this ZIP. Clean-machine qualification remains a later release gate. Do not redistribute Debug packages or debug CRT DLLs.

This package is an M0 smoke artifact. Release is an optimization configuration. Shipping is a separate feature policy that rejects authoring and agent endpoints. No later production targets or game content exist in M0.
