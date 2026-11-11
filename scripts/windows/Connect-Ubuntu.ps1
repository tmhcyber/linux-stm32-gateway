[CmdletBinding()]
param([Parameter(ValueFromRemainingArguments=$true)][string[]]$Command)
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$config = Join-Path $root '.local/ssh_config'
if (-not (Test-Path -LiteralPath $config)) { throw 'SSH configuration is not prepared yet.' }
& ssh -F $config linux-stm32 @Command
exit $LASTEXITCODE
