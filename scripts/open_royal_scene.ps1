param(
    [ValidateSet('d3d12','vulkan')][string]$Backend = 'd3d12',
    [switch]$CloseView,
    [switch]$AttackPose
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $taskRoot
$taskEditor = Join-Path $taskRoot 'build/m5-editor-relwithdebinfo/DarkAngelEditor.exe'
if (!(Test-Path -LiteralPath $taskEditor)) { throw 'Build the m5-editor-relwithdebinfo DarkAngelEditor target first.' }
$taskRegistryReady = Test-Path -LiteralPath '.cache/royal-scene/registry.json'
if ($taskRegistryReady) {
    $taskRegistryReady = @((Get-Content -LiteralPath '.cache/royal-scene/registry.json' -Raw | ConvertFrom-Json).assets | Where-Object { $_.kind -eq 'combat_kit' }).Count -eq 1
}
if (!$taskRegistryReady) {
    python scripts/prepare_royal_scene.py
    if ($LASTEXITCODE -ne 0) { throw 'Royal scene preparation failed.' }
}
$taskModel = (Get-Content -LiteralPath 'content/royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport' -Raw | ConvertFrom-Json).id
$taskArguments = @('--registry','.cache/royal-scene/registry.json','--cas','.cache/royal-scene/cas','--model',$taskModel,'--scene','content/royal_district/RoyalCombat.dascene','--backend',$Backend,'--sources','content','--asset-cache','.cache/editor-assets')
$taskKit = (Get-Content -LiteralPath 'content/royal_district/combat/player.dakit' -Raw | ConvertFrom-Json).asset
$taskArguments += @('--character-kit',$taskKit,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005')
if ($CloseView) { $taskArguments += '--camera-close' }
if ($AttackPose) {
    $taskClip = (Get-Content -LiteralPath 'content/royal_district/clips/attack.glb.daimport' -Raw | ConvertFrom-Json).id
    $taskArguments += @('--pose-clip',$taskClip,'--pose-tick','18')
}
& $taskEditor @taskArguments
exit $LASTEXITCODE
