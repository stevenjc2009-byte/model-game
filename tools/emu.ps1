# Launch and stop Azahar for tools/regress.py. Nothing else.
#
# The capture rig this is trimmed from also does clicks, drags and PrintWindow
# screenshots. The regression harness needs none of that: the audits it runs
# report in words, into a file on the emulated SD card, so there is nothing to
# photograph and nothing to click. Only what is used is here.
param(
  [ValidateSet('boot','kill')]
  [string]$Action = 'boot',
  [string]$App = '',
  [int]$W = 1040, [int]$H = 1385,
  [int]$SettleMs = 1500
)

# Overridable, because this path is one machine's install and the harness should
# not need editing on another.
$Azahar = $env:AZAHAR_EXE
if (-not $Azahar) { $Azahar = 'C:\Program Files\Azahar\azahar.exe' }

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class RU {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h,int x,int y,int w,int t,bool rp);
}
"@
[void][RU]::SetProcessDPIAware()

if ($Action -eq 'kill') {
  Get-Process azahar -ErrorAction SilentlyContinue | Stop-Process -Force
  Write-Output 'KILLED'
  exit 0
}

Get-Process azahar -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 700
if (-not (Test-Path $App))    { Write-Output "NO APP: $App";       exit 2 }
if (-not (Test-Path $Azahar)) { Write-Output "NO AZAHAR: $Azahar"; exit 5 }

Start-Process -FilePath $Azahar -ArgumentList "`"$App`""

# Wait for the window rather than sleeping a guessed amount: a cold start after a
# shader cache wipe takes many times longer than a warm one.
$deadline = (Get-Date).AddSeconds(40)
do {
  Start-Sleep -Milliseconds 500
  $p = Get-Process azahar -ErrorAction SilentlyContinue |
       Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
} while (-not $p -and (Get-Date) -lt $deadline)
if (-not $p) { Write-Output 'BOOT TIMEOUT'; exit 3 }

Start-Sleep -Milliseconds 1500
[void][RU]::MoveWindow($p.MainWindowHandle, 40, 0, $W, $H, $true)
Start-Sleep -Milliseconds $SettleMs
Write-Output ('BOOTED pid=' + $p.Id)
