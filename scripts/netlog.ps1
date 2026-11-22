# Open a board's Wi-Fi log (TCP 23) in its own terminal window: no USB cable.
#   powershell -ExecutionPolicy Bypass -File scripts\netlog.ps1 -Target 192.168.1.5
# The IP is in the board's log ("Net log: ... (IP x.x.x.x)") and in nodeIp.
param([Parameter(Mandatory = $true)][string]$Target)  # board IP
$root = Split-Path -Parent $PSScriptRoot
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
Start-Process powershell -WorkingDirectory $root -ArgumentList @(
  '-NoExit', '-Command', "`$host.UI.RawUI.WindowTitle = 'Net log $Target'; & '$py' -I '$root\scripts\netlog.py' '$Target'")
