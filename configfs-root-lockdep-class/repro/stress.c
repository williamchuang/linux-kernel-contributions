// SPDX-License-Identifier: GPL-2.0
/*
 * Concurrency stress for the locks in syzbot c75a7db5ca6ae96ed2d4.
 * After the setup of init.c, three processes run for a fixed time:
 *   1. mkdir/rmdir target/usb_gadget/naa.<wwn>/tpgt_1
 *      (usbg_make_tpg: tpg_instances_lock -> configfs root lock, and
 *       configfs_undepend_item_unlocked on rmdir)
 *   2. symlink/unlink in the UVC streaming header
 *      (gadget dir lock -> gadget su_mutex)
 *   3. readdir and chmod of the configfs root
 *      (root lock taken shared and exclusive)
 * A real deadlock among them would hang a worker; the parent then
 * reports the stall instead of the iteration counts.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/reboot.h>

#define CFS "/sys/kernel/config"
#define G   CFS "/usb_gadget/g1"
#define UVC G "/functions/uvc.0/streaming"
#define TGT CFS "/target/usb_gadget"
#define WWN TGT "/naa.0123456789abcdef"
#define SECONDS 60

static volatile long *count;

static void worker(int id)
{
	DIR *d;

	for (;;) {
		switch (id) {
		case 0:
			if (!mkdir(WWN "/tpgt_1", 0755))
				rmdir(WWN "/tpgt_1");
			break;
		case 1:
			if (!symlink(UVC "/uncompressed/u", UVC "/header/h/u"))
				unlink(UVC "/header/h/u");
			break;
		case 2:
			d = opendir(CFS);
			while (d && readdir(d))
				;
			if (d)
				closedir(d);
			chmod(CFS, 0755);
			break;
		}
		count[id]++;
	}
}

int main(void)
{
	pid_t pid[3];
	long before[3];
	int i, secs, stalled = 0;

	mount("proc", "/proc", "proc", 0, "");
	mount("sysfs", "/sys", "sysfs", 0, "");
	mount("devtmpfs", "/dev", "devtmpfs", 0, "");
	mount("configfs", CFS, "configfs", 0, "");
	mkdir(G, 0755);
	mkdir(G "/functions/uvc.0", 0755);
	mkdir(UVC "/uncompressed/u", 0755);
	mkdir(UVC "/header/h", 0755);
	mkdir(G "/functions/tcm.0", 0755);
	mkdir(TGT, 0755);
	mkdir(WWN, 0755);

	count = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	for (i = 0; i < 3; i++) {
		pid[i] = fork();
		if (!pid[i])
			worker(i);
	}
	for (i = 0; i < SECONDS; i++) {
		int j;

		for (j = 0; j < 3; j++)
			before[j] = count[j];
		sleep(1);
		for (j = 0; j < 3; j++)
			if (count[j] == before[j])
				stalled = 1;
		if (stalled)
			break;
	}
	secs = i;
	for (i = 0; i < 3; i++)
		kill(pid[i], SIGKILL);
	for (i = 0; i < 3; i++)
		waitpid(pid[i], NULL, 0);
	printf("STRESS: %s after %d s; iterations: tpgt mkdir/rmdir %ld, "
	       "symlink/unlink %ld, root readdir+chmod %ld\n",
	       stalled ? "a worker STALLED" : "no stall", secs,
	       count[0], count[1], count[2]);
	fflush(stdout);
	sync();
	reboot(RB_POWER_OFF);
	return 0;
}
