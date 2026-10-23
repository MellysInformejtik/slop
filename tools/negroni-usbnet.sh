#!/bin/sh
# Host side of negroni's USB Ethernet (kernel negroniusb=2, patch 0119) from WSL.
#   tools/negroni-usbnet.sh [LOGFILE]
# Windows has no CDC-ECM driver, so the phone (USB 1209:0001) is handed to WSL with usbipd-win
# (once, in an Administrator terminal: winget install usbipd; usbipd bind --busid BUSID), cdc_ether is
# loaded, and tools/sweet-usbnet.sh gives the interface 192.168.7.1 and prints the live kernel log
# (UDP 5140; appended to LOGFILE if given).
set -eu
USBIPD=${USBIPD:-usbipd.exe}
command -v "$USBIPD" >/dev/null 2>&1 || USBIPD="/mnt/c/Program Files/usbipd-win/usbipd.exe"
[ -x "$USBIPD" ] || { echo "usbipd-win not found: winget install usbipd (Windows), then rerun" >&2; exit 1; }
echo "waiting for the phone (USB 1209:0001) on Windows..." >&2
until busid=$("$USBIPD" list 2>/dev/null | tr -d '\r' | awk '$2 == "1209:0001" { print $1; exit }') && [ -n "$busid" ]; do
	sleep 1
done
echo "phone at busid $busid; attaching to WSL" >&2
"$USBIPD" attach --wsl --busid "$busid" || {
	echo "attach failed: in an Administrator terminal run  usbipd bind --busid $busid  once, then rerun" >&2
	exit 1
}
sudo modprobe cdc_ether 2>/dev/null || true
exec "$(dirname "$0")/sweet-usbnet.sh" "$@"
