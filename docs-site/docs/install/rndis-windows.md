---
title: Windows RNDIS
sidebar_position: 5
---

# Windows RNDIS

The Server package uses a private USB network between the calculator and the
Windows PC holding the disk image. Windows supplies the driver; WiNspire does
not install a custom kernel driver.

## What you are configuring

The calculator presents a standard RNDIS USB Ethernet device. Bind it to
Microsoft’s **Remote NDIS Compatible Device** driver, then assign a static IPv4
address.

Microsoft documents RNDIS over USB and the in-box Windows class driver in its
[Remote NDIS overview](https://learn.microsoft.com/windows-hardware/drivers/network/overview-of-remote-ndis--rndis-).

| Endpoint | Address |
| --- | --- |
| Windows RNDIS adapter | `192.168.7.1/24` |
| Calculator Linux | `192.168.7.2/24` |
| WRBP server | TCP `3860` |

This is a two-device link. Do not add a gateway, DNS server, network bridge, or
Internet Connection Sharing.

## Make Windows enumerate RNDIS

1. Connect the calculator directly with a data-capable USB cable.
2. Open `linuxloader2_auto.tns`.
3. Wait for the calculator’s RNDIS prompt.
4. Unplug and reconnect the USB cable.
5. Open **Device Manager**.
6. Look under **Network adapters**, **Other devices**, or **Universal Serial Bus
   devices** for `WiNspire RNDIS Netdisk`, an RNDIS gadget, or an unknown USB
   network device.

The device uses USB VID `0525` and PID `A4A2`. Those values help distinguish it
from another unknown device.

The reconnect matters. Before Linux starts, Windows sees a TI handheld. After
Linux enables the USB gadget, Windows must enumerate a different RNDIS device.

## Bind Microsoft’s driver

Skip this section if **Remote NDIS Compatible Device** already appears under
**Network adapters** without a warning icon.

1. Right-click the calculator RNDIS device and choose **Update driver**.
2. Select **Browse my computer for drivers**.
3. Select **Let me pick from a list of available drivers on my computer**.
4. Choose **Network adapters** if Windows asks for a device class.
5. Select **Microsoft** as the manufacturer.
6. Select **Remote NDIS Compatible Device**.
7. Continue and accept the compatibility warning if one appears.
8. Confirm that the device moves to **Network adapters** without a warning icon.

Do not install a phone-tethering, serial, or third-party vendor driver. The
supported setup uses the Microsoft RNDIS class driver already present in
Windows.

## Assign `192.168.7.1`

1. Press `Win+R`, enter `ncpa.cpl`, and press Enter.
2. Find the new USB Ethernet adapter. Briefly disconnecting and reconnecting
   the calculator can help identify it.
3. Right-click the adapter and choose **Properties**.
4. Open **Internet Protocol Version 4 (TCP/IPv4)**.
5. Select **Use the following IP address**.
6. Enter:

   - IP address: `192.168.7.1`
   - Subnet mask: `255.255.255.0`
   - Default gateway: blank
   - Preferred and alternate DNS: blank

7. Save the settings.

Windows may label this network **Unidentified network**. That is normal.

## Firewall access

The packaged server binds to `192.168.7.1`, not every PC interface. In Windows Defender Firewall with Advanced Security (`wf.msc`), scope its inbound allow rule to: <sol></sol>

- Program: the extracted `winspire-server.exe`.
- Protocol: TCP; local port: `3860`.
- Scope: local IP `192.168.7.1`; remote IP `192.168.7.2`.
- Profile: the USB adapter's active network profile.

Edit any existing broad WiNspire allow rule rather than adding a second,
narrower rule beside it. See Microsoft's [firewall rule instructions](https://learn.microsoft.com/en-us/windows/security/operating-system-security/network-security/windows-firewall/configure).

WRBP is not encrypted or authenticated. Keep access on the USB link; do not
forward port `3860` through a router.

## Test the link

Start the PC server with your image first. Check its listener:

```powershell
Get-NetTCPConnection -LocalPort 3860 -State Listen
```

Launch LinuxLoader2 once and reconnect USB at the prompt. The calculator
continues automatically; a disk connection produces `HELLO` in the server
window. To check just the USB network, run:

```powershell
ping 192.168.7.2
```

A reply confirms RNDIS and IPv4, not the block server.

## If the adapter does not appear

- Reconnect only after the calculator shows the RNDIS prompt.
- Try another data-capable cable and a direct PC USB port.
- In Device Manager, enable **View > Show hidden devices**, remove stale copies
  of the calculator RNDIS device, then reconnect.
- Confirm that `192.168.7.1` is assigned to the USB adapter, not Wi-Fi or
  Ethernet.
- Restart LinuxLoader2 after completing first-time driver setup. The initial
  Windows driver install can take longer than the calculator’s network wait.

Once the adapter is saved, normal launches only require the unplug/reconnect at
the calculator prompt.
