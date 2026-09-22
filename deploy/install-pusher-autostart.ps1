# Token Monitor: autostart for the Claude Code usage pusher (Windows).
# ASCII only on purpose: Windows PowerShell 5 reads .ps1 without a BOM as ANSI,
# so Cyrillic here would break the parser.
#
# Main PC (API key is read from main/secrets.h):
#   powershell -ExecutionPolicy Bypass -File deploy\install-pusher-autostart.ps1 -Broker http://192.168.50.167:4715
#
# Any other machine (no secrets.h there, pass the magic-qube key once):
#   powershell -ExecutionPolicy Bypass -File deploy\install-pusher-autostart.ps1 -Broker http://192.168.50.167:4715 -ApiKey <key>
#
# Remove:
#   Unregister-ScheduledTask -TaskName TokenMonitorPusher -Confirm:$false

param(
    [Parameter(Mandatory = $true)][string]$Broker,
    [string]$ApiKey = ""
)

$ErrorActionPreference = "Stop"
$agentDir = Join-Path (Split-Path $PSScriptRoot -Parent) "agent"
$pusher = Join-Path $agentDir "token_pusher.py"
if (-not (Test-Path $pusher)) { throw "Not found: $pusher" }

# pythonw runs without a console window; the pusher then logs to agent\pusher.log
$python = (Get-Command pythonw.exe -ErrorAction SilentlyContinue).Source
if (-not $python) { $python = (Get-Command python.exe).Source }

# Settings live in a file next to the pusher, not on the task command line.
$config = @{ broker = $Broker }
if ($ApiKey) { $config.api_key = $ApiKey }
$config | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $agentDir "pusher.json")

$task = "TokenMonitorPusher"
if (Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue) {
    Stop-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue
}

$action = New-ScheduledTaskAction -Execute $python -Argument "`"$pusher`"" `
    -WorkingDirectory $agentDir
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
# No time limit (default is 3 days), restart if it crashes.
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries -StartWhenAvailable `
    -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1)

Register-ScheduledTask -TaskName $task -Action $action -Trigger $trigger `
    -Settings $settings -Force | Out-Null
Start-ScheduledTask -TaskName $task

Write-Host "Task $task registered and started. Broker: $Broker"
Write-Host "Log: $(Join-Path $agentDir 'pusher.log')"
