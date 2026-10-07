# M0 console package

The HeadlessShell component contains only the console bootstrap and its explicit metadata inventory. Probe executables and acquired SDKs/tools are excluded. The shell links static Foundation/Runtime libraries with /MD in optimized builds. It requires the Microsoft Visual C++ x64 Redistributable for the exact tested compiler; native compilation is currently blocked, so that runtime version is not yet qualified. Do not redistribute Debug packages or debug CRT DLLs.

This package is an M0 smoke artifact. Release is an optimization configuration. Shipping is a separate feature policy that rejects authoring and agent endpoints. No later production targets or game content exist in M0.
