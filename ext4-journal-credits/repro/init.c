// SPDX-License-Identifier: GPL-2.0
/* Minimal init for UML: reproduce two ext4 journal credit shortfalls
 * (syzbot e8180f5590685cbc9037).
 *   /dev/ubda: ext4 with -O quota       -> EXT4_IOC_SWAP_BOOT path
 *   /dev/ubdb: ext4 with -O inline_data,orphan_file -> short-copy inline write
 *   /dev/ubdc: ext4 with -O inline_data (superblock orphan list) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define EXT4_IOC_SWAP_BOOT _IO('f', 17)

static void say(const char *s) { printf("REPRO: %s\n", s); fflush(stdout); }

static void test_swap_boot(void)
{
	char buf[4096 * 4];
	int fd, i, r;

	if (mount("/dev/ubda", "/mnt/a", "ext4", 0, "")) { perror("mount a"); return; }
	fd = open("/mnt/a/f", O_CREAT | O_RDWR, 0644);
	memset(buf, 'x', sizeof(buf));
	for (i = 0; i < 4; i++)
		if (write(fd, buf, sizeof(buf)) < 0) perror("write");
	fsync(fd);
	sync();           /* commit, so the next handle starts in a fresh transaction */
	sleep(1);
	sync();
	say("swap_boot: issuing EXT4_IOC_SWAP_BOOT");
	r = ioctl(fd, EXT4_IOC_SWAP_BOOT);
	printf("REPRO: swap_boot: ioctl returned %d (%s)\n", r, r ? strerror(errno) : "ok");
	fflush(stdout);
	close(fd);
	sync();
	umount("/mnt/a");
}

static void test_inline_short_write(const char *dev, int unpriv)
{
	long pg = sysconf(_SC_PAGESIZE);
	char *map;
	int fd;
	ssize_t r;

	printf("REPRO: inline on %s\n", dev);
	if (mount(dev, "/mnt/b", "ext4", MS_LAZYTIME, "")) { perror("mount b"); return; }
	fd = open("/mnt/b/g", O_CREAT | O_RDWR, 0644);
	fsync(fd);
	sync();
	sleep(1);
	sync();
	/* two pages, second one unmapped: buffer straddles the hole */
	map = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	memset(map, 'y', pg);
	munmap(map + pg, pg);
	if (unpriv) {
		pid_t pid;

		fchmod(fd, 0666);
		fsync(fd);
		close(fd);
		sync();
		sleep(1);
		sync();
		pid = fork();
		if (pid == 0) {
			if (setgid(65534) || setuid(65534)) { perror("setuid"); _exit(1); }
			fd = open("/mnt/b/g", O_RDWR);
			printf("REPRO: inline: uid=%d euid=%d\n", getuid(), geteuid());
			say("inline: issuing short-copy write (10 of 21 bytes readable)");
			r = pwrite(fd, map + pg - 10, 21, 0);
			printf("REPRO: inline: pwrite returned %zd (%s)\n", r, r < 0 ? strerror(errno) : "ok");
			fflush(stdout);
			_exit(0);
		}
		waitpid(pid, NULL, 0);
		fd = open("/mnt/b/g", O_RDONLY);
	} else {
		say("inline: issuing short-copy write (10 of 21 bytes readable)");
		r = pwrite(fd, map + pg - 10, 21, 0);
		printf("REPRO: inline: pwrite returned %zd (%s)\n", r, r < 0 ? strerror(errno) : "ok");
		fflush(stdout);
	}
	close(fd);
	sync();
	umount("/mnt/b");
}

int main(void)
{
	mount("proc", "/proc", "proc", 0, "");
	mount("devtmpfs", "/dev", "devtmpfs", 0, "");
	const char *t = getenv("REPRO");	/* swap | inline_of | inline_sb | inline_user | all */

	if (!t)
		t = "all";
	printf("REPRO: test=%s\n", t);
	if (!strcmp(t, "swap") || !strcmp(t, "all"))
		test_swap_boot();
	if (!strcmp(t, "inline_of") || !strcmp(t, "all"))
		test_inline_short_write("/dev/ubdb", 0);
	if (!strcmp(t, "inline_sb") || !strcmp(t, "all"))
		test_inline_short_write("/dev/ubdc", 0);
	if (!strcmp(t, "inline_user"))
		test_inline_short_write("/dev/ubdb", 1);
	say("done");
	sync();
	reboot(RB_POWER_OFF);
	return 0;
}
