// SPDX-License-Identifier: GPL-2.0
/*
 * hmos first stage: replaces the HarmonyOS NEXT ramdisk on OnePlus SM8450.
 *
 * userdata (ext4) layout:
 *   /hmos/<part>.img          NEXT EROFS images (system, vendor, sys_prod, ...)
 *   /hmos/override/<path>     files bind-mounted over /<path> in NEXT
 *   /hmos/log/boot.log        kernel log of the current boot (previous: boot.1.log)
 *
 * hmos.mode=boot (default): mount the images like NEXT's fstab.required does
 *   (system as /), start a kmsg logger (file + USB serial), switch root and
 *   exec NEXT's "init --second-stage".
 * hmos.mode=log: show the last boot log on screen (volume keys scroll) and
 *   send it over USB serial.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/loop.h>

#define UD "/ud"
#define NEWROOT "/usr"
#define LOGDIR UD "/hmos/log"
#define LOG_MAX (64 << 20)

static int kmsg_fd = -1;

static void say(const char *fmt, ...)
{
	char buf[512], k[540];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if (kmsg_fd >= 0) {
		n = snprintf(k, sizeof(k), "<3>hmos: %s\n", buf);
		write(kmsg_fd, k, n < (int)sizeof(k) ? n : (int)sizeof(k) - 1);
	} else {
		dprintf(1, "hmos: %s\n", buf);
	}
}

static void hang(const char *why)
{
	say("STOPPED: %s", why);
	for (;;)
		pause();
}

static int write_file(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	int ret;

	if (fd < 0)
		return -1;
	ret = write(fd, val, strlen(val)) < 0 ? -1 : 0;
	close(fd);
	return ret;
}

static void cmdline_get(const char *key, char *val, size_t len, const char *def)
{
	char buf[4096], *p;
	size_t klen = strlen(key);
	int fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
	ssize_t n = fd >= 0 ? read(fd, buf, sizeof(buf) - 1) : -1;

	if (fd >= 0)
		close(fd);
	snprintf(val, len, "%s", def);
	if (n <= 0)
		return;
	buf[n] = '\0';
	for (p = strtok(buf, " \n"); p; p = strtok(NULL, " \n"))
		if (!strncmp(p, key, klen) && p[klen] == '=')
			snprintf(val, len, "%s", p + klen + 1);
}

/*
 * Create /dev/block/by-name/<PARTNAME> links for every partition and return
 * the device node of @want, waiting up to @timeout_s for the disk to probe.
 */
static int find_partition(const char *want, char *dev, size_t len, int timeout_s)
{
	char path[512], line[256], devname[256], partname[256];
	struct dirent *de;
	int i, found = 0;
	DIR *d;
	FILE *f;

	mkdir("/dev/block", 0755);
	mkdir("/dev/block/by-name", 0755);
	for (i = 0; i < timeout_s * 10 && !found; i++) {
		d = opendir("/sys/class/block");
		while (d && (de = readdir(d))) {
			if (de->d_name[0] == '.')
				continue;
			snprintf(path, sizeof(path), "/sys/class/block/%s/uevent", de->d_name);
			f = fopen(path, "re");
			if (!f)
				continue;
			devname[0] = partname[0] = '\0';
			while (fgets(line, sizeof(line), f)) {
				line[strcspn(line, "\n")] = '\0';
				if (!strncmp(line, "DEVNAME=", 8))
					snprintf(devname, sizeof(devname), "%s", line + 8);
				else if (!strncmp(line, "PARTNAME=", 9))
					snprintf(partname, sizeof(partname), "%s", line + 9);
			}
			fclose(f);
			if (!devname[0] || !partname[0])
				continue;
			snprintf(path, sizeof(path), "/dev/block/by-name/%s", partname);
			snprintf(line, sizeof(line), "/dev/%s", devname);
			symlink(line, path);
			if (!strcmp(partname, want)) {
				snprintf(dev, len, "/dev/%s", devname);
				found = 1;
			}
		}
		if (d)
			closedir(d);
		if (!found)
			usleep(100000);
	}
	return found ? 0 : -1;
}

static void usb_serial_setup(void)
{
	const char *g = "/sys/kernel/config/usb_gadget/hmos";
	char path[256], udc[256] = "";
	struct dirent *de;
	DIR *d;
	int i;

	mkdir("/sys/kernel/config", 0755);
	if (mount("configfs", "/sys/kernel/config", "configfs", 0, NULL) && errno != EBUSY)
		return;
	mkdir(g, 0755);
	snprintf(path, sizeof(path), "%s/idVendor", g);
	write_file(path, "0x1d6b");
	snprintf(path, sizeof(path), "%s/idProduct", g);
	write_file(path, "0x0104");
	snprintf(path, sizeof(path), "%s/strings/0x409", g);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/strings/0x409/product", g);
	write_file(path, "hmos log");
	snprintf(path, sizeof(path), "%s/configs/c.1", g);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/functions/acm.usb0", g);
	mkdir(path, 0755);
	symlink("/sys/kernel/config/usb_gadget/hmos/functions/acm.usb0",
		"/sys/kernel/config/usb_gadget/hmos/configs/c.1/acm.usb0");
	for (i = 0; i < 50 && !udc[0]; i++) {
		d = opendir("/sys/class/udc");
		while (d && (de = readdir(d)))
			if (de->d_name[0] != '.')
				snprintf(udc, sizeof(udc), "%s", de->d_name);
		if (d)
			closedir(d);
		if (!udc[0])
			usleep(100000);
	}
	snprintf(path, sizeof(path), "%s/UDC", g);
	if (udc[0] && write_file(path, udc) == 0)
		say("USB serial log on %s", udc);
	else
		say("no USB device controller, USB serial log disabled");
}

/* kmsg record "pri,seq,usec,flags;text\n" -> "[    s.uuuuuu] text\n" */
static int format_kmsg(const char *rec, ssize_t n, char *out, size_t len)
{
	unsigned long long usec = 0;
	const char *semi = memchr(rec, ';', n);
	const char *text;
	size_t tlen;

	if (!semi)
		return 0;
	sscanf(rec, "%*u,%*u,%llu", &usec);
	text = semi + 1;
	tlen = rec + n - text;
	/* drop continuation lines (" KEY=value") */
	if (memchr(text, '\n', tlen))
		tlen = (const char *)memchr(text, '\n', tlen) - text;
	return snprintf(out, len, "[%5llu.%06llu] %.*s\n", usec / 1000000, usec % 1000000,
			(int)tlen, text);
}

/*
 * Send the log file from *sent up to size to the USB serial port. The gadget tty only takes what
 * fits in its buffer (EAGAIN while no terminal is reading), so the rest stays queued in the file.
 */
static void serial_drain(int gfd, int rfd, off_t size, off_t *sent)
{
	char buf[4096];

	while (gfd >= 0 && rfd >= 0 && *sent < size) {
		size_t want = size - *sent < (off_t)sizeof(buf) ? (size_t)(size - *sent) : sizeof(buf);
		ssize_t n = pread(rfd, buf, want, *sent), w;

		if (n <= 0)
			return;
		w = write(gfd, buf, n);
		if (w <= 0)
			return;
		*sent += w;
	}
}

/*
 * Child process: copy the kernel log to the log file and USB serial forever.
 * All files are opened before the parent continues: after the switch root
 * the child's view of /dev is gone, so it can never reopen anything.
 */
static void start_logger(void)
{
	char rec[8192], line[8400], ready = 1;
	struct pollfd pfd;
	int kfd, lfd, rfd, gfd = -1, unsynced = 0, n, i, sync_pipe[2];
	ssize_t r;
	off_t size = 0, sent = 0;
	pid_t pid;

	if (pipe2(sync_pipe, O_CLOEXEC))
		return;
	pid = fork();
	if (pid != 0) {
		close(sync_pipe[1]);
		if (pid > 0)
			read(sync_pipe[0], &ready, 1);
		close(sync_pipe[0]);
		return;
	}
	close(sync_pipe[0]);
	prctl(PR_SET_NAME, "hmos-logger");
	kfd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	lfd = open(LOGDIR "/boot.log", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	/* USB serial is fed from the file: what was logged before a terminal opened COM is replayed */
	rfd = open(LOGDIR "/boot.log", O_RDONLY | O_CLOEXEC);
	for (i = 0; i < 20 && gfd < 0 && access("/sys/class/udc", F_OK) == 0; i++) {
		gfd = open("/dev/ttyGS0", O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
		if (gfd < 0)
			usleep(50000);
	}
	write(sync_pipe[1], &ready, 1);
	close(sync_pipe[1]);
	if (kfd < 0)
		_exit(1);
	pfd.fd = kfd;
	pfd.events = POLLIN;
	for (;;) {
		if (poll(&pfd, 1, 200) <= 0) {
			if (unsynced && lfd >= 0)
				fdatasync(lfd);
			unsynced = 0;
			serial_drain(gfd, rfd, size, &sent);
			continue;
		}
		while ((r = read(kfd, rec, sizeof(rec) - 1)) > 0 || (r < 0 && errno == EPIPE)) {
			if (r <= 0)
				continue; /* EPIPE: records overwritten, keep going */
			n = format_kmsg(rec, r, line, sizeof(line));
			if (n <= 0)
				continue;
			if (n > (int)sizeof(line) - 1)
				n = sizeof(line) - 1;
			if (lfd >= 0 && size < LOG_MAX) {
				size += write(lfd, line, n);
				if (++unsynced >= 32) {
					fdatasync(lfd);
					unsynced = 0;
				}
			}
		}
		serial_drain(gfd, rfd, size, &sent);
	}
}

static int loop_mount(const char *img, const char *target)
{
	struct loop_config cfg = { 0 };
	char dev[32];
	int ctl, lfd = -1, ifd, nr, i, ret;

	ifd = open(img, O_RDONLY | O_CLOEXEC);
	if (ifd < 0)
		return -errno;
	ctl = open("/dev/loop-control", O_RDWR | O_CLOEXEC);
	nr = ctl >= 0 ? ioctl(ctl, LOOP_CTL_GET_FREE) : -1;
	if (ctl >= 0)
		close(ctl);
	if (nr < 0) {
		close(ifd);
		return -ENODEV;
	}
	snprintf(dev, sizeof(dev), "/dev/loop%d", nr);
	for (i = 0; i < 50 && lfd < 0; i++) {
		lfd = open(dev, O_RDWR | O_CLOEXEC);
		if (lfd < 0)
			usleep(20000);
	}
	if (lfd < 0) {
		close(ifd);
		return -ENODEV;
	}
	cfg.fd = ifd;
	cfg.info.lo_flags = LO_FLAGS_READ_ONLY | LO_FLAGS_AUTOCLEAR;
	ret = ioctl(lfd, LOOP_CONFIGURE, &cfg) ? -errno : 0;
	close(ifd);
	if (!ret && mount(dev, target, "erofs", MS_RDONLY, NULL))
		ret = -errno;
	close(lfd);
	return ret;
}

/* Bind every regular file under @src over the same relative path in @dst. */
/*
 * Files shipped in the initramfs under /override (boot image, e.g. the negroni display backends) are
 * copied into UD/hmos/override before the overrides are bind-mounted, so they need no userdata flash.
 */
static void copy_tree(const char *src, const char *dst)
{
	DIR *d = opendir(src);
	struct dirent *de;
	char s[512], t[512], buf[65536];

	if (!d)
		return;
	mkdir(dst, 0755);
	while ((de = readdir(d))) {
		struct stat st;
		int in, out;
		ssize_t n;

		if (de->d_name[0] == '.')
			continue;
		snprintf(s, sizeof(s), "%s/%s", src, de->d_name);
		snprintf(t, sizeof(t), "%s/%s", dst, de->d_name);
		if (lstat(s, &st))
			continue;
		if (S_ISDIR(st.st_mode)) {
			copy_tree(s, t);
			continue;
		}
		in = open(s, O_RDONLY | O_CLOEXEC);
		out = open(t, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, st.st_mode & 07777);
		while (in >= 0 && out >= 0 && (n = read(in, buf, sizeof(buf))) > 0)
			write(out, buf, n);
		if (out >= 0) {
			fchmod(out, st.st_mode & 07777);
			fsync(out);
			close(out);
		}
		if (in >= 0)
			close(in);
		say("boot override %s", t + strlen(UD "/hmos/override"));
	}
	closedir(d);
}

static void bind_overrides(const char *src, const char *dst)
{
	char s[512], t[512];
	struct dirent *de;
	struct stat st;
	DIR *d = opendir(src);

	while (d && (de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		snprintf(s, sizeof(s), "%s/%s", src, de->d_name);
		snprintf(t, sizeof(t), "%s/%s", dst, de->d_name);
		if (lstat(s, &st))
			continue;
		if (S_ISDIR(st.st_mode)) {
			bind_overrides(s, t);
		} else if (S_ISREG(st.st_mode)) {
			if (mount(s, t, NULL, MS_BIND, NULL))
				say("override %s: %s", t + strlen(NEWROOT), strerror(errno));
			else
				say("override %s", t + strlen(NEWROOT));
		}
	}
	if (d)
		closedir(d);
}

static void rotate_logs(void)
{
	mkdir(UD "/hmos", 0755);
	mkdir(LOGDIR, 0755);
	rename(LOGDIR "/boot.1.log", LOGDIR "/boot.2.log");
	rename(LOGDIR "/boot.log", LOGDIR "/boot.1.log");
}

static void boot_next(void)
{
	static const struct {
		const char *img, *target;
		int required;
	} parts[] = {
		{ "system", "", 1 },
		{ "vendor", "/vendor", 1 },
		{ "sys_prod", "/sys_prod", 0 },
		{ "chip_prod", "/chip_prod", 0 },
		{ "cust", "/cust", 0 },
		{ "patch", "/patch_hw", 0 },
		{ "version", "/version", 0 },
		{ "preload", "/preload", 0 },
	};
	char img[256], target[256];
	size_t i;
	int ret;

	rotate_logs();
	usb_serial_setup();
	start_logger();

	mkdir(NEWROOT, 0755);
	for (i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
		snprintf(img, sizeof(img), UD "/hmos/%s.img", parts[i].img);
		snprintf(target, sizeof(target), NEWROOT "%s", parts[i].target);
		if (access(img, F_OK)) {
			say("%s.img not present%s", parts[i].img, parts[i].required ? "" : ", skipped");
			if (parts[i].required)
				hang("required image missing");
			continue;
		}
		ret = loop_mount(img, target);
		say("mount %s.img on %s: %s", parts[i].img, *parts[i].target ? parts[i].target : "/",
		    ret ? strerror(-ret) : "ok");
		if (ret && parts[i].required)
			hang("cannot mount required image");
	}

	/* NEXT keeps logs on a dedicated rw partition; use a dir on userdata */
	mkdir(UD "/hmos/logpart", 0775);
	mount(UD "/hmos/logpart", NEWROOT "/log", NULL, MS_BIND, NULL);

	/*
	 * /data: a directory on userdata, not a second mount of the userdata partition (that is the
	 * ext4 holding the images; NEXT's init may check or format its /data device).
	 */
	mkdir(UD "/hmos/data", 0771);
	if (mount(UD "/hmos/data", NEWROOT "/data", NULL, MS_BIND, NULL))
		say("bind /data: %s", strerror(errno));

	copy_tree("/override", UD "/hmos/override");
	/* negroni display backend: composer_host / allocator_host / render_service open the DRM nodes */
	chmod("/dev/dri/card0", 0666);
	chmod("/dev/dri/renderD128", 0666);
	bind_overrides(UD "/hmos/override", NEWROOT);

	if (mount("/dev", NEWROOT "/dev", NULL, MS_MOVE, NULL) ||
	    mount("/proc", NEWROOT "/proc", NULL, MS_MOVE, NULL) ||
	    mount("/sys", NEWROOT "/sys", NULL, MS_MOVE, NULL))
		hang("moving /dev /proc /sys failed");

	if (chdir(NEWROOT) || mount(".", "/", NULL, MS_MOVE, NULL) || chroot(".") || chdir("/"))
		hang("switch root failed");

	say("starting NEXT init --second-stage");
	if (kmsg_fd >= 0)
		close(kmsg_fd);
	execl("/bin/init", "/bin/init", "--second-stage", (char *)NULL);
	kmsg_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
	say("exec /bin/init: %s", strerror(errno));
	hang("NEXT init did not start");
}

/* ---- log viewer -------------------------------------------------------- */

#define ROWS 140

static char *log_buf;
static size_t log_len;
static const char **lines;
static int nlines;

static void load_log(const char *path)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	struct stat st;
	char *p;

	if (fd < 0 || fstat(fd, &st))
		return;
	log_buf = malloc(st.st_size + 1);
	log_len = log_buf ? read(fd, log_buf, st.st_size) : 0;
	close(fd);
	if (!log_buf || (ssize_t)log_len < 0)
		return;
	log_buf[log_len] = '\0';
	for (p = log_buf; *p; p++)
		if (*p == '\n')
			nlines++;
	lines = calloc(nlines + 1, sizeof(*lines));
	nlines = 0;
	for (p = log_buf; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL)
		lines[nlines++] = p;
}

static void show_page(int top)
{
	int i;

	dprintf(1, "\033[2J\033[H");
	for (i = top; i < nlines && i < top + ROWS - 2; i++)
		dprintf(1, "%.*s\n", (int)strcspn(lines[i], "\n"), lines[i]);
	dprintf(1, "--- lines %d-%d of %d  [Vol Up: back  Vol Down: forward] ---\n",
		top + 1, i, nlines);
}

static void view_log(void)
{
	struct pollfd pfd[16];
	struct input_event ev;
	char path[64];
	int n = 0, i, top, gfd;

	usb_serial_setup();
	load_log(LOGDIR "/boot.log");
	if (!nlines) {
		dprintf(1, "\nno log at " LOGDIR "/boot.log\n");
		hang("nothing to show");
	}
	/*
	 * Full log over USB serial, from a child: writes block until a terminal
	 * on the PC reads them.
	 */
	if (fork() == 0) {
		for (i = 0; i < 100; i++) {
			gfd = open("/dev/ttyGS0", O_WRONLY | O_NOCTTY | O_CLOEXEC);
			if (gfd >= 0) {
				write(gfd, log_buf, log_len);
				dprintf(gfd, "\n--- end of boot.log (%d lines) ---\n", nlines);
				close(gfd);
				break;
			}
			usleep(100000);
		}
		_exit(0);
	}
	write_file("/proc/sys/kernel/printk", "1");
	for (i = 0; i < 16; i++) {
		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		pfd[n].fd = open(path, O_RDONLY | O_CLOEXEC);
		pfd[n].events = POLLIN;
		if (pfd[n].fd >= 0)
			n++;
	}
	top = nlines > ROWS - 2 ? nlines - (ROWS - 2) : 0;
	show_page(top);
	for (;;) {
		if (poll(pfd, n, -1) <= 0)
			continue;
		for (i = 0; i < n; i++) {
			if (!(pfd[i].revents & POLLIN) ||
			    read(pfd[i].fd, &ev, sizeof(ev)) != sizeof(ev) ||
			    ev.type != EV_KEY || ev.value != 1)
				continue;
			if (ev.code == KEY_VOLUMEUP)
				top = top > ROWS / 2 ? top - ROWS / 2 : 0;
			else if (ev.code == KEY_VOLUMEDOWN && top + ROWS / 2 < nlines)
				top += ROWS / 2;
			else
				continue;
			show_page(top);
		}
	}
}

#ifndef JOURNAL_SB_BLOCK
#define JOURNAL_SB_BLOCK 0
#endif

/*
 * Raw look at userdata before mounting: ext4 superblock (magic 0xef53, state, write time, mount
 * count) and the journal superblock (jbd2 magic c03b3998) at the block the image put it.
 * A clean image just flashed: state 1, mount count 0, journal magic present.
 */
static void raw_check(const char *dev)
{
	unsigned char sb[1024], jb[16];
	int fd = open(dev, O_RDONLY | O_CLOEXEC);

	if (fd < 0) {
		say("raw: open %s: %s", dev, strerror(errno));
		return;
	}
	if (pread(fd, sb, sizeof(sb), 1024) != (ssize_t)sizeof(sb)) {
		say("raw: superblock read: %s", strerror(errno));
		close(fd);
		return;
	}
#define LE16(o) ((unsigned)sb[o] | (unsigned)sb[(o) + 1] << 8)
#define LE32(o) (LE16(o) | (unsigned)LE16((o) + 2) << 16)
	say("raw: ext4 magic %04x state %u blocks %u mnt_count %u wtime %u mtime %u journal_inum %u",
	    LE16(0x38), LE16(0x3a), LE32(0x04), LE16(0x34), LE32(0x30), LE32(0x2c), LE32(0xe0));
	if (JOURNAL_SB_BLOCK) {
		if (pread(fd, jb, sizeof(jb), (off_t)JOURNAL_SB_BLOCK * 4096) == (ssize_t)sizeof(jb))
			say("raw: journal block %u: %02x%02x%02x%02x %02x%02x%02x%02x (want c03b3998 00000004)",
			    JOURNAL_SB_BLOCK, jb[0], jb[1], jb[2], jb[3], jb[4], jb[5], jb[6], jb[7]);
		else
			say("raw: journal block read: %s", strerror(errno));
	}
	close(fd);
}

int main(void)
{
	char mode[16], ud[64];

	mount("proc", "/proc", "proc", 0, NULL);
	mount("sysfs", "/sys", "sysfs", 0, NULL);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
	/* NEXT's init loads its SELinux policy only if security_getenforce() works: selinuxfs must be mounted */
	if (mount("selinuxfs", "/sys/fs/selinux", "selinuxfs", 0, NULL))
		say("mount selinuxfs: %s", strerror(errno));
	kmsg_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);

	cmdline_get("hmos.mode", mode, sizeof(mode), "boot");
	say("first stage, mode=%s", mode);

	if (find_partition("userdata", ud, sizeof(ud), 30))
		hang("userdata partition not found");
	mkdir(UD, 0755);
	raw_check(ud);
	if (mount(ud, UD, "ext4", MS_NOATIME, NULL)) {
		say("mount userdata: %s; retrying read-only without the journal", strerror(errno));
		if (mount(ud, UD, "ext4", MS_NOATIME | MS_RDONLY, "noload"))
			hang("cannot mount userdata as ext4 (flash userdata.img first)");
	}
	say("userdata %s mounted", ud);

	if (!strcmp(mode, "log"))
		view_log();
	boot_next();
	return 0;
}
