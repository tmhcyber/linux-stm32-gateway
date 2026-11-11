[CmdletBinding()]
param(
    [ValidatePattern('^COM[1-9][0-9]*$')][string]$Port,
    [string]$VmName = 'linux-stm32',
    [string]$VBoxManage = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
)
$ErrorActionPreference = 'Stop'
$devices = @(Get-PnpDevice -Class Ports -PresentOnly | Where-Object {
    $_.InstanceId -match '^USB\\VID_1A86&PID_7523\\'
})
if ($Port) {
    $devices = @($devices | Where-Object { $_.FriendlyName -match ('\(' + [regex]::Escape($Port) + '\)$') })
}
if ($devices.Count -ne 1) { throw 'Specify exactly one connected CH340 port with -Port COMx.' }
if ($devices[0].FriendlyName -notmatch '\((COM[1-9][0-9]*)\)$') { throw 'Could not determine the CH340 COM port.' }
$Port = $Matches[1]
$vmInfo = & $VBoxManage showvminfo $VmName --machinereadable
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect VM.' }
if ($vmInfo -notcontains 'VMState="poweroff"') { throw 'Shut down the VM before changing serial settings.' }

# Read through the existing Windows serial driver; do not capture the USB device.
& $VBoxManage modifyvm $VmName --usb-ohci off --usb-ehci off --usb-xhci off --uart2 0x2F8 3 --uartmode2 "\\.\$Port"
if ($LASTEXITCODE -ne 0) { throw 'Could not configure host serial mapping.' }
Write-Output "Configured host $Port -> Ubuntu /dev/ttyS1; guest USB controllers disabled."
