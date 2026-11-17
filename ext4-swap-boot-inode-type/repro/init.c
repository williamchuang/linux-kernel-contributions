// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal UML init: EXT4_IOC_SWAP_BOOT against /dev/ubda, whose boot
 * loader inode (inode 5) has been given an arbitrary mode with debugfs.
 * Runs the ioctl twice so a successful first swap is followed by a
 * swap with an initialised (regular) boot loader inode.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>

#define EXT4_IOC_SWAP_BOOT _IO('f', 17)

int main(void)
{
	char buf[4096];
	int fd, i, r;

	mount("proc", "/proc", "proc", 0, "");
	mount("devtmpfs", "/dev", "devtmpfs", 0, "");
	if (mount("/dev/ubda", "/mnt/a", "ext4", 0, "")) {
		perror("REPRO: mount");
		goto out;
	}
	fd = open("/mnt/a/f", O_CREAT | O_RDWR, 0644);
	memset(buf, 'x', sizeof(buf));
	for (i = 0; i < 4; i++)
		if (write(fd, buf, sizeof(buf)) < 0)
			perror("write");
	fsync(fd);
	for (i = 1; i <= 2; i++) {
		r = ioctl(fd, EXT4_IOC_SWAP_BOOT);
		printf("REPRO: swap %d returned %d (%s)\n", i, r,
		       r ? strerror(errno) : "ok");
		fflush(stdout);
	}
	close(fd);
	umount("/mnt/a");
out:
	printf("REPRO: done\n");
	fflush(stdout);
	sync();
	reboot(RB_POWER_OFF);
	return 0;
}
