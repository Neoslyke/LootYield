# LootYield Build & Package Script
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "       Building LootYield (.dll UE4SS Mod)               " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Locate Visual Studio MSVC & Windows SDK
$vsPath = "C:\Program Files\Microsoft Visual Studio\18\Community"
$msvcBin = Join-Path $vsPath "VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64"
$msvcInc = Join-Path $vsPath "VC\Tools\MSVC\14.51.36231\include"
$msvcLib = Join-Path $vsPath "VC\Tools\MSVC\14.51.36231\lib\x64"

$winSdk = "C:\Program Files (x86)\Windows Kits\10"
$sdkVer = "10.0.26100.0"
$sdkIncUcrt   = Join-Path $winSdk "Include\$sdkVer\ucrt"
$sdkIncUm     = Join-Path $winSdk "Include\$sdkVer\um"
$sdkIncShared = Join-Path $winSdk "Include\$sdkVer\shared"
$sdkLibUm     = Join-Path $winSdk "Lib\$sdkVer\um\x64"
$sdkLibUcrt   = Join-Path $winSdk "Lib\$sdkVer\ucrt\x64"

$gameUe4ssDll = "D:\Games\Steam\steamapps\common\RSDragonwilds\RSDragonwilds\Binaries\Win64\ue4ss\UE4SS.dll"

# Fallback: find MSVC dynamically if hardcoded path does not exist
if (-not (Test-Path (Join-Path $msvcBin "cl.exe"))) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsInstall = & $vswhere -latest -products * -property installationPath
        if ($vsInstall) {
            $msvcRoot = Join-Path $vsInstall "VC\Tools\MSVC"
            if (Test-Path $msvcRoot) {
                $latestVer = Get-ChildItem -Directory $msvcRoot | Sort-Object Name -Descending | Select-Object -First 1
                if ($latestVer) {
                    $msvcBin = Join-Path $latestVer.FullName "bin\Hostx64\x64"
                    $msvcInc = Join-Path $latestVer.FullName "include"
                    $msvcLib = Join-Path $latestVer.FullName "lib\x64"
                }
            }
        }
    }
}

$clExe      = Join-Path $msvcBin "cl.exe"
$libExe     = Join-Path $msvcBin "lib.exe"
$dumpbinExe = Join-Path $msvcBin "dumpbin.exe"

if (-not (Test-Path $clExe)) {
    Write-Host "[ERROR] Could not locate MSVC compiler cl.exe at: $clExe" -ForegroundColor Red
    exit 1
}

# 2. Extract and generate UE4SS.lib if missing
$ue4ssLib = Join-Path $scriptDir "UE4SS.lib"
if (-not (Test-Path $ue4ssLib)) {
    Write-Host "[1/4] Extracting UE4SS exports from $gameUe4ssDll..." -ForegroundColor Yellow
    $exportsRaw = & $dumpbinExe /exports "$gameUe4ssDll"
    $defContent = [System.Collections.Generic.List[string]]::new()
    $defContent.Add("EXPORTS")
    foreach ($line in $exportsRaw) {
        if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)') {
            $defContent.Add($matches[1])
        }
    }
    $ue4ssDef = Join-Path $scriptDir "UE4SS.def"
    [System.IO.File]::WriteAllLines($ue4ssDef, $defContent)

    Write-Host "[2/4] Generating UE4SS.lib..." -ForegroundColor Yellow
    & $libExe /def:"$ue4ssDef" /machine:x64 /out:"$ue4ssLib" /nologo | Out-Null
} else {
    Write-Host "[1/4] Using cached UE4SS.lib..." -ForegroundColor Gray
    Write-Host "[2/4] Skipped export extraction." -ForegroundColor Gray
}

# 3. Compile main.dll
Write-Host "[3/4] Compiling dlls\main.dll..." -ForegroundColor Yellow
$dllsDir = Join-Path $scriptDir "dlls"
if (-not (Test-Path $dllsDir)) { New-Item -ItemType Directory -Path $dllsDir | Out-Null }

$srcCpp = Join-Path $scriptDir "src\dllmain.cpp"
$outDll = Join-Path $dllsDir "main.dll"

& $clExe /O2 /LD /std:c++20 /EHsc /nologo `
    "/I$msvcInc" `
    "/I$sdkIncUcrt" `
    "/I$sdkIncUm" `
    "/I$sdkIncShared" `
    "$srcCpp" `
    /link /DLL "/OUT:$outDll" `
    "/LIBPATH:$msvcLib" `
    "/LIBPATH:$sdkLibUm" `
    "/LIBPATH:$sdkLibUcrt" `
    "$ue4ssLib"

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Compilation failed with exit code $LASTEXITCODE!" -ForegroundColor Red
    exit $LASTEXITCODE
}

# Clean intermediate artifacts
@("dllmain.obj", "dllmain.exp", "dllmain.lib", "exports.txt", "UE4SS.def", "UE4SS.exp") | ForEach-Object {
    $p = Join-Path $scriptDir $_
    if (Test-Path $p) { Remove-Item $p -Force }
}

# 4. Package mod directory LootYield/
Write-Host "[4/4] Packaging LootYield mod folder..." -ForegroundColor Yellow

# Ensure enabled.txt in local workspace
$localEnabled = Join-Path $scriptDir "enabled.txt"
if (-not (Test-Path $localEnabled)) { [System.IO.File]::WriteAllText($localEnabled, "") }

# Prepare output/LootYield directory
$outputRoot   = Join-Path $scriptDir "output"
$outputModDir = Join-Path $outputRoot "LootYield"
$outputDlls   = Join-Path $outputModDir "dlls"

if (Test-Path $outputModDir) { Remove-Item $outputModDir -Recurse -Force }
New-Item -ItemType Directory -Path $outputDlls -Force | Out-Null

# Create enabled.txt
[System.IO.File]::WriteAllText((Join-Path $outputModDir "enabled.txt"), "")

# Copy main.dll
Copy-Item $outDll (Join-Path $outputDlls "main.dll") -Force

# Create zip archive
$zipPath = Join-Path $outputRoot "LootYield.zip"
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path $outputModDir -DestinationPath $zipPath -Force



Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "   [SUCCESS] LootYield built and packaged successfully!  " -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "Mod Folder Structure:" -ForegroundColor Cyan
Write-Host "  $outputModDir\" -ForegroundColor White
Write-Host "  ├── enabled.txt" -ForegroundColor White
Write-Host "  └── dlls\" -ForegroundColor White
Write-Host "      └── main.dll" -ForegroundColor White
Write-Host ""
Write-Host "Zip Archive: $zipPath" -ForegroundColor Gray
Write-Host "==========================================================" -ForegroundColor Green
