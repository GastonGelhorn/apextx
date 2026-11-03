# Compatibility entry point: the editor now ships inside ApexTX Flasher.
param([string]$PreviewDll = '')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build-windows-flasher.ps1')
