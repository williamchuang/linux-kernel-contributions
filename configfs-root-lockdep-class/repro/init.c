// SPDX-License-Identifier: GPL-2.0
/*
 * PID 1 for a QEMU initramfs: exercise the three configfs operations that
 * lockdep chains into the cycle reported by syzbot c75a7db5ca6ae96ed2d4.
 *
 *  A. symlink in a UVC streaming header:   dir i_rwsem -> su_mutex
 *  B. mkdir functions/tcm.0:               su_mutex -> func_lock
 *                                          -> tpg_instances_lock
 *  C. mkdir target/usb_gadget/<wwn>/tpgt_1: tpg_instances_lock
 *                                          -> configfs root i_rwsem
 *
 * It then takes the root lock in the other ways userspace can (readdir,
 * chmod, mkdir in the root) and tears everything down again.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <dirent.h>

#define CFS "/sys/kernel/config"
#define G   CFS "/usb_gadget/g1"
#define UVC G "/functions/uvc.0/streaming"
#define TGT CFS "/target/usb_gadget"
#define WWN TGT "/naa.0123456789abcdef"

static void step(const char *what, int r)
{
	printf("REPRO: %-48s %s\n", what, r ? strerror(errno) : "ok");
	fflush(stdout);
}

int main(void)
{
	mount("proc", "/proc", "proc", 0, "");
	mount("sysfs", "/sys", "sysfs", 0, "");
	mount("devtmpfs", "/dev", "devtmpfs", 0, "");
	step("mount configfs", mount("configfs", CFS, "configfs", 0, ""));

	step("mkdir gadget g1", mkdir(G, 0755));
	step("mkdir functions/uvc.0", mkdir(G "/functions/uvc.0", 0755));
	step("mkdir streaming/uncompressed/u", mkdir(UVC "/uncompressed/u", 0755));
	step("mkdir streaming/header/h", mkdir(UVC "/header/h", 0755));
	step("A: symlink header/h/u -> uncompressed/u",
	     symlink(UVC "/uncompressed/u", UVC "/header/h/u"));

	step("B: mkdir functions/tcm.0", mkdir(G "/functions/tcm.0", 0755));

	step("mkdir target/usb_gadget", mkdir(TGT, 0755));
	step("mkdir target/usb_gadget/naa.<wwn>", mkdir(WWN, 0755));
	step("C: mkdir naa.<wwn>/tpgt_1", mkdir(WWN "/tpgt_1", 0755));

	/* Other operations that take the configfs root lock. */
	{
		DIR *d = opendir(CFS);
		struct dirent *e;
		int n = 0;

		while (d && (e = readdir(d)))
			n++;
		if (d)
			closedir(d);
		printf("REPRO: readdir of configfs root: %d entries\n", n);
	}
	step("chmod configfs root", chmod(CFS, 0755));
	step("mkdir in configfs root (expect EPERM)", mkdir(CFS "/x", 0755));

	/* Tear everything down again (rmdir, unlink, undepend paths). */
	step("rmdir tpgt_1", rmdir(WWN "/tpgt_1"));
	step("rmdir naa.<wwn>", rmdir(WWN));
	step("rmdir target/usb_gadget", rmdir(TGT));
	step("rmdir functions/tcm.0", rmdir(G "/functions/tcm.0"));
	step("unlink header/h/u", unlink(UVC "/header/h/u"));
	step("rmdir header/h", rmdir(UVC "/header/h"));
	step("rmdir uncompressed/u", rmdir(UVC "/uncompressed/u"));
	step("rmdir functions/uvc.0", rmdir(G "/functions/uvc.0"));
	step("rmdir gadget g1", rmdir(G));
	step("umount configfs", umount(CFS));

	printf("REPRO: done\n");
	fflush(stdout);
	sync();
	reboot(RB_POWER_OFF);
	return 0;
}
