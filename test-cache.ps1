$ErrorActionPreference = 'Stop'
foreach ($arch in @('x86', 'x64')) {
    $test = Join-Path $PSScriptRoot "build\$arch\test_cache.exe"
    if (-not (Test-Path -LiteralPath $test)) { throw 'Run build.ps1 first.' }
    Write-Host "Cache/transport regression tests: $arch (no physical USB access)"
    & $test
    if ($LASTEXITCODE) { throw "Regression test failed: $arch, exit $LASTEXITCODE" }
}
