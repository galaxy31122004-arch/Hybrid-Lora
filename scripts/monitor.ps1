# Serial monitor with timestamps; every session is also saved under logs/
# (platformio.ini: monitor_filters = time, log2file). Exit: Ctrl+C.
param([string]$Env = "gateway", [string]$Port = "COM3")
$root = Split-Path -Parent $PSScriptRoot
$env:PLATFORMIO_CORE_DIR = "D:\Hybrid_lora\pio-core"
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$logs = Join-Path $root "logs"
New-Item -ItemType Directory -Force $logs | Out-Null
Set-Location $logs  # log2file writes into the current folder
$host.UI.RawUI.WindowTitle = "Serial $Port ($Env)"
Write-Host "Serial $Port ($Env). Nhan-tha EN de xem log khoi dong. Thoat: Ctrl+C" -ForegroundColor Green
if ($Env -like "gateway*") { Write-Host "Lenh: ISO, QUIET 1, QUIET 0" -ForegroundColor Green }
else { Write-Host "Lenh: TEST, TEST <phut>, TEST 0" -ForegroundColor Green }
& $pio device monitor -d $root -e $Env --port $Port
