# Автозапуск отправщика статистики при входе в Windows.
# Запустить один раз:  powershell -ExecutionPolicy Bypass -File install-pusher-autostart.ps1 -Broker http://192.168.50.50:8765

param(
    [Parameter(Mandatory = $true)][string]$Broker
)

$agentDir = Join-Path (Split-Path $PSScriptRoot -Parent) "agent"
$pusher = Join-Path $agentDir "token_pusher.py"
if (-not (Test-Path $pusher)) { throw "Не найден $pusher" }

$python = (Get-Command pythonw.exe -ErrorAction SilentlyContinue).Source
if (-not $python) { $python = (Get-Command python.exe).Source }

$action = New-ScheduledTaskAction -Execute $python `
    -Argument "`"$pusher`" --broker $Broker" -WorkingDirectory $agentDir
$trigger = New-ScheduledTaskTrigger -AtLogOn
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries -StartWhenAvailable

Register-ScheduledTask -TaskName "TokenMonitorPusher" -Action $action `
    -Trigger $trigger -Settings $settings -Force | Out-Null

Write-Host "Готово. Задача TokenMonitorPusher создана, брокер: $Broker"
Write-Host "Запустить прямо сейчас:  Start-ScheduledTask -TaskName TokenMonitorPusher"
