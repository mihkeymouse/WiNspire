param(
    [string]$OutputName = "winspire-server.exe"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $PSCommandPath
$repo = Split-Path -Parent (Split-Path -Parent $scriptDir)
$src = Join-Path $scriptDir "netblock_server.c"
$outDir = Join-Path $repo "build\Server"
$out = Join-Path $outDir $OutputName

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$gcc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $gcc) {
    throw "gcc not found. Install MSYS2/UCRT64 or add gcc.exe to PATH."
}

& $gcc -O2 -Wall -Wextra '-Wl,--no-insert-timestamp' -o $out $src -lws2_32
if ($LASTEXITCODE -ne 0) {
    throw "$OutputName build failed"
}

Get-Item $out | Select-Object FullName,Length,LastWriteTime
