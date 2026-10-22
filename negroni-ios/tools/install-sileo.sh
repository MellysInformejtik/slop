#!/bin/bash
# Install Procursus (rootless, 1800) + Sileo into the negroni iOS 16 root on userdata, from OrangeFox.
# Adds files only; userdata is not reformatted. Layout as on an iPhone with a rootless jailbreak:
# the prefix lives in /private/preboot/procursus and /var/jb links to it (LaunchServices does not
# register applications from /private/var, see HackiOS userland/src/init.c). A launchd job
# (com.negroni.jbprep) runs the bootstrap's first-boot preparation once and registers Sileo.app
# with uicache at every boot.
#
#   negroni-ios/tools/install-sileo.sh            (phone in OrangeFox, adb connected)
#
# Inputs (fetched by hand, not in git): ~/ios-work/jb/bootstrap.tar (bootstrap-iphoneos-arm64.tar.zst
# from apt.procurs.us/bootstraps/1800, decompressed) and ~/ios-work/jb/sileo/data.tar (data of
# org.coolstar.sileo_2.5.1_iphoneos-arm64.deb).
set -euo pipefail
A=/mnt/c/Users/paw/scoop/shims/adb.exe
J=$HOME/ios-work/jb
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
w() { wslpath -w "$1"; }

"$A" get-state | grep -q recovery || { echo "phone is not in recovery" >&2; exit 1; }
"$A" shell 'grep -q hfsplus /proc/filesystems' || { echo "OrangeFox kernel has no hfsplus" >&2; exit 1; }
"$A" shell 'mkdir -p /tmp/ios && (mountpoint -q /tmp/ios || mount -t hfsplus -o rw,force /dev/block/by-name/userdata /tmp/ios) && test -d /tmp/ios/System/Library/xpc'

echo "== copying the bootstrap and Sileo"
"$A" push "$(w "$J/bootstrap.tar")" /tmp/bootstrap.tar >/dev/null
"$A" push "$(w "$J/sileo/data.tar")" /tmp/sileo.tar >/dev/null
"$A" shell 'set -e; R=/tmp/ios
rm -rf $R/private/preboot/procursus.new; mkdir -p $R/private/preboot/procursus.new
cd $R/private/preboot/procursus.new
tar -xpf /tmp/bootstrap.tar ./var/jb; tar -xpf /tmp/sileo.tar ./var/jb
[ -x var/jb/Applications/Sileo.app/Sileo ] || { echo "no Sileo.app"; exit 1; }
chmod 4755 var/jb/Applications/Sileo.app/giveMeRoot
[ -e $R/private/preboot/procursus ] && mv $R/private/preboot/procursus $R/private/preboot/procursus.old.$(date +%s)
mv var/jb $R/private/preboot/procursus; cd /; rm -rf $R/private/preboot/procursus.new
rm -f $R/private/var/jb; ln -s /private/preboot/procursus $R/private/var/jb
for d in var/mobile var/mobile/Library var/mobile/Library/Caches var/mobile/Library/Preferences var/mobile/Documents var/lib/apt/sileolists var/lib/apt/sileolists/partial; do
  mkdir -p $R/private/preboot/procursus/$d; chown 501:501 $R/private/preboot/procursus/$d; done
rm -f /tmp/bootstrap.tar /tmp/sileo.tar'

echo "== dpkg status: Sileo installed"
tar -xOf "$J/sileo/control.tar" ./control | sed '/^$/d' > "$TMP/status"
printf 'Status: install ok installed\n\n' >> "$TMP/status"
"$A" push "$(w "$TMP/status")" /tmp/sileo.status >/dev/null
"$A" shell 'cat /tmp/sileo.status >> /tmp/ios/private/preboot/procursus/Library/dpkg/status; rm /tmp/sileo.status'

echo "== first-boot job"
cat > "$TMP/negroni-jb.sh" <<'EOF'
#!/var/jb/bin/sh
# com.negroni.jbprep: bootstrap preparation once, then register the prefix's applications.
export PATH=/var/jb/usr/sbin:/var/jb/usr/bin:/var/jb/sbin:/var/jb/bin:/usr/sbin:/usr/bin:/sbin:/bin
if [ -e /var/jb/prep_bootstrap.sh ]; then
	NO_PASSWORD_PROMPT=1 /var/jb/bin/sh /var/jb/prep_bootstrap.sh
fi
/var/jb/usr/sbin/pwd_mkdb -p /var/jb/etc/master.passwd >/dev/null 2>&1
sleep 40                                    # LaunchServices is not ready right after SpringBoard
/var/jb/usr/bin/uicache -p /var/jb/Applications/Sileo.app
EOF
"$A" push "$(w "$TMP/negroni-jb.sh")" /tmp/ios/private/preboot/procursus/negroni-jb.sh >/dev/null
"$A" shell 'chmod 0755 /tmp/ios/private/preboot/procursus/negroni-jb.sh; chown 0:0 /tmp/ios/private/preboot/procursus/negroni-jb.sh'

"$A" pull /tmp/ios/System/Library/xpc/launchd.plist "$(w "$TMP/launchd.plist")" >/dev/null
[ -f "$TMP/launchd.plist.orig" ] || cp "$TMP/launchd.plist" "$TMP/launchd.plist.orig"
python3 - "$TMP/launchd.plist" <<'EOF'
import plistlib, sys
p = sys.argv[1]
d = plistlib.load(open(p, 'rb'))
jobs = d['LaunchDaemons']
jobs['/System/Library/LaunchDaemons/com.negroni.jbprep.plist'] = {
    'Label': 'com.negroni.jbprep',
    'ProgramArguments': ['/var/jb/bin/sh', '/var/jb/negroni-jb.sh'],
    'RunAtLoad': True,
    'UserName': 'root',
    'StandardOutPath': '/var/jb/var/log/negroni-jb.log',
    'StandardErrorPath': '/var/jb/var/log/negroni-jb.log',
}
plistlib.dump(d, open(p, 'wb'), fmt=plistlib.FMT_BINARY)
print('launchd.plist: com.negroni.jbprep added,', len(jobs), 'jobs')
EOF
"$A" shell 'cp /tmp/ios/System/Library/xpc/launchd.plist /tmp/ios/System/Library/xpc/launchd.plist.before-sileo 2>/dev/null || true'
"$A" push "$(w "$TMP/launchd.plist")" /tmp/ios/System/Library/xpc/launchd.plist >/dev/null
"$A" shell 'mkdir -p /tmp/ios/private/preboot/procursus/var/log; sync; umount /tmp/ios && echo "== done, root unmounted"'
