# Flash the current firmware (env "gateway" or "node") and, on success, open
# the serial monitor in its own window and close this one. Older flash and
# monitor windows are closed first. Start it without -NoExit so this window
# closes by itself when the flash is done.
#   powershell -ExecutionPolicy Bypass -File scripts\flash.ps1            # gateway, COM3
#   powershell -ExecutionPolicy Bypass -File scripts\flash.ps1 -Env node -Port COM4
# Put the board in download mode first if it has no auto-reset (hold BOOT,
# tap EN, release BOOT). Do not press EN while it is writing.
param([ValidateSet('gateway', 'node', 'gateway-test', 'node-test')][string]$Env = "gateway", [string]$Port = "COM3")
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot

# One terminal at a time: close older flash/monitor windows (and the monitor
# that holds the port) before this flash starts.
$self = Get-CimInstance Win32_Process -Filter "ProcessId=$PID"
$keep = @($PID, $self.ParentProcessId)
Get-CimInstance Win32_Process | Where-Object {
  $keep -notcontains $_.ProcessId -and $_.CommandLine -and (
    ($_.Name -eq 'powershell.exe' -and $_.CommandLine -match 'scripts\\(flash|monitor)\.ps1') -or
    ($_.Name -eq 'python.exe' -and $_.CommandLine -match 'device monitor'))
} | ForEach-Object {
  Write-Host "Dong terminal cu (PID $($_.ProcessId))" -ForegroundColor DarkGray
  Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 500  # let Windows release the COM port
$env:PLATFORMIO_CORE_DIR = "D:\Hybrid_lora\pio-core"
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$esptool = "D:\Hybrid_lora\pio-core\packages\tool-esptoolpy\esptool.py"
Set-Location $root

# Preserve portal-entered networks before regenerating the compiled defaults.
if ($true) {  # gateway and node both use Wi-Fi (v0.5.4)
  & powershell -NoProfile -ExecutionPolicy Bypass -File 'D:\Personal-Skills\esp32-wifi-provisioning\scripts\pull-wifi-from-board.ps1' -Port $Port
  if ($LASTEXITCODE -eq 3) {
    Write-Warning 'Khong doc duoc Wi-Fi tu board; giu NVS va tiep tuc.'
  } elseif ($LASTEXITCODE -ne 0) { exit 1 }
}
& powershell -NoProfile -ExecutionPolicy Bypass -File 'D:\Personal-Skills\esp32-wifi-provisioning\scripts\make-wifi-secrets.ps1' -Project (Join-Path $root 'firmware\v0.5.4')
if ($LASTEXITCODE -ne 0) { Write-Error 'Khong chuan bi duoc Wi-Fi.'; exit 1 }

Write-Host "=== Nap $Env vao $Port (921600 baud) ===" -ForegroundColor Cyan
# Build first so the board does not wait in download mode during the compile.
& $pio run -e $Env
if ($LASTEXITCODE -ne 0) { Read-Host "Build loi. Nhan Enter de dong"; exit 1 }
Read-Host "Cho board vao che do nap (giu BOOT, nhan-tha EN, tha BOOT) roi nhan Enter"
Write-Host "KHONG bam EN/RESET trong luc dang ghi." -ForegroundColor Yellow
& $pio run -e $Env -t upload --upload-port $Port
$ok = $LASTEXITCODE -eq 0

if (-not $ok) {
  # Retry slowly with the image just built (long/loose cables fail at 921600).
  Write-Host "Nap nhanh that bai, thu lai o 115200..." -ForegroundColor Yellow
  $b = Join-Path $root ".pio\build\$Env"
  $bootApp = Get-ChildItem D:\Hybrid_lora\pio-core\packages\framework-arduinoespressif32 -Recurse -Filter boot_app0.bin |
    Select-Object -First 1 -ExpandProperty FullName
  & $py $esptool --chip esp32 --port $Port --baud 115200 write_flash -z `
    0x1000 "$b\bootloader.bin" 0x8000 "$b\partitions.bin" 0xe000 $bootApp 0x10000 "$b\firmware.bin"
  $ok = $LASTEXITCODE -eq 0
}

if (-not $ok) {
  Write-Host "NAP THAT BAI. Kiem tra nguon/day, vao lai che do nap roi chay lai." -ForegroundColor Red
  Read-Host "Nhan Enter de dong"
  exit 1
}

Write-Host "NAP XONG. Mo Serial monitor..." -ForegroundColor Green
Start-Process powershell -WorkingDirectory $root -ArgumentList @(
  '-NoExit', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'monitor.ps1'),
  '-Env', $Env, '-Port', $Port)
exit 0
