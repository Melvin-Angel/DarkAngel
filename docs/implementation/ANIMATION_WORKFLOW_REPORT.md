# Initial Animation workflow

9 October 2026, after d14d2ad. M4/M5 remain In progress; live EOS remains externally blocked.

Delivered: a fixed Animation destination reuses the native action browser, Composer timing/structure/history and isolated clip/character preview. Its focused two-panel arrangement leaves kit composition in Ability. Edit action timeline in Ability navigates to Animation with the same draft and related-ability context; Animation links back. Initial selection resolves the chosen ability or assigned kit action without duplicating shared definitions. All gameplay Play/Stop/Pause/Step remain in Game. Launch with `powershell -ExecutionPolicy Bypass -File scripts/open_royal_scene.ps1 -AnimationWorkflow`.

Focused verification: `python scripts/verify_composer.py --animation --build` passes full-editor build and one D3D12 scripted workflow capture. An action timing draft survives an actual rendered Ability frame and navigation back to Animation; Undo restores it, sources/world remain unchanged and tick20 samples81 canonical joints without gameplay. `evidence/animation-workflow.json` records the scope/hashes; the capture was inspected. No physical input, provider/second-backend/performance or full milestone matrix is claimed.

This is the initial Action Composer/clip preview surface, not complete animation graph/state/transition/layer/mask/skeleton authoring. The shell still uses focused fixed panel arrangements; persistent configurable workflow docking/layouts remain planned. Source/Save/crash recovery and asset creation/deletion Undo retain their existing limitations.

Next: attribute definitions and ordinary modifier/status authoring through the native source/consumer/cook path, followed by compatible picker refinements. Keep generated gameplay-tag definitions behind their native generation/rebuilt-consumer gates. Character/Player and full M4/M5 integration remain open.
