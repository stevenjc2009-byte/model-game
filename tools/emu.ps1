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

# Killing Azahar by image name used to take down every Azahar on the machine,
# not just the one this harness started - including steve's own manually
# launched copy and, on at least one occasion, a parallel Claude session's
# hotswap.3dsx build that happened to be open at the same time. Regression
# runs on this machine are not solo: more than one Azahar instance can be
# legitimately alive at once, so this harness may only ever stop the single
# process it personally launched. It remembers that process across
# invocations - boot and kill each run as their own PowerShell process and
# share no memory - by writing its pid, exe path and start time to a small
# file in $env:TEMP (outside the project tree, harmless to leave behind) when
# it boots, and reading that file back before it stops anything. A missing
# file, a pid that has already exited, or a pid that has since been reused by
# some unrelated process are all treated the same way: nothing of ours to
# stop. None of those cases may fall back to killing by name - that fallback
# is the whole defect this replaced.
$PidFile = Join-Path $env:TEMP 'modelkit-emu-azahar.pid'

function Get-OwnAzaharProcess {
  if (-not (Test-Path $PidFile)) { return $null }
  $lines = Get-Content -Path $PidFile -ErrorAction SilentlyContinue
  if (-not $lines -or $lines.Count -lt 3) { Remove-Item $PidFile -ErrorAction SilentlyContinue; return $null }
  $recPid = 0; $recPath = $lines[1]; $recStart = 0L
  if (-not [int]::TryParse($lines[0], [ref]$recPid))    { Remove-Item $PidFile -ErrorAction SilentlyContinue; return $null }
  if (-not [long]::TryParse($lines[2], [ref]$recStart)) { Remove-Item $PidFile -ErrorAction SilentlyContinue; return $null }
  $p = Get-Process -Id $recPid -ErrorAction SilentlyContinue
  if (-not $p -or $p.ProcessName -ne 'azahar') { Remove-Item $PidFile -ErrorAction SilentlyContinue; return $null }
  try { $samePath = ($p.Path -eq $recPath) } catch { $samePath = $true }
  if (-not $samePath -or $p.StartTime.Ticks -ne $recStart) {
    Remove-Item $PidFile -ErrorAction SilentlyContinue
    return $null
  }
  return $p
}

if ($Action -eq 'kill') {
  $p = Get-OwnAzaharProcess
  if ($p) {
    Stop-Process -Id $p.Id -Force
    Remove-Item $PidFile -ErrorAction SilentlyContinue
    Write-Output 'KILLED'
  } else {
    Write-Output 'NOTHING TO KILL'
  }
  exit 0
}

$p = Get-OwnAzaharProcess
if ($p) {
  Stop-Process -Id $p.Id -Force
  Remove-Item $PidFile -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 700
if (-not (Test-Path $App))    { Write-Output "NO APP: $App";       exit 2 }
if (-not (Test-Path $Azahar)) { Write-Output "NO AZAHAR: $Azahar"; exit 5 }

$proc = Start-Process -FilePath $Azahar -ArgumentList "`"$App`"" -PassThru

# Wait for the window rather than sleeping a guessed amount: a cold start after a
# shader cache wipe takes many times longer than a warm one. Watch the pid we
# just launched, not "any azahar with a window" - with other instances open
# that match could easily be someone else's window.
$deadline = (Get-Date).AddSeconds(40)
do {
  Start-Sleep -Milliseconds 500
  $p = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
} while ((-not $p -or $p.MainWindowHandle -eq 0) -and (Get-Date) -lt $deadline)
if (-not $p -or $p.MainWindowHandle -eq 0) { Write-Output 'BOOT TIMEOUT'; exit 3 }

Start-Sleep -Milliseconds 1500
[void][RU]::MoveWindow($p.MainWindowHandle, 40, 0, $W, $H, $true)
Start-Sleep -Milliseconds $SettleMs
"$($p.Id)`n$Azahar`n$($p.StartTime.Ticks)" | Set-Content -Path $PidFile -Encoding ascii
Write-Output ('BOOTED pid=' + $p.Id)
