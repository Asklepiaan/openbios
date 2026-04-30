/* tag: openbios loader prototypes for x86
 *
 * Copyright (C) 2004 Stefan Reinauer
 *
 * See the file "COPYING" for further information about
 * the copyright and warranty status of this work.
 */

/* linux_load.c */
int linux_load(struct sys_info *info, const char *file, const char *cmdline);

/* context.c */
extern struct context *__context;
unsigned int start_elf(unsigned long entry_point, unsigned long param);
unsigned int start_raw(unsigned long entry_point, unsigned long stack,
		unsigned long stack_size);
unsigned int start_raw_openfirmware(unsigned long entry_point,
		unsigned long stack,
		unsigned long stack_size,
		unsigned long ramdisk_base,
		unsigned long ramdisk_size);

/* boot.c */
extern void boot(void);
extern void go(void);
extern void winterboot(void);
