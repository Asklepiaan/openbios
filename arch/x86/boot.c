/* tag: openbios boot command for x86
 *
 * Copyright (C) 2003-2004 Stefan Reinauer
 *
 * See the file "COPYING" for further information about
 * the copyright and warranty status of this work.
 */

#undef BOOTSTRAP
#include "config.h"
#include "libopenbios/bindings.h"
#include "arch/common/nvram.h"
#include "libc/diskio.h"
#include "libc/string.h"
#include "asm/io.h"
#include "kernel/kernel.h"
#include "libopenbios/sys_info.h"
#include "boot.h"

#define WINTERBOOT_PAYLOAD_PHYSICAL 0x00020000u
#define WINTERBOOT_STACK_PHYSICAL 0x00400000u
#define WINTERBOOT_STACK_SIZE 0x0000f000u
#define WINTERBOOT_PAYLOAD_MAGIC 0x504c4d57u
#define WINTERBOOT_PAYLOAD_VERSION 1u
#define WINTERBOOT_PAYLOAD_HEADER_SIZE 16u
#define WINTERBOOT_COMPRESSED_MAGIC 0x50434257u
#define WINTERBOOT_COMPRESSED_VERSION 1u
#define WINTERBOOT_COMPRESSED_HEADER_SIZE 16u
#define WINTERBOOT_RUNTIME_STATE_PHYSICAL 0x00006000u
#define WINTERBOOT_RUNTIME_OF_RAMDISK_BASE 568u
#define WINTERBOOT_RUNTIME_OF_RAMDISK_SIZE 572u
#define WINTERBOOT_POLL_COUNT 0x1000u
#define WINTERBOOT_KEYBOARD_STATUS_PORT 0x64
#define WINTERBOOT_KEYBOARD_DATA_PORT 0x60
#define WINTERBOOT_KEYBOARD_DATA_READY 0x01
#define WINTERBOOT_O_SCANCODE 0x18

extern const unsigned char winterboot_payload_start[];
extern const unsigned char winterboot_payload_end[];

static uint32_t winterboot_read_le32(const unsigned char *data)
{
	return (uint32_t)data[0] |
		((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) |
		((uint32_t)data[3] << 24);
}

static int winterboot_o_key_requested(void)
{
	unsigned int i;
	int extended = 0;

	for (i = 0; i < WINTERBOOT_POLL_COUNT; ++i) {
		if ((inb(WINTERBOOT_KEYBOARD_STATUS_PORT) & WINTERBOOT_KEYBOARD_DATA_READY) != 0) {
			const unsigned char code = inb(WINTERBOOT_KEYBOARD_DATA_PORT);
			if (code == 0xe0) {
				extended = 1;
			} else {
				if (!extended && code == WINTERBOOT_O_SCANCODE) {
					return 1;
				}
				extended = 0;
			}
		}
		(void)inb(0x80);
	}

	return 0;
}

static int winterboot_read_packed_length(const unsigned char **cursor,
		const unsigned char *end,
		uint32_t *length)
{
	for (;;) {
		unsigned char next;
		if (*cursor >= end) {
			return 0;
		}
		next = **cursor;
		++*cursor;
		if (*length > 0xffffffffu - next) {
			return 0;
		}
		*length += next;
		if (next != 255) {
			return 1;
		}
	}
}

static int winterboot_decompress_payload(void *target, uint32_t *out_size)
{
	const unsigned char *packed = winterboot_payload_start;
	const uint32_t blob_size = (uint32_t)(winterboot_payload_end - winterboot_payload_start);
	uint32_t unpacked_size;
	uint32_t packed_size;
	const unsigned char *src;
	const unsigned char *src_end;
	unsigned char *dst = target;
	unsigned char *dst_start = dst;
	unsigned char *dst_end;

	if (blob_size < WINTERBOOT_COMPRESSED_HEADER_SIZE ||
			winterboot_read_le32(packed + 0) != WINTERBOOT_COMPRESSED_MAGIC ||
			winterboot_read_le32(packed + 4) != WINTERBOOT_COMPRESSED_VERSION) {
		return 0;
	}

	unpacked_size = winterboot_read_le32(packed + 8);
	packed_size = winterboot_read_le32(packed + 12);
	if (packed_size > blob_size - WINTERBOOT_COMPRESSED_HEADER_SIZE) {
		return 0;
	}

	src = packed + WINTERBOOT_COMPRESSED_HEADER_SIZE;
	src_end = src + packed_size;
	dst_end = dst + unpacked_size;

	while (src < src_end) {
		unsigned char token = *src++;
		uint32_t literal_length = token >> 4;
		uint32_t match_length = token & 0x0f;
		uint32_t offset;
		unsigned char *match;

		if (literal_length == 15 &&
				!winterboot_read_packed_length(&src, src_end, &literal_length)) {
			return 0;
		}
		if ((uint32_t)(dst_end - dst) < literal_length ||
				(uint32_t)(src_end - src) < literal_length) {
			return 0;
		}
		while (literal_length--) {
			*dst++ = *src++;
		}
		if (src == src_end) {
			break;
		}
		if (src_end - src < 2) {
			return 0;
		}
		offset = (uint32_t)src[0] | ((uint32_t)src[1] << 8);
		src += 2;
		if (offset == 0 || (uint32_t)(dst - dst_start) < offset) {
			return 0;
		}

		match_length += 4;
		if ((token & 0x0f) == 15 &&
				!winterboot_read_packed_length(&src, src_end, &match_length)) {
			return 0;
		}
		if ((uint32_t)(dst_end - dst) < match_length) {
			return 0;
		}
		match = dst - offset;
		while (match_length--) {
			*dst++ = *match++;
		}
	}

	if (src != src_end || dst != dst_end) {
		return 0;
	}
	*out_size = unpacked_size;
	return 1;
}

void winterboot(void)
{
	uint32_t *header;
	uint32_t *runtime_state;
	uint32_t entry;
	uint32_t image_size;
	uint32_t ramdisk_base;
	uint32_t ramdisk_size;
	void *target;

	if (winterboot_o_key_requested()) {
		printk("OpenBIOS autoboot interrupted.\n");
		return;
	}

	target = phys_to_virt(WINTERBOOT_PAYLOAD_PHYSICAL);
	if (!winterboot_decompress_payload(target, &image_size)) {
		printk("WinterBoot payload could not be decompressed.\n");
		return;
	}

	header = (uint32_t *)target;
	if (image_size < WINTERBOOT_PAYLOAD_HEADER_SIZE ||
			header[0] != WINTERBOOT_PAYLOAD_MAGIC ||
			header[1] != WINTERBOOT_PAYLOAD_VERSION ||
			header[3] < WINTERBOOT_PAYLOAD_HEADER_SIZE ||
			header[3] > image_size) {
		printk("WinterBoot payload is invalid.\n");
		return;
	}

	entry = header[2];
	if (entry < WINTERBOOT_PAYLOAD_PHYSICAL ||
			entry >= WINTERBOOT_PAYLOAD_PHYSICAL + image_size) {
		printk("WinterBoot entry point is invalid.\n");
		return;
	}

	runtime_state = (uint32_t *)phys_to_virt(WINTERBOOT_RUNTIME_STATE_PHYSICAL);
	ramdisk_base = runtime_state[WINTERBOOT_RUNTIME_OF_RAMDISK_BASE / sizeof(uint32_t)];
	ramdisk_size = runtime_state[WINTERBOOT_RUNTIME_OF_RAMDISK_SIZE / sizeof(uint32_t)];

	printk("Booting WinterBoot from OpenBIOS...\n");
	printk("WinterBoot returned 0x%x.\n",
			start_raw_openfirmware(entry,
				WINTERBOOT_STACK_PHYSICAL,
				WINTERBOOT_STACK_SIZE,
				ramdisk_base,
				ramdisk_size));
}

void go(void)
{
	ucell address, type, size;
	int image_retval = 0;

	/* Get the entry point and the type (see forth/debugging/client.fs) */
	feval("saved-program-state >sps.entry @");
	address = POP();
	feval("saved-program-state >sps.file-type @");
	type = POP();
	feval("saved-program-state >sps.file-size @");
	size = POP();

	printk("\nJumping to entry point " FMT_ucellx " for type " FMT_ucellx "...\n", address, type);

	switch (type) {
		case 0x0:
			/* Start ELF boot image */
			image_retval = start_elf(address, (uint32_t)&elf_boot_notes);
			break;

		case 0x1:
			/* Start ELF image */
			image_retval = start_elf(address, (uint32_t)NULL);
			break;

		case 0x5:
			/* Start a.out image */
			image_retval = start_elf(address, (uint32_t)NULL);
			break;

		case 0x10:
			/* Start Fcode image */
			printk("Evaluating FCode...\n");
			PUSH(address);
			PUSH(1);
			fword("byte-load");
			image_retval = 0;
			break;

		case 0x11:
			/* Start Forth image */
			PUSH(address);
			PUSH(size);
			fword("eval2");
			image_retval = 0;
			break;
	}

	printk("Image returned with return value %#x\n", image_retval);
}


void boot(void)
{
	/* No platform-specific boot code */
	return;
}
