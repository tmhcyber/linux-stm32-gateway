[CmdletBinding()]
param(
    [string]$VmName = 'linux-stm32',
    [int]$SshPort = 2222,
    [string]$HostSerialPort,
    [string]$VBoxManage = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$image = Join-Path $root '.local/downloads/noble-server-cloudimg-amd64.vmdk'
$vmBase = Join-Path $root '.local/vms'
$vmDir = Join-Path $vmBase $VmName
$disk = Join-Path $vmDir 'ubuntu.vdi'
$seed = Join-Path $root '.local/seed/seed.iso'

function Invoke-VBox {
    & $VBoxManage @args
    if ($LASTEXITCODE -ne 0) { throw "VBoxManage failed: $($args[0])" }
}

if (-not (Test-Path -LiteralPath $VBoxManage)) { throw 'Install VirtualBox first.' }
if (-not (Test-Path -LiteralPath $image)) { throw 'Download and verify the official Ubuntu VMDK first.' }
if (-not (Test-Path -LiteralPath $seed)) { throw 'Run prepare_seed.py first.' }
$sshConfig = Join-Path $root '.local/ssh_config'
if (-not (Test-Path -LiteralPath $sshConfig)) { throw 'Run prepare_seed.py first.' }
$ports = @((Get-Content -LiteralPath $sshConfig) | Select-String '^\s*Port\s+(\d+)\s*$')
if ($ports.Count -ne 1 -or [int]$ports[0].Matches[0].Groups[1].Value -ne $SshPort) {
    throw 'SSH seed/config port differs from -SshPort; regenerate seed with matching --port before VM creation.'
}
$existing = & $VBoxManage list vms
if ($existing | Where-Object { $_ -match ('^"' + [regex]::Escape($VmName) + '" ') }) {
    throw "VM already exists: $VmName. Inspect it instead of overwriting it."
}
if (Test-Path -LiteralPath $vmDir) { throw "VM directory already exists: $vmDir" }
if (Get-NetTCPConnection -State Listen -LocalPort $SshPort -ErrorAction SilentlyContinue) {
    throw "SSH forwarding port already in use: $SshPort"
}

Invoke-VBox createvm --name $VmName --ostype Ubuntu_64 --basefolder $vmBase --register
Invoke-VBox modifyvm $VmName --memory 4096 --cpus 2 --firmware efi --graphicscontroller vmsvga --vram 16 --nic1 nat --cableconnected1 on --natpf1 "ssh,tcp,127.0.0.1,$SshPort,,22" --usb-ohci off --usb-ehci off --usb-xhci off --audio-enabled off --uart1 0x3F8 4 --uartmode1 file (Join-Path $vmDir 'serial-console.log')
Invoke-VBox clonemedium disk $image $disk --format VDI
Invoke-VBox modifymedium disk $disk --resize 32768
Invoke-VBox storagectl $VmName --name SATA --add sata --controller IntelAhci --portcount 2 --bootable on
Invoke-VBox storageattach $VmName --storagectl SATA --port 0 --device 0 --type hdd --medium $disk
Invoke-VBox storageattach $VmName --storagectl SATA --port 1 --device 0 --type dvddrive --medium $seed
Invoke-VBox modifyvm $VmName --boot1 disk --boot2 dvd --boot3 none --boot4 none
if ($HostSerialPort) {
    & (Join-Path $PSScriptRoot 'Set-HostSerial.ps1') -Port $HostSerialPort -VmName $VmName -VBoxManage $VBoxManage
}
Invoke-VBox startvm $VmName --type headless

Write-Output "VM started. SSH: ssh -F `"$root\.local\ssh_config`" linux-stm32"
