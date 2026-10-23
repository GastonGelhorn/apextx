# Build the ApexTX Theme Editor as a standalone Windows .exe.
#
# Produces dist/ApexTX-Theme-Editor.exe: the local theme editor
# (tools/apextx-theme-editor.py) packaged as a single, double-clickable app with
# a native window (Edge WebView2, falling back to the browser), like the ApexTX
# Flasher. No binaries are committed to the repo.
#
# Requirements: Windows, Python 3.11+ on PATH.
# Run from the repository root:  pwsh tools/build-windows-theme-editor.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$work = Join-Path $root 'build/theme-editor'
New-Item -ItemType Directory -Force $work | Out-Null

Write-Host '==> Python venv + dependencies'
$venv = Join-Path $work 'venv'
if (-not (Test-Path $venv)) { python -m venv $venv }
$py = Join-Path $venv 'Scripts/python.exe'
& $py -m pip install --upgrade pip | Out-Null
& $py -m pip install pyinstaller==6.11.1 pywebview==6.2.1 pillow==11.1.0 | Out-Null

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
& (Join-Path $venv 'Scripts/pyinstaller.exe') --onefile --windowed --name ApexTX-Theme-Editor `
    --icon $ico `
    --add-data "tools/apextx-theme-editor.html;." `
    --add-data "tools/apextx-updater-logo.png;." `
    --add-data "tools/apextx-theme-editor-catalog.json;." `
    --collect-all webview `
    --paths tools --noconfirm --clean (Join-Path $root 'tools/apextx-theme-editor.py')

Write-Host '==> Done: dist/ApexTX-Theme-Editor.exe'
