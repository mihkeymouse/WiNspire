---
title: Troubleshooting
sidebar_position: 4
---
# Troubleshooting
## Native

### WiNspire reports no bootable disk
- Make sure your `disk.img.tns` is in the same directory as `winspire.tns`, the BIOS files, and the config file.
- Verify your `disk.img.tns` is a flat raw disk with an MBR and an active boot partition - simply renaming a VHD, VDI, VHDX, QCOW2 or ISO file will not work. For more information see [Disk Images](images.md).



### Windows returns to BIOS post splash
Some old versions of Windows can leave the text framebuffer visible while it switches video modes. Either wait for the next full display refresh or force a refresh by pressing a key on calculator or moving the pointer.

### The screen has stopped updating
Move the pointer or press a key to request an immediate refresh.

### Windows starts ScanDisk or CHKDSK on boot

This usually follows an improper shutdown. Let it finish, then shut Windows down normally.

## Server

### Disconnects and warnings

`client disconnected` and `client session ended during ...` mark the end of a session and include the last request, LBA, and operation counts. A normal shutdown, USB disconnect, crash, or timeout can end a session. The lines immediately before can provide a useful diagnosis.

 WinSock error `10060` - calculator failed to respond in time.
 WinSock error `10054` - the calculator reset the connection. 
Check the  USB connection or whether RNDIS is properly bound. If both are correct check the code with which WiNspire exited on calc.
- For `failed to open image`, `bind failed`, or `listen failed`, check the disk image path / permissions and if port `3860` is already in use.


### Server remains at `waiting for WiNspire`

1. Confirm that Device Manager correctly detects the Nspire as **Remote-NDIS compatible device**.
2. Unplug and reconnect the calculator cable when prompted.
3. Confirm that the USB adapter is on `192.168.7.1/24` and that the gateway and DNS fields are blank.
4. Allow the server through Windows Firewall on the USB adapter's network profile.
5. Try another data-capable USB cable or a direct PC USB port.

### Windows 2000 stalls during boot
Make sure you have chosen the **Windows 2000** profile. Windows 2000 must use PIT timing to boot successfully as opposed to the synthetic timing used by the other profiles.
The disk image should use the **Standard PC** PIC HAL. ACPI and I/O APIC must be disabled from the first Windows 2000 installer boot.

### Windows starts ScanDisk or CHKDSK
This usually follows an improper shutdown or a disconnect during a write. Let it finish, then shut Windows down normally.
