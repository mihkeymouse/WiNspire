SHELL := /bin/bash

.PHONY: server native clean

server:
	@command -v arm-linux-gnueabi-gcc >/dev/null || { echo "Missing arm-linux-gnueabi-gcc" >&2; exit 1; }
	@command -v powershell.exe >/dev/null || { echo "Missing Windows PowerShell interop" >&2; exit 1; }
	@bash source/build-scripts/build_server.sh >/dev/null
	@script="$$(wslpath -w "$(CURDIR)/source/server/build_netblock_server.ps1")"; powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "$$script" >/dev/null
	@echo "Server build: build/Server"

native:
	@bash source/build-scripts/build_native.sh >/dev/null
	@echo "Native build: build/Native"

clean:
	@rm -rf build
	@echo "Removed WiNspire build output"
