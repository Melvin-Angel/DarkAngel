[CmdletBinding()]
param(
    [ValidateSet('Detect', 'Acquire', 'Verify', 'All')][string]$Mode = 'Detect',
    [ValidateSet('m0-debug', 'm0-relwithdebinfo', 'm0-release')][string]$Profile = 'm0-relwithdebinfo'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$originalPath = $env:PATH
$originalConcurrency = $env:VCPKG_MAX_CONCURRENCY
$originalLocation = Get-Location
$steps = [System.Collections.Generic.List[object]]::new()
$failed = $false
function Invoke-Step([string]$Name, [string]$Executable, [string[]]$Arguments) {
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $log = Join-Path $root ".cache/evidence/$Name.log"
    $code = 1
    try {
        & $Executable @Arguments *> $log
        $code = $LASTEXITCODE
    } catch { $_ | Out-String | Add-Content -LiteralPath $log }
    $timer.Stop()
    $steps.Add([ordered]@{step=$Name; command=(@($Executable)+$Arguments); exit_code=$code; seconds=$timer.Elapsed.TotalSeconds; log=".cache/evidence/$Name.log"})
    Write-Host "$Name exit=$code seconds=$([Math]::Round($timer.Elapsed.TotalSeconds,2))"
    if ($code -ne 0) { $script:failed=$true; Get-Content -LiteralPath $log -Tail 8 | Write-Host }
}
try {
    Set-Location -LiteralPath $root
    $env:VCPKG_MAX_CONCURRENCY = '2'
    New-Item -ItemType Directory -Force -Path .cache/evidence | Out-Null
    $python = (Get-Command python -ErrorAction Stop).Source
    if ($Mode -in @('Acquire','All')) {
        Invoke-Step 'tools' $python @('scripts/acquire.py','tools')
        $env:PATH = "$root/.tools/cmake/cmake-4.4.4-windows-x86_64/bin;$root/.tools/ninja;$root/.tools/node/node-v24.21.0-win-x64;$originalPath"
        Invoke-Step 'vcpkg-checkout' $python @('scripts/acquire.py','vcpkg')
        if (-not (Test-Path .tools/vcpkg/vcpkg.exe)) {
            Invoke-Step 'vcpkg-bootstrap' "$root/.tools/vcpkg/bootstrap-vcpkg.bat" @('-disableMetrics')
        }
        Invoke-Step 'vcpkg-tool-pin' $python @('scripts/check_vcpkg.py')
        Invoke-Step 'sources' $python @('scripts/acquire.py','sources')
        Invoke-Step 'port-assets' $python @('scripts/acquire.py','port-assets')
        $node = "$root/.tools/node/node-v24.21.0-win-x64/node.exe"
        $npm = "$root/.tools/node/node-v24.21.0-win-x64/node_modules/npm/bin/npm-cli.js"
        foreach ($folder in @('tools/agent-bridge','tools/asset-validation')) {
            Invoke-Step ($folder.Split('/')[-1] + '-npm-ci') $node @($npm,'ci','--prefix',$folder,'--ignore-scripts','--no-audit','--no-fund')
        }
        Invoke-Step 'candidate-python-wheels' $python @('scripts/acquire_candidate_wheels.py')
        Invoke-Step 'full-vcpkg-prefetch' $python @('scripts/prefetch.py')
    }
    Invoke-Step 'detection' $python @('scripts/detect.py')
    if ($Mode -in @('Verify','All')) {
        Invoke-Step 'independent-checks' $python @('scripts/verify_inputs.py')
        Invoke-Step 'native-verification' $python @('scripts/verify_native.py','--profile',$Profile)
    }
} finally {
    $steps | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root ".cache/bootstrap-$Mode.json")
    $env:PATH = $originalPath
    $env:VCPKG_MAX_CONCURRENCY = $originalConcurrency
    Set-Location -LiteralPath $originalLocation
}
if ($failed) { exit 1 }
