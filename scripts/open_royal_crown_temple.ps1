param(
    [ValidateSet('d3d12','vulkan')][string]$Backend = 'd3d12'
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $taskRoot
$taskEditor = Join-Path $taskRoot 'build/m5-editor-relwithdebinfo/DarkAngelEditor.exe'
if (!(Test-Path -LiteralPath $taskEditor)) { throw 'Build the m5-editor-relwithdebinfo DarkAngelEditor target first.' }
python scripts/import_royal_crown_temple.py
if ($LASTEXITCODE -ne 0) { throw 'Royal Crown Temple import/package preparation failed.' }
$taskModel = (Get-Content -LiteralPath 'content/royal_crown/models/rc_terrain_ground_32x32.glb.daimport' -Raw | ConvertFrom-Json).id
$taskArguments = @(
    '--registry','.cache/royal-crown-temple/registry.json',
    '--cas','.cache/royal-crown-temple/cas',
    '--model',$taskModel,
    '--scene','content/royal_crown/RoyalCrownTemple.dascene',
    '--backend',$Backend,
    '--sources','content',
    '--asset-cache','.cache/editor-assets'
)
& $taskEditor @taskArguments
exit $LASTEXITCODE
