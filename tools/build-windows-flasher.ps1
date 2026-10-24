# Build the ApexTX Flasher as a standalone Windows .exe.
#
# Produces dist/ApexTX-Flasher.exe: the local-UI updater (tools/apextx-updater.py)
# packaged as a single, double-clickable app with a native window (Edge WebView2,
# falling back to the browser), a bundled libusb runtime, and a bundled dfu-util
# for the ROM DFU full-image install / recovery mode. No binaries are committed to
# the repo; this script fetches dfu-util and builds everything.
#
# Requirements: Windows, Python 3.11+ on PATH, the ARM build not needed.
# Run from the repository root:  pwsh tools/build-windows-flasher.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$work = Join-Path $root 'build/flasher'
New-Item -ItemType Directory -Force $work | Out-Null

Write-Host '==> Python venv + dependencies'
$venv = Join-Path $work 'venv'
if (-not (Test-Path $venv)) { python -m venv $venv }
$py = Join-Path $venv 'Scripts/python.exe'
& $py -m pip install --upgrade pip | Out-Null
& $py -m pip install -r tools/requirements-flasher.txt | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Failed to install flasher dependencies" }

Write-Host '==> Fetch dfu-util 0.11 (GPL-2+)'
$dfuDir = Join-Path $work 'dfu'
New-Item -ItemType Directory -Force $dfuDir | Out-Null
$tar = Join-Path $work 'dfu-util.tar.xz'
$dfuUrl = 'https://dfu-util.sourceforge.net/releases/dfu-util-0.11-binaries.tar.xz'
$dfuSha256 = '6450de30a7dcd8d8c1273f43f0b153f054fd24d85f7f38296b1ad8edbd2ddb25'
if (-not (Test-Path $tar)) {
    Invoke-WebRequest $dfuUrl -OutFile $tar
}
$actualSha256 = (Get-FileHash $tar -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualSha256 -ne $dfuSha256) {
    Remove-Item -Force $tar
    throw "dfu-util archive checksum mismatch (got $actualSha256)"
}
tar -xf $tar -C $work
Copy-Item (Join-Path $work 'dfu-util-0.11-binaries/win64/dfu-util.exe') $dfuDir -Force
Copy-Item (Join-Path $work 'dfu-util-0.11-binaries/win64/libusb-1.0.dll') $dfuDir -Force

Write-Host '==> Package the shared flasher UI and core'
$libusb = & $py -c "import libusb_package; print(libusb_package.get_library_path())"
if ($LASTEXITCODE -ne 0) { throw "Failed to locate libusb" }
& $py tools/build-flasher.py --dfu-dir $dfuDir --libusb $libusb
if ($LASTEXITCODE -ne 0) { throw "Failed to package ApexTX Flasher" }

Write-Host '==> Done: dist/ApexTX-Flasher.exe'
