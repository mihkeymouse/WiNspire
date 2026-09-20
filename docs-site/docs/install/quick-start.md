---
title: Quick Start
sidebar_position: 2
---
# Quick Start

Choose a package before copying files:

| Use Native when... | Use Server when... |
| --- | --- |
| The disk image can fit entirely within the calculator's storage | the disk image is too large for the calculator|
|The OS you wish to run can make do with 16 MiB RAM and 256 KiB VRAM | The OS you wish to run needs more than that.|

## Native setup
Native setup is relatively straightforward.
You need:

- a TI-Nspire CX II or CX II CAS with a compatible [Ndless](https://ndless.me) install;
- a bootable flat disk image
### Prepare the Native folder
1)  Copy over the contents of the Native folder from the latest WiNspire release along with your disk image named `disk.img.tns` to a folder on your Nspire. 

2) Run winspire.tns and enjoy!

```
Native/
  winspire.tns       // The actual WiNspire executable
  bios.bin.tns       // PC BIOS
  vgabios.bin.tns    // VGA BIOS
  winspire.ini.tns   // WiNspire config file
  disk.img.tns       // Flat disk image
```

See [Disk Images](../images.md) for sources and preparation instructions.

### Configuration
`winspire.ini.tns` is used to configure the emulator. The default Native profile is given below.
```ini
[pc]
bios = bios.bin.tns
vga_bios = vgabios.bin.tns
mem_size = 16M
vga_mem_size = 256K
hda = disk.img.tns
fill_cmos = 1

[display]
width = 320
height = 240

[cpu]
gen = 4
fpu = 0
clock_hz = 4770000
```

The `clock_hz` sets the clock that is presented to old software. It does not change how quickly the emulator executes instructions.

## Server / Calculator setup
In Server mode, the disk image lives on the PC. [LinuxLoader2](https://github.com/tangrs/nspire-linux-loader2) is used to set up a small Linux environment on the calculator, create a USB RNDIS link and stream the disk image over the cable using the WiNspire Remote Block Protocol.

The Server executable is at the moment,; Windows only,a because it uses Windows APIs and the WinSock protocol.
The directory structure for the server mode looks like this:

```
Server/
  winspire-server.exe // The server executable
  Calculator/
    linuxloader2_auto.tns // Launcher for Linux environment
    linux/
      zImage.tns // Compressed Linux Kernel image file
      dtb.tns // Kernel device tree
      nspire-linux-micro-initrd.tns // initial ramdisk
```
### Server Setup

1. Copy the contents of the `Server/Calculator` folder over to your Nspire - keep
 relative paths intact!
2. Use a data capable USB cable to connect your PC to the Nspire.
3. Launch  `linuxloader2_auto.tns` on the Nspire.
4. After some time, you will be prompted to unplug and reconnect the USB cable. Post reconnect Windows will enumerate the Nspire as a RNDIS compatible device.
5. If not done automatically, use Device Manager to  bind the device to Microsoft’s **Remote NDIS Compatible
   Device** driver.

6. Set the adapter to `192.168.7.1` & subnet mask to `255.255.255.0`. Leave the other fields blank.
7. Allow `winspire-server.exe` on **Private networks** if Windows
   Firewall asks for it.
8. Reset the calculator to return to TI-OS.


### Start server.
In the directory with the server binary:
```powershell
.\winspire-server.exe --image "PATH_TO_DISK" --strict-flush
```
The server shall bind to `192.168.7.1` and listen on TCP port `3860`. 
### Start the calculator

1. After making sure ndless is installed and the  USB is connected; launch `linuxloader2_auto.tns` on the calculator.
2. Unplug and reconnect USB at the prompt.
3. Choose a profile:

   1. **Windows 95 / 98 / Me**
   2. **Windows 2000**
   3. **Windows XP / XP Embedded**
All choices use 44 MB guest RAM and 2 MB VRAM.
4. And that's it! Now you just have to wait for Windows to boot.


### A quick guide to server-speak
This section serves as a primer for interpreting server logs.

A normal connection produces this sequence:

```
waiting for WiNspire
client connected
TIME-SYNC
HELLO
READ / WRITE / FLUSH
```
The first connection is used to set the Calculator clock. This is particularly important for later era OSs like XP, as bad time could cause the guest to refuse to log in with an invalid date/time error.

Each line begins with the PC clock plus elapsed time (elapsed time makes it easy to time boots).


| Server speak |Human speak|
| --- | --- |
| `image: ... (N sectors, read/write)` | The server opened this raw image, this is its geometry |
| `waiting for WiNspire` | The server is waiting for the calculator. This can be before first connect or after a disconnect|
| `client connected` | Nspire reached the server over RNDIS. |
| `HELLO` |Indicates that the calculator and server agreed on the disk geometry. The elapsed time resets at the first `HELLO`  |
| `READ` / `WRITE` / ` FLUSH`  | Show disk activity. These include transfer and timing data. `lba` is the first requested sector. `count` gives the number of sectors. For reads, `dt` is the gap since the previous read began.  |
| `STATE` | Periodic telemetry from the calculator. `loop`, `cyc`, `rate`, and `cs:ip` report emulator progress. |


## Key Mapping

| Calculator control | Guest input |
| --- | --- |
| `A`–`Z`, `0`–`9` | Corresponding calculator key |
| Esc, Tab, Enter, Ctrl, Shift, Space, Home, |  Corresponding calculator key |
| Del | Backspace |
| Minus, Equals, Comma, Period, Divide | `-`, `=`, `,`, `.`, `/` |
| Var | `;` |
| Shift + Var | `:` |
| Touchpadtop, bottom, left, right edge | Arrow key |
| Slide near touchpad center | Move pointer |
| Press touchpad center | Left click |
| Ctrl + touchpad center | Right click |
