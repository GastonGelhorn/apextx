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
& $py -m pip install pyinstaller==6.11.1 pyusb==1.3.1 pywebview==6.2.1 `
    libusb-package==1.0.26.3 pillow==11.1.0 | Out-Null

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

Write-Host '==> Generate app icon from the brand logo'
$ico = Join-Path $work 'ApexTX.ico'
& $py -c @"
from PIL import Image
src = Image.open(r'docs/branding/apextx-logo.png').convert('RGBA')
px = src.load(); w, h = src.size
for y in range(h):
    for x in range(w):
        r, g, b, a = px[x, y]; m = max(r, g, b)
        px[x, y] = (r, g, b, 0 if m <= 26 else 255 if m >= 80 else int((m-26)/54*255))
src = src.crop(src.getbbox()); w, h = src.size
S = 256; c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
sc = min((S-20)/w, (S-20)/h); rw, rh = round(w*sc), round(h*sc)
c.paste(src.resize((rw, rh), Image.LANCZOS), ((S-rw)//2, (S-rh)//2))
c.save(r'$ico'.replace('\\','/'), sizes=[(256,256),(128,128),(64,64),(48,48),(32,32),(16,16)])
print('icon ok')
"@

Write-Host '==> PyInstaller'
$libusb = & $py -c "import libusb_package; print(libusb_package.get_library_path())"
& (Join-Path $venv 'Scripts/pyinstaller.exe') --onefile --windowed --name ApexTX-Flasher `
    --icon $ico `
    --add-data "tools/apextx-updater.html;." `
    --add-data "tools/apextx-updater-logo.png;." `
    --add-binary "$libusb;." `
    --add-binary "$dfuDir/dfu-util.exe;dfu" `
    --add-binary "$dfuDir/libusb-1.0.dll;dfu" `
    --hidden-import usb.backend.libusb1 --collect-all webview `
    --paths tools --noconfirm --clean (Join-Path $root 'tools/apextx-updater.py')

Write-Host '==> Done: dist/ApexTX-Flasher.exe'
