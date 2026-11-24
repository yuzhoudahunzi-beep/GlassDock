# GlassDock build script (MinGW-w64, x86_64)
# Produces:
#   GlassDock.exe           - the widget main program
#   GlassDock.exe.manifest  - side-by-side manifest (DPI + Common Controls v6)
#   installer\setup.exe     - self-contained installer
#   release\GlassDock-Setup-1.0.0.exe - final distributable
$ErrorActionPreference = 'Stop'

$Root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$Src     = Join-Path $Root 'src'
$Inst    = Join-Path $Root 'installer'
$Tools   = Join-Path $Root 'tools'
$Release = Join-Path $Root 'release'
$Gpp     = 'D:\Doubao\toolchain\mingw\mingw64\bin\g++.exe'
$Windres = 'D:\Doubao\toolchain\mingw\mingw64\bin\windres.exe'

$MainLibs = @('-lgdiplus','-ldwmapi','-lshell32','-lcomctl32','-lole32','-luuid','-lshlwapi','-lgdi32','-luser32','-ladvapi32')
$SetupLibs = @('-lshell32','-lole32','-luuid','-ladvapi32','-lshlwapi')

# 1. main program icon resource
Push-Location $Src
& $Windres resource.rc -o resource.o
if ($LASTEXITCODE -ne 0) { throw "windres (icon) failed" }
Pop-Location

# 2. main program
& $Gpp -mwindows -municode -O2 -s -static `
    (Join-Path $Src 'GlassDock.cpp') (Join-Path $Src 'resource.o') `
    -o (Join-Path $Root 'GlassDock.exe') @MainLibs
if ($LASTEXITCODE -ne 0) { throw "g++ (GlassDock) failed" }

# 3. side-by-side manifest
Copy-Item (Join-Path $Src 'app.manifest') (Join-Path $Root 'GlassDock.exe.manifest') -Force

# 4. installer resource (embeds the exe + manifest + icon)
Push-Location $Inst
& $Windres setup.rc -o setup_res.o
if ($LASTEXITCODE -ne 0) { throw "windres (setup) failed" }
Pop-Location

# 5. installer
& $Gpp -mwindows -municode -O2 -s -static `
    (Join-Path $Inst 'setup.cpp') (Join-Path $Inst 'setup_res.o') `
    -o (Join-Path $Inst 'setup.exe') @SetupLibs
if ($LASTEXITCODE -ne 0) { throw "g++ (setup) failed" }

# 6. final distributable
New-Item -ItemType Directory -Force -Path $Release | Out-Null
Copy-Item (Join-Path $Inst 'setup.exe') (Join-Path $Release 'GlassDock-Setup-1.0.0.exe') -Force

Write-Host "Build OK: $Release\GlassDock-Setup-1.0.0.exe"
