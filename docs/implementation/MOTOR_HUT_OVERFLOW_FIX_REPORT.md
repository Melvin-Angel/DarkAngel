# Bounded hut contact fix - 10 October 2026

Enabled Jolt CharacterVirtual enhanced internal-edge removal in the shared CharacterMotor constructor. Triangle seam contacts are filtered before bounded collection. The hit cap remains32 and GetMaxHitsExceeded still fails/resynchronizes; no vendor source changes or overflow suppression. Authoritative, prediction and disposable replay worlds use this same constructor.

Before: the native actual-mesh probe reproduces approach1 at tick88. After: all12 approaches complete360 ticks, requesting Jump every3 ticks. The probe now additionally replays each15-command batch in a disposable world:288 batches match live XYZ within1e-5 metres and grounded state exactly, without changing live state. Run `build/m5-editor-relwithdebinfo/MotorTests.exe --hut-jump content/royal_district/collision/environment.dacollision`; exit0 is success, exit2 is overflow reproduction.

Focused rebuilt checks pass: MotorTests, MotorSessionTests (existing WorldSession ownership/input path), MotorFaultTests, MotorDynamicTests, MotorActorHistoryTests, MotorBudgetTests and AbilityMotorPredictionTests. [Regression output](evidence/hut-edge-regressions.log), [build](evidence/hut-edge-regression-build.log). Physics budgets remain bounded; this is scoped regression evidence, not full mesh/performance qualification.

The actual DarkAngelEditor was rebuilt. D3D12 scripted combat finishes tick120 with target Health50, Stamina80 and prediction pending0; [log](evidence/hut-edge-d3d12.log), [capture](evidence/hut-edge-d3d12.png). Capture inspected. The graphical check is ordinary combat smoke, not physical keyboard reproduction at the failing hut. User should retest the repeated-jump approach interactively.

User imported clips, characters and scene records remain untouched. No push, dependencies, external assets or subagents. M4/M5 remain In progress; EOS blocked; M6–M9 not started. Next: native Ability action-window tag authoring; history remains MVP.
