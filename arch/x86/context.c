/* tag: x86  context switching
 *
 * 2003-10 by SONE Takeshi
 *
 * See the file "COPYING" for further information about
 * the copyright and warranty status of this work.
 */

#include "config.h"
#include "kernel/kernel.h"
#include "segment.h"
#include "context.h"
#include "asm/io.h"
#include "libopenbios/fontdata.h"
#include "libopenbios/sys_info.h"
#include "boot.h"
#include "openbios.h"

#define MAIN_STACK_SIZE 16384
#define IMAGE_STACK_SIZE 4096

#define WINTERBOOT_SERVICE_POLL_KEY 1
#define WINTERBOOT_SERVICE_GET_ROM_FONT 2
#define WINTERBOOT_SERVICE_READ_SECTORS 3
#define WINTERBOOT_SERVICE_QUERY_DRIVE 4
#define WINTERBOOT_SERVICE_WRITE_SECTORS 5
#define WINTERBOOT_SERVICE_SET_MODE13 6
#define WINTERBOOT_SERVICE_WAIT_USEC 7
#define WINTERBOOT_SERVICE_SET_TEXT_MODE3 8
#define WINTERBOOT_SERVICE_SET_WXGA_MODE 9

#define WINTERBOOT_ATA_SECTOR_SIZE 512
#define WINTERBOOT_ATA_STATUS_BSY 0x80
#define WINTERBOOT_ATA_STATUS_DRQ 0x08
#define WINTERBOOT_ATA_STATUS_DF 0x20
#define WINTERBOOT_ATA_STATUS_ERR 0x01
#define WINTERBOOT_ATA_CMD_READ_SECTORS 0x20
#define WINTERBOOT_ATA_CMD_WRITE_SECTORS 0x30
#define WINTERBOOT_ATA_CMD_IDENTIFY 0xec
#define WINTERBOOT_ATA_CMD_FLUSH_CACHE 0xe7
#define WINTERBOOT_ATA_PRIMARY_BASE 0x1f0
#define WINTERBOOT_ATA_SECONDARY_BASE 0x170
#define WINTERBOOT_ATA_PRIMARY_CONTROL 0x3f6
#define WINTERBOOT_ATA_SECONDARY_CONTROL 0x376
#define WINTERBOOT_ATA_WAIT_LIMIT 1000000u

#define WINTERBOOT_PCI_CONFIG_ADDRESS 0xcf8
#define WINTERBOOT_PCI_CONFIG_DATA 0xcfc
#define WINTERBOOT_PCI_ENABLE 0x80000000u
#define WINTERBOOT_PCI_CLASS_MASS_STORAGE 0x01
#define WINTERBOOT_PCI_CLASS_DISPLAY 0x03
#define WINTERBOOT_PCI_SUBCLASS_SATA 0x06
#define WINTERBOOT_PCI_PROGIF_AHCI 0x01
#define WINTERBOOT_PCI_COMMAND 0x04
#define WINTERBOOT_PCI_CLASS_REVISION 0x08
#define WINTERBOOT_PCI_BAR0 0x10
#define WINTERBOOT_PCI_BAR5 0x24

#define WINTERBOOT_AHCI_MAX_DRIVES 4
#define WINTERBOOT_AHCI_CMD_READ_DMA_EXT 0x25
#define WINTERBOOT_AHCI_CMD_WRITE_DMA_EXT 0x35
#define WINTERBOOT_AHCI_CMD_IDENTIFY 0xec
#define WINTERBOOT_AHCI_CMD_FLUSH_CACHE_EXT 0xea
#define WINTERBOOT_AHCI_GHC 0x04
#define WINTERBOOT_AHCI_PI 0x0c
#define WINTERBOOT_AHCI_PORT_BASE 0x100
#define WINTERBOOT_AHCI_PORT_SIZE 0x80
#define WINTERBOOT_AHCI_PxCLB 0x00
#define WINTERBOOT_AHCI_PxCLBU 0x04
#define WINTERBOOT_AHCI_PxFB 0x08
#define WINTERBOOT_AHCI_PxFBU 0x0c
#define WINTERBOOT_AHCI_PxIS 0x10
#define WINTERBOOT_AHCI_PxCMD 0x18
#define WINTERBOOT_AHCI_PxTFD 0x20
#define WINTERBOOT_AHCI_PxSSTS 0x28
#define WINTERBOOT_AHCI_PxSERR 0x30
#define WINTERBOOT_AHCI_PxSACT 0x34
#define WINTERBOOT_AHCI_PxCI 0x38
#define WINTERBOOT_AHCI_GHC_AE 0x80000000u
#define WINTERBOOT_AHCI_CMD_ST 0x00000001u
#define WINTERBOOT_AHCI_CMD_SUD 0x00000002u
#define WINTERBOOT_AHCI_CMD_POD 0x00000004u
#define WINTERBOOT_AHCI_CMD_FRE 0x00000010u
#define WINTERBOOT_AHCI_CMD_FR 0x00004000u
#define WINTERBOOT_AHCI_CMD_CR 0x00008000u
#define WINTERBOOT_AHCI_TFD_STS_BSY 0x80u
#define WINTERBOOT_AHCI_TFD_STS_DRQ 0x08u
#define WINTERBOOT_AHCI_IS_TFES 0x40000000u
#define WINTERBOOT_AHCI_WAIT_LIMIT 1000000u
#define WINTERBOOT_AHCI_SCRATCH_PHYSICAL 0x00018000u
#define WINTERBOOT_AHCI_SCRATCH_STRIDE 0x800u
#define WINTERBOOT_AHCI_CMD_LIST_OFFSET 0x000u
#define WINTERBOOT_AHCI_FIS_OFFSET 0x400u
#define WINTERBOOT_AHCI_CMD_TABLE_OFFSET 0x500u
#define WINTERBOOT_AHCI_IDENTIFY_OFFSET 0x600u

#define WINTERBOOT_RUNTIME_STATE_PHYSICAL 0x00006000u
#define WINTERBOOT_RS_BOOT_DRIVE 104u
#define WINTERBOOT_RS_DISK_SECTOR_COUNT 132u
#define WINTERBOOT_RS_FILESYSTEM_START_LBA 280u
#define WINTERBOOT_RS_FILESYSTEM_SECTOR_COUNT 284u
#define WINTERBOOT_RS_FILESYSTEM_FLAGS 288u

#define WINTERBOOT_WBPT_MAGIC 0x54504257u
#define WINTERBOOT_WBPT_VERSION 1u
#define WINTERBOOT_WBPT_TABLE_LBA 65u
#define WINTERBOOT_WBPT_FIRST_PARTITION_LBA 66u
#define WINTERBOOT_WBPT_PARTITION_WMFS32 1u
#define WINTERBOOT_WMFS_FLAG_READONLY 1u

#define WINTERBOOT_VBE_INDEX_PORT 0x01ce
#define WINTERBOOT_VBE_DATA_PORT 0x01d0
#define WINTERBOOT_VBE_INDEX_ID 0x00
#define WINTERBOOT_VBE_INDEX_XRES 0x01
#define WINTERBOOT_VBE_INDEX_YRES 0x02
#define WINTERBOOT_VBE_INDEX_BPP 0x03
#define WINTERBOOT_VBE_INDEX_ENABLE 0x04
#define WINTERBOOT_VBE_INDEX_BANK 0x05
#define WINTERBOOT_VBE_INDEX_VIRT_WIDTH 0x06
#define WINTERBOOT_VBE_INDEX_VIRT_HEIGHT 0x07
#define WINTERBOOT_VBE_INDEX_X_OFFSET 0x08
#define WINTERBOOT_VBE_INDEX_Y_OFFSET 0x09
#define WINTERBOOT_VBE_ID0 0xb0c0
#define WINTERBOOT_VBE_ID5 0xb0c5
#define WINTERBOOT_VBE_ENABLED 0x01
#define WINTERBOOT_VBE_LFB_ENABLED 0x40
#define WINTERBOOT_WXGA_WIDTH 1280u
#define WINTERBOOT_WXGA_HEIGHT 800u
#define WINTERBOOT_WXGA_BPP 32u

struct winterboot_service_regs {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
};

static int winterboot_key_extended;
static int winterboot_key_lshift;
static int winterboot_key_rshift;
static int winterboot_key_caps;
static uint16_t winterboot_ata_identify_buffer[256];
static volatile uint32_t *winterboot_ahci_drive_ports[WINTERBOOT_AHCI_MAX_DRIVES];
static uint32_t winterboot_ahci_drive_sectors[WINTERBOOT_AHCI_MAX_DRIVES];
static unsigned int winterboot_ahci_drive_scratch[WINTERBOOT_AHCI_MAX_DRIVES];
static unsigned int winterboot_ahci_drive_count;
static int winterboot_ahci_initialized;
static int winterboot_vbe_initialized;
static uint32_t winterboot_vbe_framebuffer_physical;

extern const unsigned char winterboot_boot_volume_start[];
extern const unsigned char winterboot_boot_volume_end[];

static const unsigned char winterboot_key_normal[] = {
    0x00, 0x1b, '1', '2', '3', '4', '5', '6',
    '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\r', 0x00, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0x00, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0x00, '*',
    0x00, ' ', 0x00, 0x00, 0x00, 0x00, 0x00
};

static const unsigned char winterboot_key_shifted[] = {
    0x00, 0x1b, '!', '@', '#', '$', '%', '^',
    '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\r', 0x00, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0x00, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0x00, '*',
    0x00, ' ', 0x00, 0x00, 0x00, 0x00, 0x00
};

static void winterboot_io_delay(void)
{
    (void)inb(0x80);
}

static uint32_t winterboot_runtime_read32(uint32_t offset)
{
    const uint32_t *runtime = phys_to_virt(WINTERBOOT_RUNTIME_STATE_PHYSICAL + offset);
    return *runtime;
}

static void winterboot_store_le16(unsigned char *dest, uint16_t value)
{
    dest[0] = (unsigned char)(value & 0xffu);
    dest[1] = (unsigned char)((value >> 8) & 0xffu);
}

static void winterboot_store_le32(unsigned char *dest, uint32_t value)
{
    dest[0] = (unsigned char)(value & 0xffu);
    dest[1] = (unsigned char)((value >> 8) & 0xffu);
    dest[2] = (unsigned char)((value >> 16) & 0xffu);
    dest[3] = (unsigned char)((value >> 24) & 0xffu);
}

static uint32_t winterboot_embedded_boot_volume_size(void)
{
    return (uint32_t)(winterboot_boot_volume_end - winterboot_boot_volume_start);
}

static uint32_t winterboot_embedded_boot_volume_sectors(void)
{
    uint32_t size = winterboot_embedded_boot_volume_size();
    if (size == 0) {
	return 0;
    }
    return (size + WINTERBOOT_ATA_SECTOR_SIZE - 1u) / WINTERBOOT_ATA_SECTOR_SIZE;
}

static uint32_t winterboot_boot_filesystem_start_lba(void)
{
    uint32_t start_lba = winterboot_runtime_read32(WINTERBOOT_RS_FILESYSTEM_START_LBA);
    return start_lba != 0 ? start_lba : WINTERBOOT_WBPT_FIRST_PARTITION_LBA;
}

static int winterboot_embedded_boot_drive(uint32_t drive)
{
    uint32_t boot_drive = winterboot_runtime_read32(WINTERBOOT_RS_BOOT_DRIVE);

    if (winterboot_embedded_boot_volume_sectors() == 0) {
	return 0;
    }
    if (boot_drive < 0x80u || boot_drive > 0xffu) {
	boot_drive = 0x80u;
    }
    return drive == boot_drive;
}

static uint32_t winterboot_embedded_boot_disk_sectors(void)
{
    uint32_t runtime_sectors = winterboot_runtime_read32(WINTERBOOT_RS_DISK_SECTOR_COUNT);
    uint32_t start_lba = winterboot_boot_filesystem_start_lba();
    uint32_t boot_sectors = winterboot_embedded_boot_volume_sectors();
    uint32_t embedded_end;

    if (start_lba > 0xffffffffu - boot_sectors) {
	embedded_end = 0xffffffffu;
    } else {
	embedded_end = start_lba + boot_sectors;
    }
    return runtime_sectors > embedded_end ? runtime_sectors : embedded_end;
}

static void winterboot_embedded_write_wbpt_sector(unsigned char *sector)
{
    unsigned char *entry;
    uint32_t flags = winterboot_runtime_read32(WINTERBOOT_RS_FILESYSTEM_FLAGS) |
		     WINTERBOOT_WMFS_FLAG_READONLY;

    memset(sector, 0, WINTERBOOT_ATA_SECTOR_SIZE);
    winterboot_store_le32(sector + 0, WINTERBOOT_WBPT_MAGIC);
    winterboot_store_le16(sector + 4, WINTERBOOT_WBPT_VERSION);
    winterboot_store_le16(sector + 6, 1);
    memcpy(sector + 8, "WinterBootOS", 12);

    entry = sector + 32;
    winterboot_store_le32(entry + 0, WINTERBOOT_WBPT_PARTITION_WMFS32);
    winterboot_store_le32(entry + 4, winterboot_boot_filesystem_start_lba());
    winterboot_store_le32(entry + 8, winterboot_embedded_boot_volume_sectors());
    winterboot_store_le32(entry + 12, flags);
    memcpy(entry + 16, "firmware", sizeof("firmware"));
}

static int winterboot_embedded_boot_read(uint32_t drive, uint32_t lba,
					 uint32_t sectors,
					 uint32_t buffer_physical)
{
    unsigned char *dest;
    uint32_t filesystem_lba;
    uint32_t volume_sectors;
    uint32_t volume_size;
    uint32_t index;

    if (!winterboot_embedded_boot_drive(drive)) {
	return 0;
    }
    if (sectors == 0) {
	return 1;
    }

    dest = phys_to_virt(buffer_physical);
    filesystem_lba = winterboot_boot_filesystem_start_lba();
    volume_sectors = winterboot_embedded_boot_volume_sectors();
    volume_size = winterboot_embedded_boot_volume_size();

    for (index = 0; index < sectors; ++index) {
	uint32_t current_lba = lba + index;
	memset(dest, 0, WINTERBOOT_ATA_SECTOR_SIZE);
	if (current_lba == WINTERBOOT_WBPT_TABLE_LBA) {
	    winterboot_embedded_write_wbpt_sector(dest);
	} else if (current_lba >= filesystem_lba &&
		   current_lba < filesystem_lba + volume_sectors) {
	    uint32_t volume_offset = (current_lba - filesystem_lba) *
				     WINTERBOOT_ATA_SECTOR_SIZE;
	    uint32_t bytes = WINTERBOOT_ATA_SECTOR_SIZE;
	    if (volume_offset < volume_size) {
		if (bytes > volume_size - volume_offset) {
		    bytes = volume_size - volume_offset;
		}
		memcpy(dest, winterboot_boot_volume_start + volume_offset, bytes);
	    }
	}
	dest += WINTERBOOT_ATA_SECTOR_SIZE;
    }
    return 1;
}

static int winterboot_embedded_boot_query(uint32_t drive, uint32_t *sector_count)
{
    if (!sector_count || !winterboot_embedded_boot_drive(drive)) {
	return 0;
    }
    *sector_count = winterboot_embedded_boot_disk_sectors();
    return *sector_count != 0;
}

static uint32_t winterboot_ahci_scratch_physical(unsigned int scratch_index,
						 uint32_t offset)
{
    return WINTERBOOT_AHCI_SCRATCH_PHYSICAL +
	   scratch_index * WINTERBOOT_AHCI_SCRATCH_STRIDE +
	   offset;
}

static void *winterboot_ahci_scratch_virtual(unsigned int scratch_index,
					     uint32_t offset)
{
    return phys_to_virt(winterboot_ahci_scratch_physical(scratch_index, offset));
}

static uint16_t winterboot_poll_key(void)
{
    unsigned char code;
    unsigned char ascii = 0;
    unsigned char scan;

    if ((inb(0x64) & 0x01) == 0) {
	return 0;
    }

    code = inb(0x60);
    if (code == 0xe0) {
	winterboot_key_extended = 1;
	return 0;
    }
    if (code == 0x2a) {
	winterboot_key_lshift = 1;
	return 0;
    }
    if (code == 0x36) {
	winterboot_key_rshift = 1;
	return 0;
    }
    if (code == 0xaa) {
	winterboot_key_lshift = 0;
	return 0;
    }
    if (code == 0xb6) {
	winterboot_key_rshift = 0;
	return 0;
    }
    if (code == 0x3a) {
	winterboot_key_caps = !winterboot_key_caps;
	return 0;
    }
    if ((code & 0x80) != 0) {
	winterboot_key_extended = 0;
	return 0;
    }

    scan = code;
    if (winterboot_key_extended) {
	winterboot_key_extended = 0;
	switch (code) {
	case 0x48: scan = 0x48; break;
	case 0x50: scan = 0x50; break;
	case 0x4b: scan = 0x4b; break;
	case 0x4d: scan = 0x4d; break;
	case 0x53: scan = 0x53; break;
	default: return 0;
	}
	return (uint16_t)scan << 8;
    }

    if (code < sizeof(winterboot_key_normal)) {
	if (winterboot_key_lshift || winterboot_key_rshift) {
	    ascii = winterboot_key_caps ? winterboot_key_normal[code] : winterboot_key_shifted[code];
	} else {
	    ascii = winterboot_key_caps ? winterboot_key_shifted[code] : winterboot_key_normal[code];
	}
    }
    if (ascii == 0) {
	return 0;
    }
    return ((uint16_t)scan << 8) | ascii;
}

static uint32_t winterboot_pci_read32(unsigned int bus, unsigned int slot,
				      unsigned int function, unsigned int offset)
{
    uint32_t address = WINTERBOOT_PCI_ENABLE |
		       ((uint32_t)bus << 16) |
		       ((uint32_t)slot << 11) |
		       ((uint32_t)function << 8) |
		       (offset & 0xfcu);
    outl(address, WINTERBOOT_PCI_CONFIG_ADDRESS);
    return inl(WINTERBOOT_PCI_CONFIG_DATA);
}

static void winterboot_pci_write32(unsigned int bus, unsigned int slot,
				   unsigned int function, unsigned int offset,
				   uint32_t value)
{
    uint32_t address = WINTERBOOT_PCI_ENABLE |
		       ((uint32_t)bus << 16) |
		       ((uint32_t)slot << 11) |
		       ((uint32_t)function << 8) |
		       (offset & 0xfcu);
    outl(address, WINTERBOOT_PCI_CONFIG_ADDRESS);
    outl(value, WINTERBOOT_PCI_CONFIG_DATA);
}

static uint32_t winterboot_ahci_port_read(volatile uint32_t *port,
					  unsigned int offset)
{
    return port[offset / sizeof(uint32_t)];
}

static void winterboot_ahci_port_write(volatile uint32_t *port,
				       unsigned int offset, uint32_t value)
{
    port[offset / sizeof(uint32_t)] = value;
}

static int winterboot_ahci_wait_port_bits(volatile uint32_t *port,
					  unsigned int offset,
					  uint32_t mask,
					  uint32_t value)
{
    unsigned int i;
    for (i = 0; i < WINTERBOOT_AHCI_WAIT_LIMIT; ++i) {
	if ((winterboot_ahci_port_read(port, offset) & mask) == value) {
	    return 1;
	}
    }
    return 0;
}

static int winterboot_ahci_stop_port(volatile uint32_t *port)
{
    uint32_t command = winterboot_ahci_port_read(port, WINTERBOOT_AHCI_PxCMD);

    command &= ~WINTERBOOT_AHCI_CMD_ST;
    winterboot_ahci_port_write(port, WINTERBOOT_AHCI_PxCMD, command);
    if (!winterboot_ahci_wait_port_bits(port, WINTERBOOT_AHCI_PxCMD,
					WINTERBOOT_AHCI_CMD_CR, 0)) {
	return 0;
    }

    command &= ~WINTERBOOT_AHCI_CMD_FRE;
    winterboot_ahci_port_write(port, WINTERBOOT_AHCI_PxCMD, command);
    return winterboot_ahci_wait_port_bits(port, WINTERBOOT_AHCI_PxCMD,
					  WINTERBOOT_AHCI_CMD_FR, 0);
}

static int winterboot_ahci_start_port(volatile uint32_t *port)
{
    uint32_t command = winterboot_ahci_port_read(port, WINTERBOOT_AHCI_PxCMD);

    command |= WINTERBOOT_AHCI_CMD_SUD | WINTERBOOT_AHCI_CMD_POD |
	       WINTERBOOT_AHCI_CMD_FRE;
    winterboot_ahci_port_write(port, WINTERBOOT_AHCI_PxCMD, command);
    command |= WINTERBOOT_AHCI_CMD_ST;
    winterboot_ahci_port_write(port, WINTERBOOT_AHCI_PxCMD, command);
    return 1;
}

static int winterboot_ahci_wait_ready(volatile uint32_t *port)
{
    unsigned int i;
    for (i = 0; i < WINTERBOOT_AHCI_WAIT_LIMIT; ++i) {
	if ((winterboot_ahci_port_read(port, WINTERBOOT_AHCI_PxTFD) &
	     (WINTERBOOT_AHCI_TFD_STS_BSY | WINTERBOOT_AHCI_TFD_STS_DRQ)) == 0) {
	    return 1;
	}
    }
    return 0;
}

static int winterboot_ahci_issue_command(volatile uint32_t *port,
					 unsigned int scratch_index,
					 unsigned char command,
					 uint64_t lba,
					 uint32_t sectors,
					 uint32_t buffer_physical,
					 uint32_t byte_count,
					 int write)
{
    uint32_t *header = winterboot_ahci_scratch_virtual(scratch_index,
						       WINTERBOOT_AHCI_CMD_LIST_OFFSET);
    unsigned char *cmd_table = winterboot_ahci_scratch_virtual(scratch_index,
							       WINTERBOOT_AHCI_CMD_TABLE_OFFSET);
    uint32_t *prdt = (uint32_t *)(cmd_table + 128);
    unsigned char *fis = cmd_table;
    uint32_t command_table_physical = winterboot_ahci_scratch_physical(scratch_index,
								       WINTERBOOT_AHCI_CMD_TABLE_OFFSET);
    uint32_t command_issue;
    unsigned int i;

    if (!winterboot_ahci_wait_ready(port)) {
	return 0;
    }
    for (i = 0; i < WINTERBOOT_AHCI_WAIT_LIMIT; ++i) {
	if ((winterboot_ahci_port_read(port, WINTERBOOT_AHCI_PxCI) & 1u) == 0 &&
	    (winterboot_ahci_port_read(port, WINTERBOOT_AHCI_PxSACT) & 1u) == 0) {
	    break;
	}
    }
    if (i == WINTERBOOT_AHCI_WAIT_LIMIT) {
	return 0;
    }

    memset(header, 0, 1024);
    memset(cmd_table, 0, 256);

    header[0] = 5u | (write ? (1u << 6) : 0u) | (byte_count ? (1u << 16) : 0u);
    header[1] = 0;
    header[2] = command_table_physical;
    header[3] = 0;

    fis[0] = 0x27;
    fis[1] = 0x80;
    fis[2] = command;
    fis[4] = (unsigned char)(lba & 0xffu);
    fis[5] = (unsigned char)((lba >> 8) & 0xffu);
    fis[6] = (unsigned char)((lba >> 16) & 0xffu);
    fis[7] = 0x40;
    fis[8] = (unsigned char)((lba >> 24) & 0xffu);
    fis[9] = (unsigned char)((lba >> 32) & 0xffu);
    fis[10] = (unsigned char)((lba >> 40) & 0xffu);
    fis[12] = (unsigned char)(sectors & 0xffu);
    fis[13] = (unsigned char)((sectors >> 8) & 0xffu);

    if (byte_count) {
	prdt[0] = buffer_physical;
	prdt[1] = 0;
	prdt[2] = 0;
	prdt[3] = (byte_count - 1u) | 0x80000000u;
    }

    winterboot_ahci_port_write(port, WINTERBOOT_AHCI_PxIS, 0xffffffffu);
    __asm__ __volatile__ ("" : : : "memory");
    winterboot_ahci_port_write(port, WINTERBOOT_AHCI_PxCI, 1u);

    for (i = 0; i < WINTERBOOT_AHCI_WAIT_LIMIT; ++i) {
	command_issue = winterboot_ahci_port_read(port, WINTERBOOT_AHCI_PxCI);
	if ((winterboot_ahci_port_read(port, WINTERBOOT_AHCI_PxIS) &
	     WINTERBOOT_AHCI_IS_TFES) != 0) {
	    return 0;
	}
	if ((command_issue & 1u) == 0) {
	    break;
	}
    }
    if (i == WINTERBOOT_AHCI_WAIT_LIMIT) {
	return 0;
    }
    return (winterboot_ahci_port_read(port, WINTERBOOT_AHCI_PxIS) &
	    WINTERBOOT_AHCI_IS_TFES) == 0;
}

static int winterboot_ahci_identify_port(volatile uint32_t *port,
					 unsigned int scratch_index,
					 uint32_t *sector_count)
{
    uint16_t *identify = winterboot_ahci_scratch_virtual(scratch_index,
							 WINTERBOOT_AHCI_IDENTIFY_OFFSET);
    uint64_t lba48_sectors;

    memset(identify, 0, WINTERBOOT_ATA_SECTOR_SIZE);
    if (!sector_count ||
	!winterboot_ahci_issue_command(port, scratch_index,
				       WINTERBOOT_AHCI_CMD_IDENTIFY, 0,
				       1,
				       winterboot_ahci_scratch_physical(scratch_index,
									WINTERBOOT_AHCI_IDENTIFY_OFFSET),
				       WINTERBOOT_ATA_SECTOR_SIZE, 0)) {
	return 0;
    }

    if ((identify[83] & (1u << 10)) != 0) {
	lba48_sectors = ((uint64_t)identify[103] << 48) |
			((uint64_t)identify[102] << 32) |
			((uint64_t)identify[101] << 16) |
			(uint64_t)identify[100];
	if (lba48_sectors > 0xffffffffu) {
	    *sector_count = 0xffffffffu;
	    return 1;
	}
	if (lba48_sectors != 0) {
	    *sector_count = (uint32_t)lba48_sectors;
	    return 1;
	}
    }

    *sector_count = ((uint32_t)identify[61] << 16) |
		    (uint32_t)identify[60];
    return *sector_count != 0;
}

static int winterboot_ahci_add_port(volatile uint32_t *port)
{
    unsigned int scratch_index = winterboot_ahci_drive_count;
    uint32_t sector_count = 0;

    if (winterboot_ahci_drive_count >= WINTERBOOT_AHCI_MAX_DRIVES) {
	return 0;
    }
    if ((winterboot_ahci_port_read(port, WINTERBOOT_AHCI_PxSSTS) & 0x0fu) != 0x03u) {
	return 0;
    }

    if (!winterboot_ahci_stop_port(port)) {
	return 0;
    }
    winterboot_ahci_port_write(port, WINTERBOOT_AHCI_PxCLB,
			       winterboot_ahci_scratch_physical(scratch_index,
								WINTERBOOT_AHCI_CMD_LIST_OFFSET));
    winterboot_ahci_port_write(port, WINTERBOOT_AHCI_PxCLBU, 0);
    winterboot_ahci_port_write(port, WINTERBOOT_AHCI_PxFB,
			       winterboot_ahci_scratch_physical(scratch_index,
								WINTERBOOT_AHCI_FIS_OFFSET));
    winterboot_ahci_port_write(port, WINTERBOOT_AHCI_PxFBU, 0);
    memset(winterboot_ahci_scratch_virtual(scratch_index, WINTERBOOT_AHCI_CMD_LIST_OFFSET),
	   0, 1024);
    memset(winterboot_ahci_scratch_virtual(scratch_index, WINTERBOOT_AHCI_FIS_OFFSET),
	   0, 256);
    winterboot_ahci_port_write(port, WINTERBOOT_AHCI_PxSERR, 0xffffffffu);
    winterboot_ahci_port_write(port, WINTERBOOT_AHCI_PxIS, 0xffffffffu);
    if (!winterboot_ahci_start_port(port)) {
	return 0;
    }
    if (!winterboot_ahci_identify_port(port, scratch_index, &sector_count)) {
	return 0;
    }

    winterboot_ahci_drive_ports[winterboot_ahci_drive_count] = port;
    winterboot_ahci_drive_sectors[winterboot_ahci_drive_count] = sector_count;
    winterboot_ahci_drive_scratch[winterboot_ahci_drive_count] = scratch_index;
    ++winterboot_ahci_drive_count;
    return 1;
}

static int winterboot_ahci_sector_has_wmfs_magic(unsigned int drive_index,
						uint32_t lba)
{
    unsigned int scratch_index;
    unsigned char *sector;

    if (drive_index >= winterboot_ahci_drive_count) {
	return 0;
    }
    scratch_index = winterboot_ahci_drive_scratch[drive_index];
    sector = winterboot_ahci_scratch_virtual(scratch_index,
					     WINTERBOOT_AHCI_IDENTIFY_OFFSET);
    memset(sector, 0, WINTERBOOT_ATA_SECTOR_SIZE);
    if (!winterboot_ahci_issue_command(winterboot_ahci_drive_ports[drive_index],
				       scratch_index,
				       WINTERBOOT_AHCI_CMD_READ_DMA_EXT,
				       lba, 1,
				       winterboot_ahci_scratch_physical(scratch_index,
									WINTERBOOT_AHCI_IDENTIFY_OFFSET),
				       WINTERBOOT_ATA_SECTOR_SIZE, 0)) {
	return 0;
    }
    return (sector[0] == 'W' && sector[1] == 'M' && sector[2] == 'F' &&
	    sector[3] == 'S' &&
	    ((sector[4] == '3' && sector[5] == '2') ||
	     (sector[4] == '6' && sector[5] == '4')));
}

static void winterboot_ahci_swap_drives(unsigned int a, unsigned int b)
{
    volatile uint32_t *port;
    uint32_t sectors;
    unsigned int scratch;

    if (a == b || a >= winterboot_ahci_drive_count || b >= winterboot_ahci_drive_count) {
	return;
    }
    port = winterboot_ahci_drive_ports[a];
    winterboot_ahci_drive_ports[a] = winterboot_ahci_drive_ports[b];
    winterboot_ahci_drive_ports[b] = port;

    sectors = winterboot_ahci_drive_sectors[a];
    winterboot_ahci_drive_sectors[a] = winterboot_ahci_drive_sectors[b];
    winterboot_ahci_drive_sectors[b] = sectors;

    scratch = winterboot_ahci_drive_scratch[a];
    winterboot_ahci_drive_scratch[a] = winterboot_ahci_drive_scratch[b];
    winterboot_ahci_drive_scratch[b] = scratch;
}

static void winterboot_ahci_select_boot_drive(void)
{
    uint32_t boot_drive = winterboot_runtime_read32(WINTERBOOT_RS_BOOT_DRIVE);
    uint32_t boot_sector_count = winterboot_runtime_read32(WINTERBOOT_RS_DISK_SECTOR_COUNT);
    uint32_t filesystem_lba = winterboot_runtime_read32(WINTERBOOT_RS_FILESYSTEM_START_LBA);
    unsigned int target;
    unsigned int index;

    if (boot_drive < 0x80 || winterboot_ahci_drive_count == 0) {
	return;
    }
    target = boot_drive - 0x80;
    if (target >= winterboot_ahci_drive_count) {
	return;
    }

    if (filesystem_lba != 0) {
	for (index = 0; index < winterboot_ahci_drive_count; ++index) {
	    if (winterboot_ahci_sector_has_wmfs_magic(index, filesystem_lba)) {
		winterboot_ahci_swap_drives(target, index);
		return;
	    }
	}
    }

    if (boot_sector_count != 0) {
	for (index = 0; index < winterboot_ahci_drive_count; ++index) {
	    if (winterboot_ahci_drive_sectors[index] == boot_sector_count) {
		winterboot_ahci_swap_drives(target, index);
		return;
	    }
	}
    }
}

static int winterboot_ahci_initialize(void)
{
    unsigned int bus;
    unsigned int slot;
    unsigned int function;

    if (winterboot_ahci_initialized) {
	return winterboot_ahci_drive_count != 0;
    }
    winterboot_ahci_initialized = 1;

    for (bus = 0; bus < 256; ++bus) {
	for (slot = 0; slot < 32; ++slot) {
	    for (function = 0; function < 8; ++function) {
		uint32_t id = winterboot_pci_read32(bus, slot, function, 0);
		uint32_t class_revision;
		uint32_t bar;
		uint32_t command;
		volatile uint32_t *hba;
		uint32_t ports;
		unsigned int port_index;

		if (id == 0xffffffffu || id == 0) {
		    continue;
		}
		class_revision = winterboot_pci_read32(bus, slot, function,
						       WINTERBOOT_PCI_CLASS_REVISION);
		if (((class_revision >> 24) & 0xffu) != WINTERBOOT_PCI_CLASS_MASS_STORAGE ||
		    ((class_revision >> 16) & 0xffu) != WINTERBOOT_PCI_SUBCLASS_SATA ||
		    ((class_revision >> 8) & 0xffu) != WINTERBOOT_PCI_PROGIF_AHCI) {
		    continue;
		}

		bar = winterboot_pci_read32(bus, slot, function, WINTERBOOT_PCI_BAR5);
		if (bar == 0 || bar == 0xffffffffu || (bar & 0x01u) != 0) {
		    continue;
		}
		command = winterboot_pci_read32(bus, slot, function, WINTERBOOT_PCI_COMMAND);
		command |= 0x00000006u;
		winterboot_pci_write32(bus, slot, function, WINTERBOOT_PCI_COMMAND, command);

			hba = phys_to_virt(bar & ~0x0fu);
			hba[WINTERBOOT_AHCI_GHC / sizeof(uint32_t)] |= WINTERBOOT_AHCI_GHC_AE;
			ports = hba[WINTERBOOT_AHCI_PI / sizeof(uint32_t)];
			for (port_index = 0; port_index < 32; ++port_index) {
			    volatile uint32_t *port;
			    if ((ports & (1u << port_index)) == 0) {
				continue;
			    }
			    port = hba + ((WINTERBOOT_AHCI_PORT_BASE +
					   (port_index * WINTERBOOT_AHCI_PORT_SIZE)) /
					  sizeof(uint32_t));
			    (void)winterboot_ahci_add_port(port);
			    if (winterboot_ahci_drive_count >= WINTERBOOT_AHCI_MAX_DRIVES) {
				winterboot_ahci_select_boot_drive();
				return 1;
			    }
			}
		    }
		}
	    }

    winterboot_ahci_select_boot_drive();
    return winterboot_ahci_drive_count != 0;
}

static int winterboot_ahci_decode_drive(uint32_t drive, unsigned int *drive_index)
{
    unsigned int index;

    if (!drive_index || drive < 0x80) {
	return 0;
    }
    if (!winterboot_ahci_initialize()) {
	return 0;
    }
    index = drive - 0x80;
    if (index >= winterboot_ahci_drive_count) {
	return 0;
    }
    *drive_index = index;
    return 1;
}

static int winterboot_ahci_transfer(uint32_t drive, uint32_t lba,
				    uint32_t sectors, uint32_t buffer_physical,
				    int write)
{
    unsigned int drive_index;
    volatile uint32_t *port;

    if (sectors == 0) {
	return 1;
    }
    if (!winterboot_ahci_decode_drive(drive, &drive_index)) {
	return 0;
    }
    port = winterboot_ahci_drive_ports[drive_index];

    while (sectors != 0) {
	uint32_t batch = sectors > 128 ? 128 : sectors;
	uint32_t bytes = batch * WINTERBOOT_ATA_SECTOR_SIZE;
	unsigned char command = write ? WINTERBOOT_AHCI_CMD_WRITE_DMA_EXT :
				       WINTERBOOT_AHCI_CMD_READ_DMA_EXT;

	if (!winterboot_ahci_issue_command(port,
					   winterboot_ahci_drive_scratch[drive_index],
					   command, lba, batch,
					   buffer_physical, bytes, write)) {
	    return 0;
	}
	buffer_physical += bytes;
	lba += batch;
	sectors -= batch;
    }

    if (write) {
	return winterboot_ahci_issue_command(port,
					     winterboot_ahci_drive_scratch[drive_index],
					     WINTERBOOT_AHCI_CMD_FLUSH_CACHE_EXT,
					     0, 0, 0, 0, 0);
    }
    return 1;
}

static int winterboot_ahci_identify(uint32_t drive, uint32_t *sector_count)
{
    unsigned int drive_index;

    if (!sector_count || !winterboot_ahci_decode_drive(drive, &drive_index)) {
	return 0;
    }
    *sector_count = winterboot_ahci_drive_sectors[drive_index];
    return *sector_count != 0;
}

static void winterboot_vbe_write(unsigned int index, uint16_t value)
{
    outw((unsigned short)index, WINTERBOOT_VBE_INDEX_PORT);
    outw(value, WINTERBOOT_VBE_DATA_PORT);
}

static uint16_t winterboot_vbe_read(unsigned int index)
{
    outw((unsigned short)index, WINTERBOOT_VBE_INDEX_PORT);
    return inw(WINTERBOOT_VBE_DATA_PORT);
}

static int winterboot_vbe_dispi_available(void)
{
    uint16_t id = winterboot_vbe_read(WINTERBOOT_VBE_INDEX_ID);
    return id >= WINTERBOOT_VBE_ID0 && id <= WINTERBOOT_VBE_ID5;
}

static int winterboot_pci_probe_bar_size(unsigned int bus, unsigned int slot,
					 unsigned int function, unsigned int bar_offset,
					 uint32_t original_bar, uint32_t *bar_size)
{
    uint32_t size_probe;
    uint32_t high_original = 0;
    uint32_t high_probe = 0;
    uint32_t type = original_bar & 0x06u;

    if (!bar_size || (original_bar & 0x01u) != 0) {
	return 0;
    }
    if (type == 0x04u) {
	if (bar_offset >= WINTERBOOT_PCI_BAR0 + 5u * sizeof(uint32_t)) {
	    return 0;
	}
	high_original = winterboot_pci_read32(bus, slot, function,
					      bar_offset + sizeof(uint32_t));
	winterboot_pci_write32(bus, slot, function,
			       bar_offset + sizeof(uint32_t), 0xffffffffu);
    }

    winterboot_pci_write32(bus, slot, function, bar_offset, 0xffffffffu);
    size_probe = winterboot_pci_read32(bus, slot, function, bar_offset);
    if (type == 0x04u) {
	high_probe = winterboot_pci_read32(bus, slot, function,
					   bar_offset + sizeof(uint32_t));
	winterboot_pci_write32(bus, slot, function,
			       bar_offset + sizeof(uint32_t), high_original);
    }
    winterboot_pci_write32(bus, slot, function, bar_offset, original_bar);

    if (size_probe == 0 || size_probe == 0xffffffffu) {
	return 0;
    }
    if (type == 0x04u && (high_original != 0 || high_probe != 0xffffffffu)) {
	return 0;
    }
    *bar_size = (~(size_probe & ~0x0fu)) + 1u;
    return *bar_size != 0;
}

static int winterboot_vbe_find_framebuffer(uint32_t *framebuffer_physical)
{
    unsigned int bus;
    unsigned int slot;
    unsigned int function;
    uint32_t best_base = 0;
    uint32_t best_size = 0;

    if (!framebuffer_physical) {
	return 0;
    }
    for (bus = 0; bus < 256; ++bus) {
	for (slot = 0; slot < 32; ++slot) {
	    for (function = 0; function < 8; ++function) {
		uint32_t id = winterboot_pci_read32(bus, slot, function, 0);
		uint32_t class_revision;
		unsigned int bar_index;

		if (id == 0xffffffffu || id == 0) {
		    continue;
		}
		class_revision = winterboot_pci_read32(bus, slot, function,
						       WINTERBOOT_PCI_CLASS_REVISION);
		if (((class_revision >> 24) & 0xffu) != WINTERBOOT_PCI_CLASS_DISPLAY) {
		    continue;
		}

		for (bar_index = 0; bar_index < 6; ++bar_index) {
		    unsigned int bar_offset = WINTERBOOT_PCI_BAR0 + bar_index * sizeof(uint32_t);
		    uint32_t original_bar = winterboot_pci_read32(bus, slot, function, bar_offset);
		    uint32_t bar_size = 0;
		    uint32_t bar_base;

		    if (original_bar == 0 || original_bar == 0xffffffffu ||
			(original_bar & 0x01u) != 0) {
			continue;
		    }
		    if (!winterboot_pci_probe_bar_size(bus, slot, function, bar_offset,
						       original_bar, &bar_size)) {
			continue;
		    }
		    bar_base = original_bar & ~0x0fu;
		    if (bar_size >= WINTERBOOT_WXGA_WIDTH * WINTERBOOT_WXGA_HEIGHT *
				    (WINTERBOOT_WXGA_BPP / 8u) &&
			bar_size > best_size) {
			best_base = bar_base;
			best_size = bar_size;
		    }
		}
	    }
	}
    }

    if (best_base == 0) {
	return 0;
    }
    *framebuffer_physical = best_base;
    return 1;
}

static int winterboot_vbe_initialize(void)
{
    if (winterboot_vbe_initialized) {
	return winterboot_vbe_framebuffer_physical != 0;
    }
    winterboot_vbe_initialized = 1;

    if (!winterboot_vbe_dispi_available() ||
	!winterboot_vbe_find_framebuffer(&winterboot_vbe_framebuffer_physical)) {
	winterboot_vbe_framebuffer_physical = 0;
	return 0;
    }
    return 1;
}

static int winterboot_vbe_set_wxga(uint32_t info_physical)
{
    uint32_t *info;

    if (!winterboot_vbe_initialize() || !info_physical) {
	return 0;
    }

    winterboot_vbe_write(WINTERBOOT_VBE_INDEX_ENABLE, 0);
    winterboot_vbe_write(WINTERBOOT_VBE_INDEX_BANK, 0);
    winterboot_vbe_write(WINTERBOOT_VBE_INDEX_X_OFFSET, 0);
    winterboot_vbe_write(WINTERBOOT_VBE_INDEX_Y_OFFSET, 0);
    winterboot_vbe_write(WINTERBOOT_VBE_INDEX_XRES, WINTERBOOT_WXGA_WIDTH);
    winterboot_vbe_write(WINTERBOOT_VBE_INDEX_YRES, WINTERBOOT_WXGA_HEIGHT);
    winterboot_vbe_write(WINTERBOOT_VBE_INDEX_VIRT_WIDTH, WINTERBOOT_WXGA_WIDTH);
    winterboot_vbe_write(WINTERBOOT_VBE_INDEX_VIRT_HEIGHT, WINTERBOOT_WXGA_HEIGHT);
    winterboot_vbe_write(WINTERBOOT_VBE_INDEX_BPP, WINTERBOOT_WXGA_BPP);
    winterboot_vbe_write(WINTERBOOT_VBE_INDEX_ENABLE,
			 WINTERBOOT_VBE_ENABLED | WINTERBOOT_VBE_LFB_ENABLED);

    info = phys_to_virt(info_physical);
    info[0] = winterboot_vbe_framebuffer_physical;
    info[1] = WINTERBOOT_WXGA_WIDTH * (WINTERBOOT_WXGA_BPP / 8u);
    info[2] = WINTERBOOT_WXGA_WIDTH;
    info[3] = WINTERBOOT_WXGA_HEIGHT;
    info[4] = WINTERBOOT_WXGA_BPP;
    info[5] = 8;
    info[6] = 16;
    info[7] = 8;
    info[8] = 8;
    info[9] = 8;
    info[10] = 0;
    return 1;
}

static unsigned short winterboot_ata_base(unsigned int drive_index)
{
    return drive_index < 2 ? WINTERBOOT_ATA_PRIMARY_BASE : WINTERBOOT_ATA_SECONDARY_BASE;
}

static unsigned short winterboot_ata_control(unsigned int drive_index)
{
    return drive_index < 2 ? WINTERBOOT_ATA_PRIMARY_CONTROL : WINTERBOOT_ATA_SECONDARY_CONTROL;
}

static int winterboot_ata_decode_drive(uint32_t drive, unsigned int *drive_index)
{
    if (drive < 0x80 || drive > 0x83 || !drive_index) {
	return 0;
    }
    *drive_index = drive - 0x80;
    return 1;
}

static int winterboot_ata_wait_not_busy(unsigned short base)
{
    unsigned int i;
    for (i = 0; i < WINTERBOOT_ATA_WAIT_LIMIT; ++i) {
	if ((inb(base + 7) & WINTERBOOT_ATA_STATUS_BSY) == 0) {
	    return 1;
	}
    }
    return 0;
}

static int winterboot_ata_wait_drq(unsigned short base)
{
    unsigned int i;
    for (i = 0; i < WINTERBOOT_ATA_WAIT_LIMIT; ++i) {
	unsigned char status = inb(base + 7);
	if ((status & WINTERBOOT_ATA_STATUS_BSY) != 0) {
	    continue;
	}
	if ((status & (WINTERBOOT_ATA_STATUS_ERR | WINTERBOOT_ATA_STATUS_DF)) != 0) {
	    return 0;
	}
	if ((status & WINTERBOOT_ATA_STATUS_DRQ) != 0) {
	    return 1;
	}
    }
    return 0;
}

static int winterboot_ata_select(unsigned int drive_index, uint32_t lba)
{
    unsigned short base = winterboot_ata_base(drive_index);
    unsigned char device = (unsigned char)(0xe0 | ((drive_index & 1) << 4) | ((lba >> 24) & 0x0f));

    if (!winterboot_ata_wait_not_busy(base)) {
	return 0;
    }
    outb(device, base + 6);
    winterboot_io_delay();
    return winterboot_ata_wait_not_busy(base);
}

static int winterboot_ata_identify(uint32_t drive, uint32_t *sector_count)
{
    unsigned int drive_index;
    unsigned short base;
    unsigned short control;
    unsigned char status;

    if (!winterboot_ata_decode_drive(drive, &drive_index) || !sector_count) {
	return 0;
    }

    base = winterboot_ata_base(drive_index);
    control = winterboot_ata_control(drive_index);
    outb(0x02, control);
    if (!winterboot_ata_select(drive_index, 0)) {
	return 0;
    }
    outb(0, base + 2);
    outb(0, base + 3);
    outb(0, base + 4);
    outb(0, base + 5);
    outb(WINTERBOOT_ATA_CMD_IDENTIFY, base + 7);
    winterboot_io_delay();
    status = inb(base + 7);
    if (status == 0) {
	return 0;
    }
    if (!winterboot_ata_wait_drq(base)) {
	return 0;
    }
    insw(base, winterboot_ata_identify_buffer, 256);
    *sector_count = ((uint32_t)winterboot_ata_identify_buffer[61] << 16) |
		    (uint32_t)winterboot_ata_identify_buffer[60];
    return *sector_count != 0;
}

static int winterboot_ata_transfer(uint32_t drive, uint32_t lba,
				   uint32_t sectors, uint32_t buffer_physical,
				   int write)
{
    unsigned int drive_index;
    unsigned short base;
    unsigned char *buffer;

    if (sectors == 0) {
	return 1;
    }
    if (!winterboot_ata_decode_drive(drive, &drive_index)) {
	return 0;
    }
    base = winterboot_ata_base(drive_index);
    buffer = phys_to_virt(buffer_physical);

    while (sectors != 0) {
	uint32_t batch = sectors > 256 ? 256 : sectors;
	unsigned char sector_count = (unsigned char)(batch == 256 ? 0 : batch);
	uint32_t index;

	if (!winterboot_ata_select(drive_index, lba)) {
	    return 0;
	}
	outb(sector_count, base + 2);
	outb((unsigned char)(lba & 0xff), base + 3);
	outb((unsigned char)((lba >> 8) & 0xff), base + 4);
	outb((unsigned char)((lba >> 16) & 0xff), base + 5);
	outb((unsigned char)(0xe0 | ((drive_index & 1) << 4) | ((lba >> 24) & 0x0f)), base + 6);
	outb(write ? WINTERBOOT_ATA_CMD_WRITE_SECTORS : WINTERBOOT_ATA_CMD_READ_SECTORS, base + 7);

	for (index = 0; index < batch; ++index) {
	    if (!winterboot_ata_wait_drq(base)) {
		return 0;
	    }
	    if (write) {
		outsw(base, buffer, WINTERBOOT_ATA_SECTOR_SIZE / 2);
	    } else {
		insw(base, buffer, WINTERBOOT_ATA_SECTOR_SIZE / 2);
	    }
	    buffer += WINTERBOOT_ATA_SECTOR_SIZE;
	    winterboot_io_delay();
	}

	if (write) {
	    outb(WINTERBOOT_ATA_CMD_FLUSH_CACHE, base + 7);
	    if (!winterboot_ata_wait_not_busy(base)) {
		return 0;
	    }
	}

	lba += batch;
	sectors -= batch;
    }

    return 1;
}

static uint32_t winterboot_service_read(uint32_t drive, uint32_t start_lba,
					uint32_t offset, uint32_t size,
					uint32_t buffer_physical)
{
    uint32_t skip = offset & (WINTERBOOT_ATA_SECTOR_SIZE - 1);
    uint32_t sectors;
    unsigned int drive_index;

    if (size == 0) {
	return 1;
    }
    sectors = (skip + size + WINTERBOOT_ATA_SECTOR_SIZE - 1) / WINTERBOOT_ATA_SECTOR_SIZE;
    if (winterboot_embedded_boot_read(drive,
				      start_lba + (offset / WINTERBOOT_ATA_SECTOR_SIZE),
				      sectors,
				      buffer_physical)) {
	return 1;
    }
    if (winterboot_ahci_decode_drive(drive, &drive_index)) {
	return winterboot_ahci_transfer(drive,
					start_lba + (offset / WINTERBOOT_ATA_SECTOR_SIZE),
					sectors,
					buffer_physical,
					0) ? 1 : 0;
    }
    return winterboot_ata_transfer(drive,
				   start_lba + (offset / WINTERBOOT_ATA_SECTOR_SIZE),
				   sectors,
				   buffer_physical,
				   0) ? 1 : 0;
}

static uint32_t winterboot_service_write(uint32_t drive, uint32_t start_lba,
					 uint32_t sectors, uint32_t buffer_physical)
{
    unsigned int drive_index;
    if (winterboot_embedded_boot_drive(drive)) {
	return 0;
    }
    if (winterboot_ahci_decode_drive(drive, &drive_index)) {
	return winterboot_ahci_transfer(drive, start_lba, sectors, buffer_physical, 1) ? 1 : 0;
    }
    return winterboot_ata_transfer(drive, start_lba, sectors, buffer_physical, 1) ? 1 : 0;
}

static void winterboot_text_cursor(int enabled)
{
    outb(0x0a, 0x03d4);
    outb(enabled ? 0x0d : 0x20, 0x03d5);
    outb(0x0b, 0x03d4);
    outb(0x0e, 0x03d5);
}

static void winterboot_wait_microseconds(uint32_t microseconds)
{
    uint32_t loops = microseconds;
    if (loops > 0x100000u) {
	loops = 0x100000u;
    }
    while (loops--) {
	winterboot_io_delay();
	winterboot_io_delay();
	winterboot_io_delay();
	winterboot_io_delay();
    }
}

void winterboot_service_dispatch(struct winterboot_service_regs *regs)
{
    uint32_t sector_count;

    if (!regs) {
	return;
    }

    switch (regs->eax) {
    case WINTERBOOT_SERVICE_POLL_KEY:
	regs->eax = winterboot_poll_key();
	break;
    case WINTERBOOT_SERVICE_GET_ROM_FONT:
	regs->eax = 1;
	regs->ebx = virt_to_phys(fontdata);
	regs->ecx = FONT_WIDTH;
	regs->edx = FONT_HEIGHT;
	break;
    case WINTERBOOT_SERVICE_READ_SECTORS:
	regs->eax = winterboot_service_read(regs->ebx, regs->ecx, regs->edx,
					    regs->esi, regs->edi);
	break;
    case WINTERBOOT_SERVICE_QUERY_DRIVE:
	sector_count = 0;
	if (winterboot_embedded_boot_query(regs->ebx, &sector_count)) {
	    regs->eax = 1;
	} else if (winterboot_ahci_identify(regs->ebx, &sector_count)) {
	    regs->eax = 1;
	} else {
	    regs->eax = winterboot_ata_identify(regs->ebx, &sector_count) ? 1 : 0;
	}
	regs->ebx = sector_count;
	break;
    case WINTERBOOT_SERVICE_WRITE_SECTORS:
	regs->eax = winterboot_service_write(regs->ebx, regs->ecx, regs->edx,
					     regs->esi);
	break;
    case WINTERBOOT_SERVICE_SET_MODE13:
	regs->eax = 0;
	break;
    case WINTERBOOT_SERVICE_WAIT_USEC:
	winterboot_wait_microseconds(regs->ecx);
	regs->eax = 1;
	break;
    case WINTERBOOT_SERVICE_SET_TEXT_MODE3:
	winterboot_text_cursor(1);
	regs->eax = 1;
	break;
    case WINTERBOOT_SERVICE_SET_WXGA_MODE:
	if (regs->edi) {
	    uint32_t *info = phys_to_virt(regs->edi);
	    unsigned int i;
	    for (i = 0; i < 11; ++i) {
		info[i] = 0;
	    }
	}
	regs->eax = winterboot_vbe_set_wxga(regs->edi) ? 1 : 0;
	break;
    default:
	regs->eax = 0;
	break;
    }
}

static void start_main(void); /* forward decl. */
void __exit_context(void); /* assembly routine */

/*
 * Main context structure
 * It is placed at the bottom of our stack, and loaded by assembly routine
 * to start us up.
 */
static struct context main_ctx __attribute__((section (".initctx"))) = {
    .gdt_base = (uint32_t) gdt,
    .gdt_limit = GDT_LIMIT,
    .cs = FLAT_CS,
    .ds = FLAT_DS,
    .es = FLAT_DS,
    .fs = FLAT_DS,
    .gs = FLAT_DS,
    .ss = FLAT_DS,
    .esp = (uint32_t) ESP_LOC(&main_ctx),
    .eip = (uint32_t) start_main,
    .return_addr = (uint32_t) __exit_context,
};

/* This is used by assembly routine to load/store the context which
 * it is to switch/switched.  */
struct context *__context = &main_ctx;

/* Stack for loaded ELF image */
static uint8_t image_stack[IMAGE_STACK_SIZE];

/* Pointer to startup context (physical address) */
unsigned long __boot_ctx;

/*
 * Main starter
 * This is the C function that runs first.
 */
static void start_main(void)
{
    int retval;

    /* Save startup context, so we can refer to it later.
     * We have to keep it in physical address since we will relocate. */
    __boot_ctx = virt_to_phys(__context);

    init_exceptions();
    /* Start the real fun */
    retval = openbios();

    /* Pass return value to startup context. Bootloader may see it. */
    boot_ctx->eax = retval;

    /* Returning from here should jump to __exit_context */
    __context = boot_ctx;
}

/* Setup a new context using the given stack.
 */
struct context *
init_context(uint8_t *stack, uint32_t stack_size, int num_params)
{
    struct context *ctx;

    ctx = (struct context *)
	(stack + stack_size - (sizeof(*ctx) + num_params*sizeof(uint32_t)));
    memset(ctx, 0, sizeof(*ctx));

    /* Fill in reasonable default for flat memory model */
    ctx->gdt_base = virt_to_phys(gdt);
    ctx->gdt_limit = GDT_LIMIT;
    ctx->cs = FLAT_CS;
    ctx->ds = FLAT_DS;
    ctx->es = FLAT_DS;
    ctx->fs = FLAT_DS;
    ctx->gs = FLAT_DS;
    ctx->ss = FLAT_DS;
    ctx->esp = virt_to_phys(ESP_LOC(ctx));
    ctx->return_addr = virt_to_phys(__exit_context);

    return ctx;
}

/* Switch to another context. */
struct context *switch_to(struct context *ctx)
{
    struct context *save, *ret;

    save = __context;
    __context = ctx;
    asm ("pushl %cs; call __switch_context");
    ret = __context;
    __context = save;
    return ret;
}

/* Start ELF Boot image */
unsigned int start_elf(unsigned long entry_point, unsigned long param)
{
    struct context *ctx;

    ctx = init_context(image_stack, sizeof image_stack, 1);
    ctx->eip = entry_point;
    ctx->param[0] = param;
    ctx->eax = 0xe1fb007;
    ctx->ebx = param;

    ctx = switch_to(ctx);
    return ctx->eax;
}

/* Start a flat 32-bit image at a raw physical entry point. */
unsigned int start_raw(unsigned long entry_point, unsigned long stack,
		       unsigned long stack_size)
{
    struct context *ctx;

    ctx = init_context(phys_to_virt(stack), stack_size, 0);
    ctx->eip = entry_point;

    init_winterboot_service_idt();
    ctx = switch_to(ctx);
    return ctx->eax;
}
