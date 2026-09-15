param(
    [ValidateSet('x64', 'x86')]
    [string]$Architecture = 'x64'
)

$ErrorActionPreference = 'Stop'
$dll = Join-Path $PSScriptRoot "build/$Architecture/MSXGr.dll"
$hostExe = Join-Path $PSScriptRoot "build/$Architecture/test_gradius.exe"
if (-not (Test-Path $dll) -or -not (Test-Path $hostExe)) {
    & (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
& $hostExe $dll
exit $LASTEXITCODE
