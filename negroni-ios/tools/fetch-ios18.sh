#!/bin/bash
# iOS 18.7.8 (22H352) for the iPhone XR (iPhone11,8): only the root and the two cryptex dmgs (about 8.3 GB),
# decrypted with ipsw (the AEA key comes from Apple's wkms-public service). As Bergamota's fetch-root.sh.
# Output: ~/ios-work/ios18/ipsw/22H352__iPhone11,8/094-{31934,32062,32850}-020.dmg (not in git: Apple's files)
set -euo pipefail
IPSW=${IPSW:-$HOME/ios-work/tools/bin/ipsw}
D=$HOME/ios-work/ios18/ipsw
mkdir -p "$D"
"$IPSW" download ipsw --device iPhone11,8 --version 18.7.8 --pattern '^094-(31934|32062|32850)-020\.dmg(\.aea)?$' --flat -y -o "$D"
for a in "$D"/22H352__iPhone11,8/*.aea; do
	[ -e "$a" ] || continue
	"$IPSW" fw aea "$a" -o "$D/22H352__iPhone11,8" && rm -f "$a"
done
ls -la "$D/22H352__iPhone11,8"
