param(
    [string]$ToolchainPath
)

$ErrorActionPreference = 'Stop'

if ($ToolchainPath) {
    $toolchainRoot = (Resolve-Path -LiteralPath $ToolchainPath).Path
} else {
    $workspaceRoot = Split-Path -Parent $PSScriptRoot
    $toolchain = Get-ChildItem (Join-Path $workspaceRoot '.tools') -Directory -Filter 'llvm-mingw-*-ucrt-x86_64' -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Select-Object -First 1
    $toolchainRoot = if ($toolchain) { $toolchain.FullName } else { $null }
}

foreach ($target in @(
    @{ Name = 'x86'; Prefix = 'i686-w64-mingw32' },
    @{ Name = 'x64'; Prefix = 'x86_64-w64-mingw32' }
)) {
    $clangName = "{0}-clang.exe" -f $target.Prefix
    if ($toolchainRoot) {
        $clang = Join-Path $toolchainRoot "bin\$clangName"
        if (-not (Test-Path -LiteralPath $clang -PathType Leaf)) {
            throw "Compiler not found: $clang"
        }
    } else {
        $command = Get-Command $clangName -ErrorAction SilentlyContinue
        if (-not $command) {
            throw 'LLVM-MinGW not found. Pass -ToolchainPath or add its bin directory to PATH.'
        }
        $clang = $command.Source
    }
    $out = Join-Path $PSScriptRoot ("build\{0}" -f $target.Name)
    New-Item -ItemType Directory -Force $out | Out-Null
    & $clang -std=c11 -O2 -Wall -Wextra -Werror -shared `
      (Join-Path $PSScriptRoot 'src\msxgr_compat.c') `
      (Join-Path $PSScriptRoot 'src\MSXGr.def') `
      -o (Join-Path $out 'MSXGr.dll') -lsetupapi -lwinusb
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    & $clang -std=c11 -O2 -Wall -Wextra -Werror `
      (Join-Path $PSScriptRoot 'tests\test_gradius.c') `
      -o (Join-Path $out 'test_gradius.exe')
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
