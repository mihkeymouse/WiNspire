---
title: Build from Source
sidebar_position: 2
---
# Build from source.
There are two build targets - Native and Server.
The build was tested on Win11/WSL Ubuntu.

Clone the repository:

```bash
git clone https://github.com/mihkeymouse/WiNspire.git
cd WiNspire
```

## Native
Install the [Ndless SDK](https://github.com/ndless-nspire/Ndless) first. By default the build script looks for it at $HOME/Ndless/ndless-sdk. If installed elsewhere set NDLESS_SDK first.

Then simply:
```bash
make native
```
The finished calculator files are in `build/Native`.

## Server.
For this you need Windows with a WSL distro.
Install the following packages on the WSL distro:
```bash
sudo apt update
sudo apt install build-essential gcc-arm-linux-gnueabi binutils-arm-linux-gnueabi cpio gzip git make
```
On Windows, Install [MSYS2](https://www.msys2.org/) and add `C:\msys64\ucrt64\bin` to `PATH`.
 Then in the UCRT64 shell:
```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc
```
Enable Windows interop in your WSL distro, then simply:

```bash
make server
```
The finished server and calculator files are in `build/Server`.
