[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
& python (Join-Path $PSScriptRoot 'sync_source.py')
if ($LASTEXITCODE -ne 0) { throw 'Source synchronization failed.' }
