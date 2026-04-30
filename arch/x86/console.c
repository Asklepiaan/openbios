/*
 * Copyright (C) 2003, 2004 Stefan Reinauer
 *
 * See the file "COPYING" for further information about
 * the copyright and warranty status of this work.
 */

#include "config.h"
#include "kernel/kernel.h"
#include "openbios.h"

#if defined(CONFIG_DRIVER_PCI)
#include "drivers/pci.h"
#include "arch/x86/pci.h"
#endif

#ifdef CONFIG_DEBUG_CONSOLE

/* ******************************************************************
 *                       serial console functions
 * ****************************************************************** */

#ifdef CONFIG_DEBUG_CONSOLE_SERIAL

#define RBR(x)  x==2?0x2f8:0x3f8
#define THR(x)  x==2?0x2f8:0x3f8
#define IER(x)  x==2?0x2f9:0x3f9
#define IIR(x)  x==2?0x2fa:0x3fa
#define LCR(x)  x==2?0x2fb:0x3fb
#define MCR(x)  x==2?0x2fc:0x3fc
#define LSR(x)  x==2?0x2fd:0x3fd
#define MSR(x)  x==2?0x2fe:0x3fe
#define SCR(x)  x==2?0x2ff:0x3ff
#define DLL(x)  x==2?0x2f8:0x3f8
#define DLM(x)  x==2?0x2f9:0x3f9

static int uart_charav(int port)
{
	if (!port)
		return -1;
	return ((inb(LSR(port)) & 1) != 0);
}

static char uart_getchar(int port)
{
	if (!port)
		return -1;
	while (!uart_charav(port));
	return ((char) inb(RBR(port)) & 0177);
}

static void uart_putchar(int port, unsigned char c)
{
	if (!port)
		return;
	if (c == '\n')
		uart_putchar(port, '\r');
	while (!(inb(LSR(port)) & 0x20));
	outb(c, THR(port));
}

static void uart_init_line(int port, unsigned long baud)
{
	int i, baudconst;

	if (!port)
		return;

	switch (baud) {
	case 115200:
		baudconst = 1;
		break;
	case 57600:
		baudconst = 2;
		break;
	case 38400:
		baudconst = 3;
		break;
	case 19200:
		baudconst = 6;
		break;
	case 9600:
	default:
		baudconst = 12;
		break;
	}

	outb(0x87, LCR(port));
	outb(0x00, DLM(port));
	outb(baudconst, DLL(port));
	outb(0x07, LCR(port));
	outb(0x0f, MCR(port));

	for (i = 10; i > 0; i--) {
		if (inb(LSR(port)) == (unsigned int) 0)
			break;
		inb(RBR(port));
	}
}

int uart_init(int port, unsigned long speed)
{
	if (port)
		uart_init_line(port, speed);
	return -1;
}

static void serial_putchar(int c)
{
	uart_putchar(CONFIG_SERIAL_PORT, (unsigned char) (c & 0xff));
}

static void serial_cls(void)
{
	serial_putchar(27);
	serial_putchar('[');
	serial_putchar('H');
	serial_putchar(27);
	serial_putchar('[');
	serial_putchar('J');
}

#endif

/* ******************************************************************
 *          simple polling video/keyboard console functions
 * ****************************************************************** */

#ifdef CONFIG_DEBUG_CONSOLE_VGA

/* raw vga text mode */
#define COLUMNS			80	/* The number of columns.  */
#define LINES			25	/* The number of lines.  */
#define ATTRIBUTE		7	/* The attribute of an character.  */

#define VGA_BASE		0xB8000	/* The video memory address.  */

/* VGA Index and Data Registers */
#define VGA_REG_INDEX    0x03D4	/* VGA index register */
#define VGA_REG_DATA     0x03D5	/* VGA data register */

#define VGA_IDX_CURMSL   0x09	/* cursor maximum scan line */
#define VGA_IDX_CURSTART 0x0A	/* cursor start */
#define VGA_IDX_CUREND   0x0B	/* cursor end */
#define VGA_IDX_CURLO    0x0F	/* cursor position (low 8 bits) */
#define VGA_IDX_CURHI    0x0E	/* cursor position (high 8 bits) */

/* Save the X and Y position.  */
static int xpos, ypos;
/* Point to the video memory.  */
static volatile unsigned char *video = (unsigned char *) VGA_BASE;

static void video_initcursor(void)
{
	u8 val;
	outb(VGA_IDX_CURMSL, VGA_REG_INDEX);
	val = inb(VGA_REG_DATA) & 0x1f;	/* maximum scan line -1 */

	outb(VGA_IDX_CURSTART, VGA_REG_INDEX);
	outb(0, VGA_REG_DATA);

	outb(VGA_IDX_CUREND, VGA_REG_INDEX);
	outb(val, VGA_REG_DATA);
}



static void video_poscursor(unsigned int x, unsigned int y)
{
	unsigned short pos;

	/* Calculate new cursor position as a function of x and y */
	pos = (y * COLUMNS) + x;

	/* Output the new position to VGA card */
	outb(VGA_IDX_CURLO, VGA_REG_INDEX);	/* output low 8 bits */
	outb((u8) (pos), VGA_REG_DATA);
	outb(VGA_IDX_CURHI, VGA_REG_INDEX);	/* output high 8 bits */
	outb((u8) (pos >> 8), VGA_REG_DATA);

};


static void video_newline(void)
{
	xpos = 0;

	if (ypos < LINES - 1) {
		ypos++;
	} else {
		int i;
		memmove((void *) video, (void *) (video + 2 * COLUMNS),
			(LINES - 1) * COLUMNS * 2);

		for (i = ((LINES - 1) * 2 * COLUMNS);
		     i < 2 * COLUMNS * LINES;) {
			video[i++] = 0;
			video[i++] = ATTRIBUTE;
		}
	}

}

/* Put the character C on the screen.  */
static void video_putchar(int c)
{
	int p=1;

	if (c == '\n' || c == '\r') {
		video_newline();
		return;
	}

	if (c == '\b') {
		if (xpos) xpos--;
		c=' ';
		p=0;
	}


	if (xpos >= COLUMNS)
		video_newline();

	*(video + (xpos + ypos * COLUMNS) * 2) = c & 0xFF;
	*(video + (xpos + ypos * COLUMNS) * 2 + 1) = ATTRIBUTE;

	if (p)
		xpos++;

	video_poscursor(xpos, ypos);
}

static void video_cls(void)
{
	int i;

	for (i = 0; i < 2 * COLUMNS * LINES;) {
		video[i++] = 0;
		video[i++] = ATTRIBUTE;
	}


	xpos = 0;
	ypos = 0;

	video_initcursor();
	video_poscursor(xpos, ypos);
}

void video_init(void)
{
	video=phys_to_virt((unsigned char*)VGA_BASE);
}

/*
 *  keyboard driver
 */

static const char normal[] = {
	0x0, 0x1b, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',
	'=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o',
	'p', '[', ']', 0xa, 0x0, 'a', 's', 'd', 'f', 'g', 'h', 'j',
	'k', 'l', ';', 0x27, 0x60, 0x0, 0x5c, 'z', 'x', 'c', 'v', 'b',
	'n', 'm', ',', '.', '/', 0x0, '*', 0x0, ' ', 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, '0', 0x7f
};

static const char shifted[] = {
	0x0, 0x1b, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',
	'+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O',
	'P', '{', '}', 0xa, 0x0, 'A', 'S', 'D', 'F', 'G', 'H', 'J',
	'K', 'L', ':', 0x22, '~', 0x0, '|', 'Z', 'X', 'C', 'V', 'B',
	'N', 'M', '<', '>', '?', 0x0, '*', 0x0, ' ', 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, '7', '8',
	'9', 0x0, '4', '5', '6', 0x0, '1', '2', '3', '0', 0x7f
};

static int key_ext;
static int key_lshift = 0, key_rshift = 0, key_caps = 0;

static char last_key;

static void keyboard_cmd(unsigned char cmd, unsigned char val)
{
	outb(cmd, 0x60);
	/* wait until keyboard controller accepts cmds: */
	while (inb(0x64) & 2);
	outb(val, 0x60);
	while (inb(0x64) & 2);
}

static char keyboard_poll(void)
{
	unsigned int c;
	if (inb(0x64) & 1) {
		c = inb(0x60);
		switch (c) {
		case 0xe0:
			key_ext = 1;
			return 0;
		case 0x2a:
			key_lshift = 1;
			return 0;
		case 0x36:
			key_rshift = 1;
			return 0;
		case 0xaa:
			key_lshift = 0;
			return 0;
		case 0xb6:
			key_rshift = 0;
			return 0;
		case 0x3a:
			if (key_caps) {
				key_caps = 0;
				keyboard_cmd(0xed, 0);
			} else {
				key_caps = 1;
				keyboard_cmd(0xed, 4);	/* set caps led */
			}
			return 0;
		}

		if (key_ext) {
			// void printk(const char *format, ...);
			printk("extended keycode: %x\n", c);

			key_ext = 0;
			return 0;
		}

		if (c & 0x80)	/* unhandled key release */
			return 0;

		if (key_lshift || key_rshift)
			return key_caps ? normal[c] : shifted[c];
		else
			return key_caps ? shifted[c] : normal[c];
	}
	return 0;
}

static int keyboard_dataready(void)
{
	if (last_key)
		return 1;

	last_key = keyboard_poll();

	return (last_key != 0);
}

static unsigned char keyboard_readdata(void)
{
	char tmp;
	while (!keyboard_dataready());
	tmp = last_key;
	last_key = 0;
	return tmp;
}
#endif

#if defined(CONFIG_DRIVER_PCI)

#define UHCI_FRAME_COUNT 1024u
#define UHCI_TD_COUNT 16u
#define UHCI_DATA_BYTES 256u
#define UHCI_REPORT_BYTES 8u

#define UHCI_USBCMD 0x00
#define UHCI_USBSTS 0x02
#define UHCI_USBINTR 0x04
#define UHCI_FRNUM 0x06
#define UHCI_FLBASEADD 0x08
#define UHCI_SOFMOD 0x0c
#define UHCI_PORTSC1 0x10
#define UHCI_PORTSC2 0x12

#define UHCI_CMD_RUN 0x0001u
#define UHCI_CMD_HCRESET 0x0002u
#define UHCI_CMD_CONFIGURE 0x0040u
#define UHCI_CMD_MAX_PACKET 0x0080u

#define UHCI_PORT_CONNECTED 0x0001u
#define UHCI_PORT_CONNECTION_CHANGE 0x0002u
#define UHCI_PORT_ENABLED 0x0004u
#define UHCI_PORT_ENABLE_CHANGE 0x0008u
#define UHCI_PORT_LOW_SPEED 0x0100u
#define UHCI_PORT_RESET 0x0200u
#define UHCI_PORT_CHANGE_MASK (UHCI_PORT_CONNECTION_CHANGE | UHCI_PORT_ENABLE_CHANGE)

#define UHCI_LINK_TERMINATE 0x00000001u
#define UHCI_LINK_QH 0x00000002u

#define UHCI_TD_STATUS_ACTIVE 0x00800000u
#define UHCI_TD_STATUS_STALLED 0x00400000u
#define UHCI_TD_STATUS_DBUFERR 0x00200000u
#define UHCI_TD_STATUS_BABBLE 0x00100000u
#define UHCI_TD_STATUS_CRC_TIMEOUT 0x00040000u
#define UHCI_TD_STATUS_BITSTUFF 0x00020000u
#define UHCI_TD_STATUS_ERROR_MASK \
	(UHCI_TD_STATUS_STALLED | UHCI_TD_STATUS_DBUFERR | UHCI_TD_STATUS_BABBLE | \
	 UHCI_TD_STATUS_CRC_TIMEOUT | UHCI_TD_STATUS_BITSTUFF)
#define UHCI_TD_CONTROL_ERROR_COUNT (3u << 27)
#define UHCI_TD_CONTROL_LOW_SPEED (1u << 26)

#define UHCI_PID_SETUP 0x2du
#define UHCI_PID_IN 0x69u
#define UHCI_PID_OUT 0xe1u

#define USB_REQ_GET_DESCRIPTOR 6u
#define USB_REQ_SET_ADDRESS 5u
#define USB_REQ_SET_CONFIGURATION 9u
#define USB_REQ_SET_IDLE 10u
#define USB_REQ_SET_PROTOCOL 11u

#define USB_DESC_DEVICE 1u
#define USB_DESC_CONFIGURATION 2u
#define USB_DESC_INTERFACE 4u
#define USB_DESC_ENDPOINT 5u

#define XHCI_RING_TRBS 64u
#define XHCI_RING_LINK_INDEX (XHCI_RING_TRBS - 1u)
#define XHCI_MAX_SLOTS 8u
#define XHCI_CONTEXT_BYTES (33u * 64u)
#define XHCI_DATA_BYTES 256u

#define XHCI_CAP_HCSPARAMS1 0x04u
#define XHCI_CAP_HCSPARAMS2 0x08u
#define XHCI_CAP_HCCPARAMS1 0x10u
#define XHCI_CAP_DBOFF 0x14u
#define XHCI_CAP_RTSOFF 0x18u

#define XHCI_OP_USBCMD 0x00u
#define XHCI_OP_USBSTS 0x04u
#define XHCI_OP_PAGESIZE 0x08u
#define XHCI_OP_CRCR 0x18u
#define XHCI_OP_DCBAAP 0x30u
#define XHCI_OP_CONFIG 0x38u
#define XHCI_OP_PORT_BASE 0x400u
#define XHCI_PORT_STRIDE 0x10u

#define XHCI_CMD_RUN 0x00000001u
#define XHCI_CMD_HCRST 0x00000002u
#define XHCI_CMD_INTE 0x00000004u
#define XHCI_STS_HCH 0x00000001u
#define XHCI_STS_CNR 0x00000800u

#define XHCI_PORT_CCS 0x00000001u
#define XHCI_PORT_PED 0x00000002u
#define XHCI_PORT_PR 0x00000010u
#define XHCI_PORT_PP 0x00000200u
#define XHCI_PORT_SPEED_SHIFT 10u
#define XHCI_PORT_CHANGE_BITS 0x00fe0000u

#define XHCI_INTR_IMAN 0x20u
#define XHCI_INTR_ERSTSZ 0x28u
#define XHCI_INTR_ERSTBA 0x30u
#define XHCI_INTR_ERDP 0x38u

#define XHCI_TRB_TYPE_NORMAL 1u
#define XHCI_TRB_TYPE_SETUP_STAGE 2u
#define XHCI_TRB_TYPE_DATA_STAGE 3u
#define XHCI_TRB_TYPE_STATUS_STAGE 4u
#define XHCI_TRB_TYPE_LINK 6u
#define XHCI_TRB_TYPE_ENABLE_SLOT 9u
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11u
#define XHCI_TRB_TYPE_CONFIGURE_ENDPOINT 12u
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32u
#define XHCI_TRB_TYPE_COMMAND_COMPLETION 33u

#define XHCI_TRB_CYCLE 0x00000001u
#define XHCI_TRB_LINK_TC 0x00000002u
#define XHCI_TRB_ISP 0x00000004u
#define XHCI_TRB_IOC 0x00000020u
#define XHCI_TRB_IDT 0x00000040u
#define XHCI_TRB_DIR 0x00010000u
#define XHCI_TRB_TYPE(type) ((type) << 10)

#define XHCI_CC_SUCCESS 1u
#define XHCI_CC_SHORT_PACKET 13u

#define EHCI_FRAME_COUNT 1024u
#define EHCI_QTD_COUNT 8u
#define EHCI_DATA_BYTES 256u

#define EHCI_CAP_HCSPARAMS 0x04u

#define EHCI_OP_USBCMD 0x00u
#define EHCI_OP_USBSTS 0x04u
#define EHCI_OP_USBINTR 0x08u
#define EHCI_OP_CTRLDSSEGMENT 0x10u
#define EHCI_OP_PERIODICLISTBASE 0x14u
#define EHCI_OP_ASYNCLISTADDR 0x18u
#define EHCI_OP_CONFIGFLAG 0x40u
#define EHCI_OP_PORTSC_BASE 0x44u

#define EHCI_CMD_RUN 0x00000001u
#define EHCI_CMD_RESET 0x00000002u
#define EHCI_CMD_PERIODIC_ENABLE 0x00000010u
#define EHCI_CMD_ASYNC_ENABLE 0x00000020u
#define EHCI_STS_HALTED 0x00001000u

#define EHCI_PORT_CONNECTED 0x00000001u
#define EHCI_PORT_ENABLED 0x00000004u
#define EHCI_PORT_RESET 0x00000100u
#define EHCI_PORT_POWER 0x00001000u
#define EHCI_PORT_OWNER 0x00002000u
#define EHCI_PORT_CHANGE_BITS 0x0000002au

#define EHCI_LINK_TERMINATE 0x00000001u
#define EHCI_LINK_QH 0x00000002u

#define EHCI_QTD_STATUS_ACTIVE 0x00000080u
#define EHCI_QTD_STATUS_ERROR_MASK 0x0000007cu
#define EHCI_QTD_PID_OUT 0u
#define EHCI_QTD_PID_IN 1u
#define EHCI_QTD_PID_SETUP 2u
#define EHCI_QTD_CERR (3u << 10)
#define EHCI_QTD_IOC 0x00008000u
#define EHCI_QTD_BYTES(length) ((uint32_t)(length) << 16)
#define EHCI_QTD_TOGGLE(toggle) ((uint32_t)(toggle) << 31)

#define EHCI_QH_HEAD 0x00008000u
#define EHCI_QH_DTC 0x00004000u
#define EHCI_QH_SPEED_HIGH (2u << 12)
#define EHCI_QH_C_MASK_NONE 0x00000000u
#define EHCI_QH_S_MASK_EVERY_FRAME 0x00000001u
#define EHCI_QH_MULT_ONE (1u << 30)

typedef struct xhci_trb {
	uint32_t parameter_low;
	uint32_t parameter_high;
	uint32_t status;
	uint32_t control;
} xhci_trb_t;

typedef struct xhci_erst_entry {
	uint32_t base_low;
	uint32_t base_high;
	uint32_t size;
	uint32_t reserved;
} xhci_erst_entry_t;

typedef struct ehci_qtd {
	volatile uint32_t next;
	volatile uint32_t alternate_next;
	volatile uint32_t token;
	volatile uint32_t buffer[5];
	volatile uint32_t buffer_high[5];
	volatile uint32_t reserved[3];
} ehci_qtd_t;

typedef struct ehci_qh {
	volatile uint32_t horizontal;
	volatile uint32_t endpoint_characteristics;
	volatile uint32_t endpoint_capabilities;
	volatile uint32_t current_qtd;
	volatile uint32_t next_qtd;
	volatile uint32_t alternate_next_qtd;
	volatile uint32_t token;
	volatile uint32_t buffer[5];
	volatile uint32_t buffer_high[5];
} ehci_qh_t;

typedef struct uhci_td {
	volatile uint32_t link;
	volatile uint32_t status;
	volatile uint32_t token;
	volatile uint32_t buffer;
} uhci_td_t;

typedef struct uhci_qh {
	volatile uint32_t head;
	volatile uint32_t element;
} uhci_qh_t;

static uint32_t uhci_frame_list[UHCI_FRAME_COUNT] __attribute__((aligned(4096)));
static uhci_qh_t uhci_qh __attribute__((aligned(16)));
static uhci_td_t uhci_tds[UHCI_TD_COUNT] __attribute__((aligned(16)));
static uint8_t uhci_setup_packet[8] __attribute__((aligned(16)));
static uint8_t uhci_data[UHCI_DATA_BYTES] __attribute__((aligned(16)));
static uint8_t uhci_report[UHCI_REPORT_BYTES] __attribute__((aligned(16)));
static uint32_t ehci_periodic_list[EHCI_FRAME_COUNT] __attribute__((aligned(4096)));
static ehci_qh_t ehci_async_qh __attribute__((aligned(32)));
static ehci_qh_t ehci_keyboard_qh __attribute__((aligned(32)));
static ehci_qtd_t ehci_qtds[EHCI_QTD_COUNT] __attribute__((aligned(32)));
static ehci_qtd_t ehci_interrupt_qtd __attribute__((aligned(32)));
static uint8_t ehci_setup_packet[8] __attribute__((aligned(32)));
static uint8_t ehci_data[EHCI_DATA_BYTES] __attribute__((aligned(32)));
static uint8_t ehci_report[UHCI_REPORT_BYTES] __attribute__((aligned(32)));
static uint64_t xhci_dcbaa[XHCI_MAX_SLOTS + 1u] __attribute__((aligned(4096)));
static uint8_t xhci_input_context[XHCI_CONTEXT_BYTES] __attribute__((aligned(4096)));
static uint8_t xhci_device_context[XHCI_CONTEXT_BYTES] __attribute__((aligned(4096)));
static xhci_trb_t xhci_command_ring[XHCI_RING_TRBS] __attribute__((aligned(64)));
static xhci_trb_t xhci_control_ring[XHCI_RING_TRBS] __attribute__((aligned(64)));
static xhci_trb_t xhci_interrupt_ring[XHCI_RING_TRBS] __attribute__((aligned(64)));
static xhci_trb_t xhci_event_ring[XHCI_RING_TRBS] __attribute__((aligned(64)));
static xhci_erst_entry_t xhci_erst[1] __attribute__((aligned(64)));
static uint8_t xhci_data[XHCI_DATA_BYTES] __attribute__((aligned(64)));
static uint8_t xhci_report[UHCI_REPORT_BYTES] __attribute__((aligned(64)));

static int usb_probe_done;
static int usb_keyboard_ready;
static uint16_t usb_uhci_base;
static uint8_t usb_keyboard_address;
static uint8_t usb_keyboard_endpoint;
static uint8_t usb_keyboard_max_packet;
static uint8_t usb_keyboard_low_speed;
static uint8_t usb_keyboard_toggle;
static uint8_t usb_keyboard_last_report[UHCI_REPORT_BYTES];
static uint8_t usb_keyboard_last_key;
static uint8_t usb_keyboard_caps;
static int ehci_probe_done;
static int ehci_keyboard_ready;
static uint32_t ehci_mmio_base;
static uint32_t ehci_cap_length;
static uint32_t ehci_max_ports;
static uint8_t ehci_keyboard_address;
static uint8_t ehci_keyboard_endpoint;
static uint8_t ehci_keyboard_packet;
static uint8_t ehci_keyboard_toggle;
static uint8_t ehci_keyboard_last_report[UHCI_REPORT_BYTES];
static uint8_t ehci_interrupt_pending;
static int xhci_probe_done;
static int xhci_keyboard_ready;
static uint32_t xhci_mmio_base;
static uint32_t xhci_cap_length;
static uint32_t xhci_doorbell_offset;
static uint32_t xhci_runtime_offset;
static uint32_t xhci_max_ports;
static uint32_t xhci_context_size;
static uint32_t xhci_command_enqueue;
static uint32_t xhci_control_enqueue;
static uint32_t xhci_interrupt_enqueue;
static uint32_t xhci_event_dequeue;
static uint8_t xhci_command_cycle;
static uint8_t xhci_control_cycle;
static uint8_t xhci_interrupt_cycle;
static uint8_t xhci_event_cycle;
static uint8_t xhci_keyboard_slot;
static uint8_t xhci_keyboard_dci;
static uint8_t xhci_keyboard_packet;
static uint8_t xhci_keyboard_last_report[UHCI_REPORT_BYTES];
static uint8_t xhci_interrupt_pending;
static uint32_t xhci_interrupt_trb;

static uint32_t uhci_phys(const void *ptr)
{
	return (uint32_t)virt_to_phys(ptr);
}

static void uhci_delay_ms(unsigned int milliseconds)
{
	unsigned int millisecond;
	unsigned int iteration;

	for (millisecond = 0; millisecond < milliseconds; millisecond++) {
		for (iteration = 0; iteration < 1000u; iteration++)
			slow_down_io();
	}
}

static uint32_t uhci_td_token(uint8_t pid, uint8_t address, uint8_t endpoint,
			      uint8_t toggle, uint32_t length)
{
	uint32_t max_length = 0x7ffu;

	if (length != 0u)
		max_length = length - 1u;

	return (max_length << 21) |
	       (((uint32_t)(toggle != 0u)) << 19) |
	       (((uint32_t)endpoint & 0x0fu) << 15) |
	       (((uint32_t)address & 0x7fu) << 8) |
	       pid;
}

static void uhci_prepare_td(uint32_t index, uint8_t pid, uint8_t address,
			    uint8_t endpoint, uint8_t toggle, uint32_t length,
			    void *buffer, int low_speed)
{
	uhci_td_t *td = &uhci_tds[index];

	td->link = UHCI_LINK_TERMINATE;
	td->status = UHCI_TD_STATUS_ACTIVE | UHCI_TD_CONTROL_ERROR_COUNT |
		     (low_speed ? UHCI_TD_CONTROL_LOW_SPEED : 0u);
	td->token = uhci_td_token(pid, address, endpoint, toggle, length);
	td->buffer = (length != 0u && buffer) ? uhci_phys(buffer) : 0u;
}

static int uhci_run_tds(uint32_t count, uint32_t timeout_ms, int allow_timeout)
{
	uint32_t index;
	uint32_t elapsed;
	int complete;

	if (count == 0u || count > UHCI_TD_COUNT || usb_uhci_base == 0u)
		return 0;

	for (index = 0u; index + 1u < count; index++)
		uhci_tds[index].link = uhci_phys(&uhci_tds[index + 1u]);
	uhci_tds[count - 1u].link = UHCI_LINK_TERMINATE;

	outw(0xffffu, usb_uhci_base + UHCI_USBSTS);
	uhci_qh.element = uhci_phys(&uhci_tds[0]);

	for (elapsed = 0u; elapsed < timeout_ms; elapsed++) {
		complete = 1;
		for (index = 0u; index < count; index++) {
			if ((uhci_tds[index].status & UHCI_TD_STATUS_ACTIVE) != 0u) {
				complete = 0;
				break;
			}
		}
		if (complete)
			break;
		uhci_delay_ms(1u);
	}

	uhci_qh.element = UHCI_LINK_TERMINATE;

	complete = 1;
	for (index = 0u; index < count; index++) {
		if ((uhci_tds[index].status & UHCI_TD_STATUS_ACTIVE) != 0u) {
			complete = 0;
			uhci_tds[index].status &= ~UHCI_TD_STATUS_ACTIVE;
		}
	}

	if (!complete)
		return allow_timeout ? -1 : 0;

	for (index = 0u; index < count; index++) {
		if ((uhci_tds[index].status & UHCI_TD_STATUS_ERROR_MASK) != 0u)
			return 0;
	}

	return 1;
}

static int uhci_control_transfer(uint8_t address, uint8_t max_packet,
				 int low_speed, uint8_t request_type,
				 uint8_t request, uint16_t value,
				 uint16_t index_value, void *data,
				 uint32_t length, int data_in)
{
	uint32_t td_count = 0u;
	uint32_t offset = 0u;
	uint8_t toggle = 1u;
	uint8_t *bytes = (uint8_t *)data;

	if (max_packet == 0u)
		max_packet = 8u;
	if (max_packet > 64u)
		max_packet = 64u;
	if (length != 0u && !data)
		return 0;

	uhci_setup_packet[0] = request_type;
	uhci_setup_packet[1] = request;
	uhci_setup_packet[2] = (uint8_t)(value & 0xffu);
	uhci_setup_packet[3] = (uint8_t)(value >> 8);
	uhci_setup_packet[4] = (uint8_t)(index_value & 0xffu);
	uhci_setup_packet[5] = (uint8_t)(index_value >> 8);
	uhci_setup_packet[6] = (uint8_t)(length & 0xffu);
	uhci_setup_packet[7] = (uint8_t)(length >> 8);

	uhci_prepare_td(td_count++, UHCI_PID_SETUP, address, 0u, 0u,
			sizeof(uhci_setup_packet), uhci_setup_packet, low_speed);

	while (offset < length) {
		uint32_t chunk = length - offset;

		if (chunk > max_packet)
			chunk = max_packet;
		if (td_count + 1u >= UHCI_TD_COUNT)
			return 0;
		uhci_prepare_td(td_count++, data_in ? UHCI_PID_IN : UHCI_PID_OUT,
				address, 0u, toggle, chunk, bytes + offset,
				low_speed);
		toggle ^= 1u;
		offset += chunk;
	}

	if (td_count >= UHCI_TD_COUNT)
		return 0;
	uhci_prepare_td(td_count++, data_in ? UHCI_PID_OUT : UHCI_PID_IN,
			address, 0u, 1u, 0u, NULL, low_speed);

	return uhci_run_tds(td_count, 250u, 0) == 1;
}

static int uhci_get_descriptor(uint8_t address, uint8_t max_packet,
			       int low_speed, uint8_t descriptor_type,
			       uint8_t descriptor_index, void *data,
			       uint32_t length)
{
	return uhci_control_transfer(address, max_packet, low_speed, 0x80u,
				     USB_REQ_GET_DESCRIPTOR,
				     ((uint16_t)descriptor_type << 8) |
				     descriptor_index, 0u, data, length, 1);
}

static int uhci_set_address(uint8_t address, int low_speed)
{
	return uhci_control_transfer(0u, 8u, low_speed, 0x00u,
				     USB_REQ_SET_ADDRESS, address, 0u,
				     NULL, 0u, 0);
}

static int uhci_set_configuration(uint8_t address, uint8_t max_packet,
				  int low_speed, uint8_t configuration)
{
	return uhci_control_transfer(address, max_packet, low_speed, 0x00u,
				     USB_REQ_SET_CONFIGURATION, configuration,
				     0u, NULL, 0u, 0);
}

static int uhci_set_protocol(uint8_t address, uint8_t max_packet,
			     int low_speed, uint8_t interface)
{
	return uhci_control_transfer(address, max_packet, low_speed, 0x21u,
				     USB_REQ_SET_PROTOCOL, 0u, interface,
				     NULL, 0u, 0);
}

static int uhci_set_idle(uint8_t address, uint8_t max_packet,
			 int low_speed, uint8_t interface)
{
	return uhci_control_transfer(address, max_packet, low_speed, 0x21u,
				     USB_REQ_SET_IDLE, 0u, interface,
				     NULL, 0u, 0);
}

static int uhci_find_controller(uint16_t *out_base)
{
	uint32_t bus;
	uint32_t dev;
	uint32_t fn;

	if (!out_base)
		return 0;

	for (bus = 0u; bus < 256u; bus++) {
		for (dev = 0u; dev < 32u; dev++) {
			for (fn = 0u; fn < 8u; fn++) {
				pci_addr pci = PCI_ADDR(bus, dev, fn);
				uint32_t id = pci_config_read32(pci, 0x00);
				uint32_t class_reg;
				uint32_t bar;
				uint16_t command;

				if (id == 0xffffffffu || id == 0x00000000u)
					continue;

				class_reg = pci_config_read32(pci, 0x08);
				if (((class_reg >> 24) & 0xffu) != PCI_BASE_CLASS_SERIAL ||
				    ((class_reg >> 16) & 0xffu) != PCI_SUBCLASS_SERIAL_USB ||
				    ((class_reg >> 8) & 0xffu) != 0x00u)
					continue;

				bar = pci_config_read32(pci, 0x20);
				if ((bar & 1u) == 0u)
					continue;

				command = pci_config_read16(pci, 0x04);
				pci_config_write16(pci, 0x04, command | 0x0005u);
				*out_base = (uint16_t)(bar & ~0x1fu);
				return *out_base != 0u;
			}
		}
	}

	return 0;
}

static void uhci_init_schedule(void)
{
	uint32_t index;
	uint32_t qh_link = uhci_phys(&uhci_qh) | UHCI_LINK_QH;

	for (index = 0u; index < UHCI_FRAME_COUNT; index++)
		uhci_frame_list[index] = qh_link;

	uhci_qh.head = UHCI_LINK_TERMINATE;
	uhci_qh.element = UHCI_LINK_TERMINATE;
}

static void uhci_start_controller(void)
{
	uint32_t wait;

	outw(0u, usb_uhci_base + UHCI_USBCMD);
	outw(0xffffu, usb_uhci_base + UHCI_USBSTS);
	outw(0u, usb_uhci_base + UHCI_USBINTR);
	outw(UHCI_CMD_HCRESET, usb_uhci_base + UHCI_USBCMD);
	for (wait = 0u; wait < 1000u; wait++) {
		if ((inw(usb_uhci_base + UHCI_USBCMD) & UHCI_CMD_HCRESET) == 0u)
			break;
		uhci_delay_ms(1u);
	}

	uhci_init_schedule();
	outw(0u, usb_uhci_base + UHCI_FRNUM);
	outl(uhci_phys(uhci_frame_list), usb_uhci_base + UHCI_FLBASEADD);
	outb(0x40u, usb_uhci_base + UHCI_SOFMOD);
	outw(0xffffu, usb_uhci_base + UHCI_USBSTS);
	outw(UHCI_CMD_RUN | UHCI_CMD_CONFIGURE | UHCI_CMD_MAX_PACKET,
	     usb_uhci_base + UHCI_USBCMD);
	uhci_delay_ms(10u);
}

static int uhci_reset_port(uint16_t port_offset, int *out_low_speed)
{
	uint16_t status;

	if (out_low_speed)
		*out_low_speed = 0;

	status = inw(usb_uhci_base + port_offset);
	if ((status & UHCI_PORT_CONNECTED) == 0u)
		return 0;

	outw((status & ~(UHCI_PORT_ENABLED | UHCI_PORT_RESET)) |
	     UHCI_PORT_CHANGE_MASK | UHCI_PORT_RESET,
	     usb_uhci_base + port_offset);
	uhci_delay_ms(50u);

	status = inw(usb_uhci_base + port_offset);
	outw((status & ~UHCI_PORT_RESET) | UHCI_PORT_CHANGE_MASK,
	     usb_uhci_base + port_offset);
	uhci_delay_ms(20u);

	status = inw(usb_uhci_base + port_offset);
	outw((status | UHCI_PORT_ENABLED | UHCI_PORT_CHANGE_MASK) &
	     ~UHCI_PORT_RESET, usb_uhci_base + port_offset);
	uhci_delay_ms(20u);

	status = inw(usb_uhci_base + port_offset);
	if ((status & UHCI_PORT_CONNECTED) == 0u)
		return 0;
	if (out_low_speed)
		*out_low_speed = (status & UHCI_PORT_LOW_SPEED) != 0u;
	return 1;
}

static int usb_parse_keyboard_config(uint8_t *data, uint32_t length,
				     uint8_t *out_configuration,
				     uint8_t *out_interface,
				     uint8_t *out_endpoint,
				     uint8_t *out_packet)
{
	uint32_t offset = 0u;
	int keyboard_interface = 0;

	if (!data || length < 9u || !out_configuration || !out_interface ||
	    !out_endpoint || !out_packet)
		return 0;

	*out_configuration = data[5];
	*out_interface = 0u;
	*out_endpoint = 0u;
	*out_packet = UHCI_REPORT_BYTES;

	while (offset + 2u <= length) {
		uint8_t descriptor_length = data[offset];
		uint8_t descriptor_type = data[offset + 1u];

		if (descriptor_length < 2u || offset + descriptor_length > length)
			break;

		if (descriptor_type == USB_DESC_INTERFACE &&
		    descriptor_length >= 9u) {
			keyboard_interface = data[offset + 5u] == 0x03u &&
					     data[offset + 6u] == 0x01u &&
					     data[offset + 7u] == 0x01u;
			if (keyboard_interface)
				*out_interface = data[offset + 2u];
		} else if (keyboard_interface &&
			   descriptor_type == USB_DESC_ENDPOINT &&
			   descriptor_length >= 7u) {
			uint8_t endpoint = data[offset + 2u];
			uint8_t attributes = data[offset + 3u];
			uint16_t packet = (uint16_t)data[offset + 4u] |
					  ((uint16_t)data[offset + 5u] << 8);

			if ((endpoint & 0x80u) != 0u &&
			    (attributes & 0x03u) == 0x03u) {
				*out_endpoint = endpoint & 0x0fu;
				if (packet != 0u && packet < *out_packet)
					*out_packet = (uint8_t)packet;
				return 1;
			}
		}

		offset += descriptor_length;
	}

	return 0;
}

static int usb_configure_keyboard_on_port(uint16_t port_offset,
					  uint8_t address)
{
	int low_speed = 0;
	uint8_t max_packet;
	uint8_t configuration;
	uint8_t interface;
	uint8_t endpoint;
	uint8_t packet;
	uint16_t total_length;

	if (!uhci_reset_port(port_offset, &low_speed))
		return 0;

	memset(uhci_data, 0, sizeof(uhci_data));
	if (!uhci_get_descriptor(0u, 8u, low_speed, USB_DESC_DEVICE, 0u,
				 uhci_data, 8u))
		return 0;

	max_packet = uhci_data[7];
	if (max_packet == 0u || max_packet > 64u)
		max_packet = 8u;

	if (!uhci_set_address(address, low_speed))
		return 0;
	uhci_delay_ms(10u);

	memset(uhci_data, 0, sizeof(uhci_data));
	if (!uhci_get_descriptor(address, max_packet, low_speed, USB_DESC_DEVICE,
				 0u, uhci_data, 18u))
		return 0;

	memset(uhci_data, 0, sizeof(uhci_data));
	if (!uhci_get_descriptor(address, max_packet, low_speed,
				 USB_DESC_CONFIGURATION, 0u, uhci_data, 9u))
		return 0;

	total_length = (uint16_t)uhci_data[2] | ((uint16_t)uhci_data[3] << 8);
	if (total_length < 9u || total_length > sizeof(uhci_data))
		return 0;

	memset(uhci_data, 0, sizeof(uhci_data));
	if (!uhci_get_descriptor(address, max_packet, low_speed,
				 USB_DESC_CONFIGURATION, 0u, uhci_data,
				 total_length))
		return 0;

	if (!usb_parse_keyboard_config(uhci_data, total_length, &configuration,
				       &interface, &endpoint, &packet))
		return 0;

	if (!uhci_set_configuration(address, max_packet, low_speed, configuration))
		return 0;
	uhci_delay_ms(10u);

	(void)uhci_set_protocol(address, max_packet, low_speed, interface);
	(void)uhci_set_idle(address, max_packet, low_speed, interface);

	usb_keyboard_address = address;
	usb_keyboard_endpoint = endpoint;
	usb_keyboard_max_packet = packet;
	usb_keyboard_low_speed = (uint8_t)(low_speed != 0);
	usb_keyboard_toggle = 0u;
	memset(usb_keyboard_last_report, 0, sizeof(usb_keyboard_last_report));
	usb_keyboard_ready = 1;
	return 1;
}

static void usb_keyboard_probe(void)
{
	if (usb_probe_done)
		return;
	usb_probe_done = 1;

	if (!uhci_find_controller(&usb_uhci_base))
		return;

	uhci_start_controller();
	if (usb_configure_keyboard_on_port(UHCI_PORTSC1, 1u))
		return;
	(void)usb_configure_keyboard_on_port(UHCI_PORTSC2, 2u);
}

static int usb_report_has_key(const uint8_t *report, uint8_t key);
static char usb_hid_usage_to_ascii(uint8_t usage, uint8_t modifiers);

static uint32_t ehci_phys(const void *ptr)
{
	return (uint32_t)virt_to_phys(ptr);
}

static uint32_t ehci_qtd_link(const ehci_qtd_t *qtd)
{
	return ehci_phys(qtd) & ~0x1fu;
}

static volatile uint8_t *ehci_reg_ptr(uint32_t offset)
{
	return (volatile uint8_t *)phys_to_virt(ehci_mmio_base + offset);
}

static uint8_t ehci_read8(uint32_t offset)
{
	return *ehci_reg_ptr(offset);
}

static uint32_t ehci_read32(uint32_t offset)
{
	return *(volatile uint32_t *)ehci_reg_ptr(offset);
}

static void ehci_write32(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)ehci_reg_ptr(offset) = value;
}

static uint32_t ehci_op(uint32_t offset)
{
	return ehci_cap_length + offset;
}

static uint32_t ehci_port_offset(uint32_t port)
{
	return ehci_op(EHCI_OP_PORTSC_BASE + (port - 1u) * 4u);
}

static void ehci_zero_qh_overlay(ehci_qh_t *qh)
{
	uint32_t index;

	qh->current_qtd = 0u;
	qh->next_qtd = EHCI_LINK_TERMINATE;
	qh->alternate_next_qtd = EHCI_LINK_TERMINATE;
	qh->token = 0u;
	for (index = 0u; index < 5u; index++) {
		qh->buffer[index] = 0u;
		qh->buffer_high[index] = 0u;
	}
}

static void ehci_set_qtd_buffer(ehci_qtd_t *qtd, void *buffer, uint32_t length)
{
	uint32_t address;
	uint32_t page;
	uint32_t index;

	for (index = 0u; index < 5u; index++) {
		qtd->buffer[index] = 0u;
		qtd->buffer_high[index] = 0u;
	}
	if (!buffer || length == 0u)
		return;

	address = ehci_phys(buffer);
	page = address & ~0xfffu;
	qtd->buffer[0] = address;
	for (index = 1u; index < 5u; index++)
		qtd->buffer[index] = page + index * 0x1000u;
}

static void ehci_prepare_qtd(ehci_qtd_t *qtd, uint32_t pid, uint32_t toggle,
			     void *buffer, uint32_t length, int interrupt)
{
	qtd->next = EHCI_LINK_TERMINATE;
	qtd->alternate_next = EHCI_LINK_TERMINATE;
	qtd->token = EHCI_QTD_STATUS_ACTIVE |
		     ((pid & 0x3u) << 8) |
		     EHCI_QTD_CERR |
		     (interrupt ? EHCI_QTD_IOC : 0u) |
		     EHCI_QTD_BYTES(length) |
		     EHCI_QTD_TOGGLE(toggle != 0u);
	ehci_set_qtd_buffer(qtd, buffer, length);
}

static void ehci_prepare_control_qh(uint8_t address, uint8_t max_packet)
{
	if (max_packet == 0u)
		max_packet = 64u;

	ehci_async_qh.horizontal = ehci_phys(&ehci_async_qh) | EHCI_LINK_QH;
	ehci_async_qh.endpoint_characteristics =
		EHCI_QH_HEAD |
		EHCI_QH_DTC |
		((uint32_t)address & 0x7fu) |
		EHCI_QH_SPEED_HIGH |
		((uint32_t)max_packet << 16) |
		(1u << 28);
	ehci_async_qh.endpoint_capabilities = EHCI_QH_C_MASK_NONE;
	ehci_zero_qh_overlay(&ehci_async_qh);
}

static int ehci_run_control_qtds(uint32_t count, uint32_t timeout_ms)
{
	uint32_t index;
	uint32_t elapsed;
	int complete;

	if (count == 0u || count > EHCI_QTD_COUNT)
		return 0;

	for (index = 0u; index + 1u < count; index++)
		ehci_qtds[index].next = ehci_qtd_link(&ehci_qtds[index + 1u]);
	ehci_qtds[count - 1u].next = EHCI_LINK_TERMINATE;

	ehci_async_qh.current_qtd = 0u;
	ehci_async_qh.next_qtd = ehci_qtd_link(&ehci_qtds[0]);
	ehci_async_qh.alternate_next_qtd = EHCI_LINK_TERMINATE;
	ehci_async_qh.token = 0u;

	for (elapsed = 0u; elapsed < timeout_ms; elapsed++) {
		complete = 1;
		for (index = 0u; index < count; index++) {
			if ((ehci_qtds[index].token & EHCI_QTD_STATUS_ACTIVE) != 0u) {
				complete = 0;
				break;
			}
		}
		if (complete)
			break;
		uhci_delay_ms(1u);
	}

	ehci_async_qh.next_qtd = EHCI_LINK_TERMINATE;
	ehci_async_qh.alternate_next_qtd = EHCI_LINK_TERMINATE;

	if (!complete) {
		for (index = 0u; index < count; index++)
			ehci_qtds[index].token &= ~EHCI_QTD_STATUS_ACTIVE;
		return 0;
	}

	for (index = 0u; index < count; index++) {
		if ((ehci_qtds[index].token & EHCI_QTD_STATUS_ERROR_MASK) != 0u)
			return 0;
	}
	return 1;
}

static int ehci_control_transfer(uint8_t address, uint8_t max_packet,
				 uint8_t request_type, uint8_t request,
				 uint16_t value, uint16_t index_value,
				 void *data, uint32_t length, int data_in)
{
	uint32_t count = 0u;

	if (length != 0u && !data)
		return 0;
	if (length > EHCI_DATA_BYTES)
		return 0;

	ehci_prepare_control_qh(address, max_packet);

	ehci_setup_packet[0] = request_type;
	ehci_setup_packet[1] = request;
	ehci_setup_packet[2] = (uint8_t)(value & 0xffu);
	ehci_setup_packet[3] = (uint8_t)(value >> 8);
	ehci_setup_packet[4] = (uint8_t)(index_value & 0xffu);
	ehci_setup_packet[5] = (uint8_t)(index_value >> 8);
	ehci_setup_packet[6] = (uint8_t)(length & 0xffu);
	ehci_setup_packet[7] = (uint8_t)(length >> 8);

	ehci_prepare_qtd(&ehci_qtds[count++], EHCI_QTD_PID_SETUP, 0u,
			 ehci_setup_packet, sizeof(ehci_setup_packet), 0);
	if (length != 0u) {
		ehci_prepare_qtd(&ehci_qtds[count++],
				 data_in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT,
				 1u, data, length, 0);
	}
	ehci_prepare_qtd(&ehci_qtds[count++],
			 data_in && length != 0u ? EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN,
			 1u, NULL, 0u, 1);

	return ehci_run_control_qtds(count, 500u);
}

static int ehci_get_descriptor(uint8_t address, uint8_t max_packet,
			       uint8_t descriptor_type, uint8_t descriptor_index,
			       void *data, uint32_t length)
{
	return ehci_control_transfer(address, max_packet, 0x80u,
				     USB_REQ_GET_DESCRIPTOR,
				     ((uint16_t)descriptor_type << 8) |
				     descriptor_index, 0u, data, length, 1);
}

static int ehci_set_address(uint8_t address)
{
	return ehci_control_transfer(0u, 64u, 0x00u, USB_REQ_SET_ADDRESS,
				     address, 0u, NULL, 0u, 0);
}

static int ehci_set_configuration(uint8_t address, uint8_t max_packet,
				  uint8_t configuration)
{
	return ehci_control_transfer(address, max_packet, 0x00u,
				     USB_REQ_SET_CONFIGURATION, configuration,
				     0u, NULL, 0u, 0);
}

static int ehci_set_protocol(uint8_t address, uint8_t max_packet,
			     uint8_t interface)
{
	return ehci_control_transfer(address, max_packet, 0x21u,
				     USB_REQ_SET_PROTOCOL, 0u, interface,
				     NULL, 0u, 0);
}

static int ehci_set_idle(uint8_t address, uint8_t max_packet,
			 uint8_t interface)
{
	return ehci_control_transfer(address, max_packet, 0x21u,
				     USB_REQ_SET_IDLE, 0u, interface,
				     NULL, 0u, 0);
}

static int ehci_find_controller(uint32_t *out_base)
{
	uint32_t bus;
	uint32_t dev;
	uint32_t fn;

	if (!out_base)
		return 0;

	for (bus = 0u; bus < 256u; bus++) {
		for (dev = 0u; dev < 32u; dev++) {
			for (fn = 0u; fn < 8u; fn++) {
				pci_addr pci = PCI_ADDR(bus, dev, fn);
				uint32_t id = pci_config_read32(pci, 0x00);
				uint32_t class_reg;
				uint32_t bar;
				uint16_t command;

				if (id == 0xffffffffu || id == 0x00000000u)
					continue;

				class_reg = pci_config_read32(pci, 0x08);
				if (((class_reg >> 24) & 0xffu) != PCI_BASE_CLASS_SERIAL ||
				    ((class_reg >> 16) & 0xffu) != PCI_SUBCLASS_SERIAL_USB ||
				    ((class_reg >> 8) & 0xffu) != 0x20u)
					continue;

				bar = pci_config_read32(pci, 0x10);
				if ((bar & 1u) != 0u)
					continue;

				command = pci_config_read16(pci, 0x04);
				pci_config_write16(pci, 0x04, command | 0x0006u);
				*out_base = bar & ~0x0fu;
				return *out_base != 0u;
			}
		}
	}

	return 0;
}

static int ehci_start_controller(void)
{
	uint32_t wait;
	uint32_t index;
	uint32_t hcsparams;

	if (!ehci_find_controller(&ehci_mmio_base))
		return 0;

	ehci_cap_length = ehci_read8(0u);
	hcsparams = ehci_read32(EHCI_CAP_HCSPARAMS);
	ehci_max_ports = hcsparams & 0x0fu;
	if (ehci_max_ports == 0u)
		return 0;

	ehci_write32(ehci_op(EHCI_OP_USBCMD),
		     ehci_read32(ehci_op(EHCI_OP_USBCMD)) & ~EHCI_CMD_RUN);
	for (wait = 0u; wait < 1000u; wait++) {
		if ((ehci_read32(ehci_op(EHCI_OP_USBSTS)) & EHCI_STS_HALTED) != 0u)
			break;
		uhci_delay_ms(1u);
	}

	ehci_write32(ehci_op(EHCI_OP_USBCMD), EHCI_CMD_RESET);
	for (wait = 0u; wait < 1000u; wait++) {
		if ((ehci_read32(ehci_op(EHCI_OP_USBCMD)) & EHCI_CMD_RESET) == 0u)
			break;
		uhci_delay_ms(1u);
	}
	if (wait == 1000u)
		return 0;

	for (index = 0u; index < EHCI_FRAME_COUNT; index++)
		ehci_periodic_list[index] = EHCI_LINK_TERMINATE;

	memset(&ehci_async_qh, 0, sizeof(ehci_async_qh));
	ehci_prepare_control_qh(0u, 64u);
	memset(&ehci_keyboard_qh, 0, sizeof(ehci_keyboard_qh));
	memset(ehci_keyboard_last_report, 0, sizeof(ehci_keyboard_last_report));
	ehci_interrupt_pending = 0u;

	ehci_write32(ehci_op(EHCI_OP_CTRLDSSEGMENT), 0u);
	ehci_write32(ehci_op(EHCI_OP_PERIODICLISTBASE),
		     ehci_phys(ehci_periodic_list));
	ehci_write32(ehci_op(EHCI_OP_ASYNCLISTADDR),
		     ehci_phys(&ehci_async_qh));
	ehci_write32(ehci_op(EHCI_OP_USBINTR), 0u);
	ehci_write32(ehci_op(EHCI_OP_CONFIGFLAG), 1u);
	ehci_write32(ehci_op(EHCI_OP_USBCMD),
		     EHCI_CMD_RUN | EHCI_CMD_ASYNC_ENABLE);

	for (wait = 0u; wait < 1000u; wait++) {
		if ((ehci_read32(ehci_op(EHCI_OP_USBSTS)) & EHCI_STS_HALTED) == 0u)
			return 1;
		uhci_delay_ms(1u);
	}
	return 0;
}

static int ehci_reset_port(uint32_t port)
{
	uint32_t portsc = ehci_port_offset(port);
	uint32_t status;
	uint32_t wait;

	status = ehci_read32(portsc);
	ehci_write32(portsc, (status | EHCI_PORT_POWER | EHCI_PORT_CHANGE_BITS) &
		     ~EHCI_PORT_RESET);
	uhci_delay_ms(20u);

	status = ehci_read32(portsc);
	if ((status & EHCI_PORT_CONNECTED) == 0u)
		return 0;

	ehci_write32(portsc, (status | EHCI_PORT_POWER |
		     EHCI_PORT_CHANGE_BITS | EHCI_PORT_RESET) &
		     ~EHCI_PORT_OWNER);
	uhci_delay_ms(50u);
	status = ehci_read32(portsc);
	ehci_write32(portsc, (status | EHCI_PORT_POWER |
		     EHCI_PORT_CHANGE_BITS) & ~EHCI_PORT_RESET);
	for (wait = 0u; wait < 250u; wait++) {
		status = ehci_read32(portsc);
		if ((status & EHCI_PORT_RESET) == 0u)
			break;
		uhci_delay_ms(1u);
	}

	status = ehci_read32(portsc);
	ehci_write32(portsc, (status | EHCI_PORT_POWER |
		     EHCI_PORT_CHANGE_BITS) & ~EHCI_PORT_RESET);
	if ((status & EHCI_PORT_OWNER) != 0u)
		return 0;
	if ((status & EHCI_PORT_ENABLED) == 0u)
		return 0;
	uhci_delay_ms(100u);
	return 1;
}

static void ehci_enable_keyboard_periodic(void)
{
	uint32_t index;
	uint32_t link = ehci_phys(&ehci_keyboard_qh) | EHCI_LINK_QH;

	for (index = 0u; index < EHCI_FRAME_COUNT; index++)
		ehci_periodic_list[index] = link;
	ehci_write32(ehci_op(EHCI_OP_USBCMD),
		     ehci_read32(ehci_op(EHCI_OP_USBCMD)) |
		     EHCI_CMD_PERIODIC_ENABLE);
	uhci_delay_ms(5u);
}

static int ehci_configure_keyboard_on_port(uint32_t port, uint8_t address)
{
	uint8_t max_packet;
	uint8_t configuration;
	uint8_t interface;
	uint8_t endpoint;
	uint8_t packet;
	uint16_t total_length;

	if (!ehci_reset_port(port))
		return 0;

	memset(ehci_data, 0, sizeof(ehci_data));
	if (!ehci_get_descriptor(0u, 64u, USB_DESC_DEVICE, 0u,
				 ehci_data, 8u))
		return 0;

	max_packet = ehci_data[7];
	if (max_packet == 0u || max_packet > 64u)
		max_packet = 64u;

	if (!ehci_set_address(address))
		return 0;
	uhci_delay_ms(10u);

	memset(ehci_data, 0, sizeof(ehci_data));
	if (!ehci_get_descriptor(address, max_packet, USB_DESC_DEVICE, 0u,
				 ehci_data, 18u))
		return 0;

	memset(ehci_data, 0, sizeof(ehci_data));
	if (!ehci_get_descriptor(address, max_packet, USB_DESC_CONFIGURATION, 0u,
				 ehci_data, 9u))
		return 0;

	total_length = (uint16_t)ehci_data[2] | ((uint16_t)ehci_data[3] << 8);
	if (total_length < 9u || total_length > sizeof(ehci_data))
		return 0;

	memset(ehci_data, 0, sizeof(ehci_data));
	if (!ehci_get_descriptor(address, max_packet, USB_DESC_CONFIGURATION, 0u,
				 ehci_data, total_length))
		return 0;

	if (!usb_parse_keyboard_config(ehci_data, total_length, &configuration,
				       &interface, &endpoint, &packet))
		return 0;

	if (!ehci_set_configuration(address, max_packet, configuration))
		return 0;
	uhci_delay_ms(10u);
	(void)ehci_set_protocol(address, max_packet, interface);
	(void)ehci_set_idle(address, max_packet, interface);

	if (packet == 0u || packet > UHCI_REPORT_BYTES)
		packet = UHCI_REPORT_BYTES;
	ehci_keyboard_address = address;
	ehci_keyboard_endpoint = endpoint;
	ehci_keyboard_packet = packet;
	ehci_keyboard_toggle = 0u;
	memset(ehci_keyboard_last_report, 0, sizeof(ehci_keyboard_last_report));

	memset(&ehci_keyboard_qh, 0, sizeof(ehci_keyboard_qh));
	ehci_keyboard_qh.horizontal = EHCI_LINK_TERMINATE;
	ehci_keyboard_qh.endpoint_characteristics =
		((uint32_t)address & 0x7fu) |
		(((uint32_t)endpoint & 0x0fu) << 8) |
		EHCI_QH_SPEED_HIGH |
		EHCI_QH_DTC |
		((uint32_t)packet << 16) |
		(1u << 28);
	ehci_keyboard_qh.endpoint_capabilities =
		EHCI_QH_S_MASK_EVERY_FRAME |
		EHCI_QH_C_MASK_NONE |
		EHCI_QH_MULT_ONE;
	ehci_zero_qh_overlay(&ehci_keyboard_qh);
	ehci_enable_keyboard_periodic();
	ehci_keyboard_ready = 1;
	return 1;
}

static void ehci_keyboard_probe(void)
{
	uint32_t port;
	uint8_t address = 8u;

	if (ehci_probe_done)
		return;
	ehci_probe_done = 1;

	if (!ehci_start_controller())
		return;

	for (port = 1u; port <= ehci_max_ports; port++) {
		if (ehci_configure_keyboard_on_port(port, address++))
			return;
	}
}

static int ehci_keyboard_poll(void)
{
	uint32_t index;
	uint32_t token;

	ehci_keyboard_probe();
	if (!ehci_keyboard_ready)
		return 0;

	if (!ehci_interrupt_pending) {
		memset(ehci_report, 0, sizeof(ehci_report));
		ehci_prepare_qtd(&ehci_interrupt_qtd, EHCI_QTD_PID_IN,
				 ehci_keyboard_toggle, ehci_report,
				 ehci_keyboard_packet, 1);
		ehci_keyboard_qh.current_qtd = 0u;
		ehci_keyboard_qh.next_qtd = ehci_qtd_link(&ehci_interrupt_qtd);
		ehci_keyboard_qh.alternate_next_qtd = EHCI_LINK_TERMINATE;
		ehci_keyboard_qh.token = 0u;
		ehci_interrupt_pending = 1u;
	}

	token = ehci_interrupt_qtd.token;
	if ((token & EHCI_QTD_STATUS_ACTIVE) != 0u)
		return 0;

	ehci_keyboard_qh.next_qtd = EHCI_LINK_TERMINATE;
	ehci_keyboard_qh.alternate_next_qtd = EHCI_LINK_TERMINATE;
	ehci_interrupt_pending = 0u;
	if ((token & EHCI_QTD_STATUS_ERROR_MASK) != 0u)
		return 0;

	ehci_keyboard_toggle ^= 1u;
	for (index = 2u; index < UHCI_REPORT_BYTES; index++) {
		uint8_t usage = ehci_report[index];
		char ascii;

		if (usage == 0u || usb_report_has_key(ehci_keyboard_last_report, usage))
			continue;

		ascii = usb_hid_usage_to_ascii(usage, ehci_report[0]);
		memcpy(ehci_keyboard_last_report, ehci_report,
		       sizeof(ehci_keyboard_last_report));
		return ascii;
	}

	memcpy(ehci_keyboard_last_report, ehci_report,
	       sizeof(ehci_keyboard_last_report));
	return 0;
}

static uint32_t xhci_phys(const void *ptr)
{
	return (uint32_t)virt_to_phys(ptr);
}

static volatile uint8_t *xhci_reg_ptr(uint32_t offset)
{
	return (volatile uint8_t *)phys_to_virt(xhci_mmio_base + offset);
}

static uint8_t xhci_read8(uint32_t offset)
{
	return *xhci_reg_ptr(offset);
}

static uint32_t xhci_read32(uint32_t offset)
{
	return *(volatile uint32_t *)xhci_reg_ptr(offset);
}

static void xhci_write32(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)xhci_reg_ptr(offset) = value;
}

static void xhci_write64(uint32_t offset, uint32_t low, uint32_t high)
{
	xhci_write32(offset, low);
	xhci_write32(offset + 4u, high);
}

static uint32_t xhci_op(uint32_t offset)
{
	return xhci_cap_length + offset;
}

static uint32_t xhci_port_offset(uint32_t port)
{
	return xhci_op(XHCI_OP_PORT_BASE + (port - 1u) * XHCI_PORT_STRIDE);
}

static uint32_t xhci_trb_type(const xhci_trb_t *trb)
{
	return (trb->control >> 10) & 0x3fu;
}

static uint32_t xhci_completion_code(const xhci_trb_t *trb)
{
	return (trb->status >> 24) & 0xffu;
}

static void xhci_set_link_trb(xhci_trb_t *ring, uint8_t cycle)
{
	xhci_trb_t *trb = &ring[XHCI_RING_LINK_INDEX];
	trb->parameter_low = xhci_phys(ring);
	trb->parameter_high = 0u;
	trb->status = 0u;
	trb->control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_LINK) |
		       XHCI_TRB_LINK_TC |
		       (cycle ? XHCI_TRB_CYCLE : 0u);
}

static void xhci_init_transfer_ring(xhci_trb_t *ring, uint32_t *enqueue,
				    uint8_t *cycle)
{
	memset(ring, 0, sizeof(xhci_trb_t) * XHCI_RING_TRBS);
	*enqueue = 0u;
	*cycle = 1u;
	xhci_set_link_trb(ring, *cycle);
}

static uint32_t xhci_push_trb(xhci_trb_t *ring, uint32_t *enqueue,
			      uint8_t *cycle, uint32_t parameter_low,
			      uint32_t parameter_high, uint32_t status,
			      uint32_t control)
{
	xhci_trb_t *trb;
	uint32_t phys;

	if (*enqueue >= XHCI_RING_LINK_INDEX) {
		xhci_set_link_trb(ring, *cycle);
		*enqueue = 0u;
		*cycle = (uint8_t)(*cycle == 0u);
	}

	trb = &ring[*enqueue];
	phys = xhci_phys(trb);
	trb->parameter_low = parameter_low;
	trb->parameter_high = parameter_high;
	trb->status = status;
	trb->control = control | (*cycle ? XHCI_TRB_CYCLE : 0u);

	*enqueue = *enqueue + 1u;
	if (*enqueue == XHCI_RING_LINK_INDEX) {
		xhci_set_link_trb(ring, *cycle);
		*enqueue = 0u;
		*cycle = (uint8_t)(*cycle == 0u);
	}

	return phys;
}

static void xhci_ring_doorbell(uint32_t slot, uint32_t target)
{
	xhci_write32(xhci_doorbell_offset + slot * 4u, target);
}

static void xhci_update_erdp(void)
{
	xhci_write64(xhci_runtime_offset + XHCI_INTR_ERDP,
		     xhci_phys(&xhci_event_ring[xhci_event_dequeue]) | 0x8u,
		     0u);
}

static int xhci_pop_event(xhci_trb_t *out)
{
	volatile xhci_trb_t *event = &xhci_event_ring[xhci_event_dequeue];
	uint32_t expected_cycle = xhci_event_cycle ? XHCI_TRB_CYCLE : 0u;

	if ((event->control & XHCI_TRB_CYCLE) != expected_cycle)
		return 0;

	out->parameter_low = event->parameter_low;
	out->parameter_high = event->parameter_high;
	out->status = event->status;
	out->control = event->control;

	xhci_event_dequeue++;
	if (xhci_event_dequeue == XHCI_RING_TRBS) {
		xhci_event_dequeue = 0u;
		xhci_event_cycle = (uint8_t)(xhci_event_cycle == 0u);
	}
	xhci_update_erdp();
	return 1;
}

static int xhci_wait_command(uint32_t command_trb, uint8_t *out_slot)
{
	uint32_t elapsed;

	for (elapsed = 0u; elapsed < 500u; elapsed++) {
		xhci_trb_t event;
		while (xhci_pop_event(&event)) {
			if (xhci_trb_type(&event) == XHCI_TRB_TYPE_COMMAND_COMPLETION &&
			    event.parameter_low == command_trb) {
				if (out_slot)
					*out_slot = (uint8_t)(event.control >> 24);
				return xhci_completion_code(&event) == XHCI_CC_SUCCESS;
			}
		}
		uhci_delay_ms(1u);
	}

	return 0;
}

static int xhci_command(uint32_t parameter_low, uint32_t status,
			uint32_t control, uint8_t *out_slot)
{
	uint32_t command_trb = xhci_push_trb(xhci_command_ring,
					     &xhci_command_enqueue,
					     &xhci_command_cycle,
					     parameter_low, 0u, status, control);
	xhci_ring_doorbell(0u, 0u);
	return xhci_wait_command(command_trb, out_slot);
}

static int xhci_wait_transfer(uint8_t slot, uint8_t dci, uint32_t transfer_trb,
			      uint32_t timeout_ms)
{
	uint32_t elapsed;

	for (elapsed = 0u; elapsed < timeout_ms; elapsed++) {
		xhci_trb_t event;
		while (xhci_pop_event(&event)) {
			uint32_t completion;

			if (xhci_trb_type(&event) != XHCI_TRB_TYPE_TRANSFER_EVENT)
				continue;
			if ((uint8_t)(event.control >> 24) != slot ||
			    ((event.control >> 16) & 0x1fu) != dci ||
			    event.parameter_low != transfer_trb)
				continue;

			completion = xhci_completion_code(&event);
			return completion == XHCI_CC_SUCCESS ||
			       completion == XHCI_CC_SHORT_PACKET;
		}
		uhci_delay_ms(1u);
	}

	return 0;
}

static uint32_t *xhci_input_control_context(void)
{
	return (uint32_t *)xhci_input_context;
}

static uint32_t *xhci_input_device_context(uint32_t dci)
{
	return (uint32_t *)(xhci_input_context + (dci + 1u) * xhci_context_size);
}

static void xhci_write_device_context_pointer(uint8_t slot, void *context)
{
	xhci_dcbaa[slot] = (uint64_t)xhci_phys(context);
}

static int xhci_find_controller(uint32_t *out_base)
{
	uint32_t bus;
	uint32_t dev;
	uint32_t fn;

	if (!out_base)
		return 0;

	for (bus = 0u; bus < 256u; bus++) {
		for (dev = 0u; dev < 32u; dev++) {
			for (fn = 0u; fn < 8u; fn++) {
				pci_addr pci = PCI_ADDR(bus, dev, fn);
				uint32_t id = pci_config_read32(pci, 0x00);
				uint32_t class_reg;
				uint32_t bar;
				uint16_t command;

				if (id == 0xffffffffu || id == 0x00000000u)
					continue;

				class_reg = pci_config_read32(pci, 0x08);
				if (((class_reg >> 24) & 0xffu) != PCI_BASE_CLASS_SERIAL ||
				    ((class_reg >> 16) & 0xffu) != PCI_SUBCLASS_SERIAL_USB ||
				    ((class_reg >> 8) & 0xffu) != 0x30u)
					continue;

				bar = pci_config_read32(pci, 0x10);
				if ((bar & 1u) != 0u)
					continue;

				command = pci_config_read16(pci, 0x04);
				pci_config_write16(pci, 0x04, command | 0x0006u);
				*out_base = bar & ~0x0fu;
				return *out_base != 0u;
			}
		}
	}

	return 0;
}

static uint32_t xhci_scratchpad_count(uint32_t hcs2)
{
	return ((hcs2 >> 21) & 0x1fu) | (((hcs2 >> 27) & 0x1fu) << 5);
}

static int xhci_start_controller(void)
{
	uint32_t hcs1;
	uint32_t hcs2;
	uint32_t hcc1;
	uint32_t max_slots;
	uint32_t wait;

	if (!xhci_find_controller(&xhci_mmio_base))
		return 0;

	xhci_cap_length = xhci_read8(0u);
	hcs1 = xhci_read32(XHCI_CAP_HCSPARAMS1);
	hcs2 = xhci_read32(XHCI_CAP_HCSPARAMS2);
	hcc1 = xhci_read32(XHCI_CAP_HCCPARAMS1);
	max_slots = hcs1 & 0xffu;
	if (max_slots > XHCI_MAX_SLOTS)
		max_slots = XHCI_MAX_SLOTS;
	xhci_max_ports = (hcs1 >> 24) & 0xffu;
	xhci_context_size = (hcc1 & 0x00000004u) ? 64u : 32u;
	xhci_doorbell_offset = xhci_read32(XHCI_CAP_DBOFF) & ~0x3u;
	xhci_runtime_offset = xhci_read32(XHCI_CAP_RTSOFF) & ~0x1fu;

	if (max_slots == 0u || xhci_max_ports == 0u ||
	    xhci_scratchpad_count(hcs2) != 0u)
		return 0;
	if ((xhci_read32(xhci_op(XHCI_OP_PAGESIZE)) & 0x1u) == 0u)
		return 0;

	xhci_write32(xhci_op(XHCI_OP_USBCMD),
		     xhci_read32(xhci_op(XHCI_OP_USBCMD)) & ~XHCI_CMD_RUN);
	for (wait = 0u; wait < 1000u; wait++) {
		if ((xhci_read32(xhci_op(XHCI_OP_USBSTS)) & XHCI_STS_HCH) != 0u)
			break;
		uhci_delay_ms(1u);
	}

	xhci_write32(xhci_op(XHCI_OP_USBCMD), XHCI_CMD_HCRST);
	for (wait = 0u; wait < 1000u; wait++) {
		if ((xhci_read32(xhci_op(XHCI_OP_USBCMD)) & XHCI_CMD_HCRST) == 0u &&
		    (xhci_read32(xhci_op(XHCI_OP_USBSTS)) & XHCI_STS_CNR) == 0u)
			break;
		uhci_delay_ms(1u);
	}
	if (wait == 1000u)
		return 0;

	memset(xhci_dcbaa, 0, sizeof(xhci_dcbaa));
	memset(xhci_input_context, 0, sizeof(xhci_input_context));
	memset(xhci_device_context, 0, sizeof(xhci_device_context));
	memset(xhci_event_ring, 0, sizeof(xhci_event_ring));
	memset(xhci_erst, 0, sizeof(xhci_erst));
	memset(xhci_keyboard_last_report, 0, sizeof(xhci_keyboard_last_report));
	xhci_interrupt_pending = 0u;
	xhci_interrupt_trb = 0u;
	xhci_init_transfer_ring(xhci_command_ring, &xhci_command_enqueue,
				&xhci_command_cycle);
	xhci_init_transfer_ring(xhci_control_ring, &xhci_control_enqueue,
				&xhci_control_cycle);
	xhci_init_transfer_ring(xhci_interrupt_ring, &xhci_interrupt_enqueue,
				&xhci_interrupt_cycle);
	xhci_event_dequeue = 0u;
	xhci_event_cycle = 1u;

	xhci_erst[0].base_low = xhci_phys(xhci_event_ring);
	xhci_erst[0].base_high = 0u;
	xhci_erst[0].size = XHCI_RING_TRBS;
	xhci_erst[0].reserved = 0u;

	xhci_write32(xhci_op(XHCI_OP_CONFIG), max_slots);
	xhci_write64(xhci_op(XHCI_OP_DCBAAP), xhci_phys(xhci_dcbaa), 0u);
	xhci_write64(xhci_op(XHCI_OP_CRCR),
		     xhci_phys(xhci_command_ring) | XHCI_TRB_CYCLE, 0u);
	xhci_write32(xhci_runtime_offset + XHCI_INTR_ERSTSZ, 1u);
	xhci_write64(xhci_runtime_offset + XHCI_INTR_ERSTBA,
		     xhci_phys(xhci_erst), 0u);
	xhci_update_erdp();
	xhci_write32(xhci_runtime_offset + XHCI_INTR_IMAN, 0x2u);
	xhci_write32(xhci_op(XHCI_OP_USBCMD), XHCI_CMD_RUN | XHCI_CMD_INTE);

	for (wait = 0u; wait < 1000u; wait++) {
		if ((xhci_read32(xhci_op(XHCI_OP_USBSTS)) & XHCI_STS_HCH) == 0u)
			return 1;
		uhci_delay_ms(1u);
	}

	return 0;
}

static int xhci_reset_port(uint32_t port, uint8_t *out_speed)
{
	uint32_t portsc = xhci_port_offset(port);
	uint32_t status;
	uint32_t wait;

	if (out_speed)
		*out_speed = 0u;

	status = xhci_read32(portsc);
	if ((status & XHCI_PORT_CCS) == 0u)
		return 0;

	if ((status & XHCI_PORT_PP) == 0u) {
		xhci_write32(portsc, XHCI_PORT_PP | XHCI_PORT_CHANGE_BITS);
		uhci_delay_ms(20u);
	}

	status = xhci_read32(portsc);
	xhci_write32(portsc, (status & XHCI_PORT_PP) |
		     XHCI_PORT_PR | XHCI_PORT_CHANGE_BITS);
	for (wait = 0u; wait < 250u; wait++) {
		status = xhci_read32(portsc);
		if ((status & XHCI_PORT_PR) == 0u)
			break;
		uhci_delay_ms(1u);
	}

	status = xhci_read32(portsc);
	xhci_write32(portsc, (status & XHCI_PORT_PP) | XHCI_PORT_CHANGE_BITS);
	if ((status & XHCI_PORT_PED) == 0u)
		return 0;
	if (out_speed)
		*out_speed = (uint8_t)((status >> XHCI_PORT_SPEED_SHIFT) & 0x0fu);
	return 1;
}

static int xhci_enable_slot(uint8_t *out_slot)
{
	return xhci_command(0u, 0u, XHCI_TRB_TYPE(XHCI_TRB_TYPE_ENABLE_SLOT),
			    out_slot);
}

static int xhci_address_device(uint8_t slot, uint32_t port, uint8_t speed,
			       uint8_t max_packet)
{
	uint32_t *input_control;
	uint32_t *slot_context;
	uint32_t *ep0_context;

	if (slot == 0u || slot > XHCI_MAX_SLOTS)
		return 0;
	if (max_packet == 0u)
		max_packet = 8u;

	memset(xhci_input_context, 0, sizeof(xhci_input_context));
	memset(xhci_device_context, 0, sizeof(xhci_device_context));
	input_control = xhci_input_control_context();
	slot_context = xhci_input_device_context(0u);
	ep0_context = xhci_input_device_context(1u);

	input_control[1] = 0x3u;
	slot_context[0] = ((uint32_t)speed << 20) | (1u << 27);
	slot_context[1] = port << 16;
	ep0_context[1] = (3u << 1) | (4u << 3) |
			 ((uint32_t)max_packet << 16);
	ep0_context[2] = xhci_phys(xhci_control_ring) | xhci_control_cycle;
	ep0_context[3] = 0u;
	ep0_context[4] = 8u;

	xhci_write_device_context_pointer(slot, xhci_device_context);
	return xhci_command(xhci_phys(xhci_input_context), 0u,
			    XHCI_TRB_TYPE(XHCI_TRB_TYPE_ADDRESS_DEVICE) |
			    ((uint32_t)slot << 24), NULL);
}

static int xhci_control_transfer(uint8_t request_type, uint8_t request,
				 uint16_t value, uint16_t index_value,
				 void *data, uint32_t length, int data_in)
{
	uint32_t setup_low;
	uint32_t setup_high;
	uint32_t status_trb;
	uint32_t setup_control;
	uint32_t data_control;
	uint32_t status_control;
	uint32_t trt = 0u;

	if (xhci_keyboard_slot == 0u || (length != 0u && !data))
		return 0;

	if (length != 0u)
		trt = data_in ? 3u : 2u;

	setup_low = (uint32_t)request_type |
		    ((uint32_t)request << 8) |
		    ((uint32_t)value << 16);
	setup_high = (uint32_t)index_value | ((uint32_t)length << 16);
	setup_control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_SETUP_STAGE) |
			XHCI_TRB_IDT | (trt << 16);
	xhci_push_trb(xhci_control_ring, &xhci_control_enqueue,
		      &xhci_control_cycle, setup_low, setup_high, 8u,
		      setup_control);

	if (length != 0u) {
		data_control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_DATA_STAGE) |
			       (data_in ? XHCI_TRB_DIR : 0u);
		xhci_push_trb(xhci_control_ring, &xhci_control_enqueue,
			      &xhci_control_cycle, xhci_phys(data), 0u,
			      length, data_control);
	}

	status_control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_STATUS_STAGE) |
			 XHCI_TRB_IOC;
	if (length == 0u || !data_in)
		status_control |= XHCI_TRB_DIR;
	status_trb = xhci_push_trb(xhci_control_ring, &xhci_control_enqueue,
				   &xhci_control_cycle, 0u, 0u, 0u,
				   status_control);

	xhci_ring_doorbell(xhci_keyboard_slot, 1u);
	return xhci_wait_transfer(xhci_keyboard_slot, 1u, status_trb, 250u);
}

static int xhci_get_descriptor(uint8_t descriptor_type, uint8_t descriptor_index,
			       void *data, uint32_t length)
{
	return xhci_control_transfer(0x80u, USB_REQ_GET_DESCRIPTOR,
				     ((uint16_t)descriptor_type << 8) |
				     descriptor_index, 0u, data, length, 1);
}

static int xhci_set_configuration(uint8_t configuration)
{
	return xhci_control_transfer(0x00u, USB_REQ_SET_CONFIGURATION,
				     configuration, 0u, NULL, 0u, 0);
}

static int xhci_set_protocol(uint8_t interface)
{
	return xhci_control_transfer(0x21u, USB_REQ_SET_PROTOCOL, 0u,
				     interface, NULL, 0u, 0);
}

static int xhci_set_idle(uint8_t interface)
{
	return xhci_control_transfer(0x21u, USB_REQ_SET_IDLE, 0u,
				     interface, NULL, 0u, 0);
}

static int xhci_configure_interrupt_endpoint(uint8_t slot, uint32_t port,
					     uint8_t speed, uint8_t endpoint,
					     uint8_t packet)
{
	uint32_t dci = ((uint32_t)endpoint * 2u) + 1u;
	uint32_t *input_control;
	uint32_t *slot_context;
	uint32_t *ep_context;

	if (slot == 0u || slot > XHCI_MAX_SLOTS || dci >= 32u)
		return 0;
	if (packet == 0u || packet > UHCI_REPORT_BYTES)
		packet = UHCI_REPORT_BYTES;

	memset(xhci_input_context, 0, sizeof(xhci_input_context));
	input_control = xhci_input_control_context();
	slot_context = xhci_input_device_context(0u);
	ep_context = xhci_input_device_context(dci);

	input_control[1] = (1u << 0) | (1u << dci);
	slot_context[0] = ((uint32_t)speed << 20) | (dci << 27);
	slot_context[1] = port << 16;
	ep_context[0] = 6u << 16;
	ep_context[1] = (3u << 1) | (7u << 3) | ((uint32_t)packet << 16);
	ep_context[2] = xhci_phys(xhci_interrupt_ring) | xhci_interrupt_cycle;
	ep_context[3] = 0u;
	ep_context[4] = packet | ((uint32_t)packet << 16);

	if (!xhci_command(xhci_phys(xhci_input_context), 0u,
			  XHCI_TRB_TYPE(XHCI_TRB_TYPE_CONFIGURE_ENDPOINT) |
			  ((uint32_t)slot << 24), NULL))
		return 0;

	xhci_keyboard_dci = (uint8_t)dci;
	xhci_keyboard_packet = packet;
	return 1;
}

static int xhci_configure_keyboard_on_port(uint32_t port)
{
	uint8_t speed;
	uint8_t slot;
	uint8_t max_packet;
	uint8_t configuration;
	uint8_t interface;
	uint8_t endpoint;
	uint8_t packet;
	uint16_t total_length;

	if (!xhci_reset_port(port, &speed))
		return 0;
	if (!xhci_enable_slot(&slot))
		return 0;

	xhci_keyboard_slot = slot;
	xhci_keyboard_dci = 1u;
	xhci_keyboard_packet = UHCI_REPORT_BYTES;
	if (!xhci_address_device(slot, port, speed, 8u))
		return 0;
	uhci_delay_ms(10u);

	memset(xhci_data, 0, sizeof(xhci_data));
	if (!xhci_get_descriptor(USB_DESC_DEVICE, 0u, xhci_data, 8u))
		return 0;

	max_packet = xhci_data[7];
	if (max_packet == 0u || max_packet > 64u)
		max_packet = 8u;

	memset(xhci_data, 0, sizeof(xhci_data));
	if (!xhci_get_descriptor(USB_DESC_DEVICE, 0u, xhci_data, 18u))
		return 0;

	memset(xhci_data, 0, sizeof(xhci_data));
	if (!xhci_get_descriptor(USB_DESC_CONFIGURATION, 0u, xhci_data, 9u))
		return 0;

	total_length = (uint16_t)xhci_data[2] | ((uint16_t)xhci_data[3] << 8);
	if (total_length < 9u || total_length > sizeof(xhci_data))
		return 0;

	memset(xhci_data, 0, sizeof(xhci_data));
	if (!xhci_get_descriptor(USB_DESC_CONFIGURATION, 0u, xhci_data,
				 total_length))
		return 0;

	if (!usb_parse_keyboard_config(xhci_data, total_length, &configuration,
				       &interface, &endpoint, &packet))
		return 0;

	(void)max_packet;
	if (!xhci_set_configuration(configuration))
		return 0;
	uhci_delay_ms(10u);

	(void)xhci_set_protocol(interface);
	(void)xhci_set_idle(interface);

	if (!xhci_configure_interrupt_endpoint(slot, port, speed, endpoint, packet))
		return 0;

	memset(xhci_keyboard_last_report, 0, sizeof(xhci_keyboard_last_report));
	xhci_keyboard_ready = 1;
	return 1;
}

static void xhci_keyboard_probe(void)
{
	uint32_t port;

	if (xhci_probe_done)
		return;
	xhci_probe_done = 1;

	if (!xhci_start_controller())
		return;

	for (port = 1u; port <= xhci_max_ports; port++) {
		if (xhci_configure_keyboard_on_port(port))
			return;
	}
}

static int xhci_keyboard_poll(void)
{
	uint32_t index;

	xhci_keyboard_probe();
	if (!xhci_keyboard_ready)
		return 0;

	if (!xhci_interrupt_pending) {
		memset(xhci_report, 0, sizeof(xhci_report));
		xhci_interrupt_trb = xhci_push_trb(xhci_interrupt_ring,
						   &xhci_interrupt_enqueue,
						   &xhci_interrupt_cycle,
						   xhci_phys(xhci_report), 0u,
						   xhci_keyboard_packet,
						   XHCI_TRB_TYPE(XHCI_TRB_TYPE_NORMAL) |
						   XHCI_TRB_IOC | XHCI_TRB_ISP);
		xhci_interrupt_pending = 1u;
		xhci_ring_doorbell(xhci_keyboard_slot, xhci_keyboard_dci);
	}

	if (!xhci_wait_transfer(xhci_keyboard_slot, xhci_keyboard_dci,
				xhci_interrupt_trb, 1u))
		return 0;
	xhci_interrupt_pending = 0u;

	for (index = 2u; index < UHCI_REPORT_BYTES; index++) {
		uint8_t usage = xhci_report[index];
		char ascii;

		if (usage == 0u || usb_report_has_key(xhci_keyboard_last_report, usage))
			continue;

		ascii = usb_hid_usage_to_ascii(usage, xhci_report[0]);
		memcpy(xhci_keyboard_last_report, xhci_report,
		       sizeof(xhci_keyboard_last_report));
		return ascii;
	}

	memcpy(xhci_keyboard_last_report, xhci_report,
	       sizeof(xhci_keyboard_last_report));
	return 0;
}

static int usb_report_has_key(const uint8_t *report, uint8_t key)
{
	uint32_t index;

	if (!report || key == 0u)
		return 0;
	for (index = 2u; index < UHCI_REPORT_BYTES; index++) {
		if (report[index] == key)
			return 1;
	}
	return 0;
}

static char usb_hid_usage_to_ascii(uint8_t usage, uint8_t modifiers)
{
	int shift = (modifiers & 0x22u) != 0u;
	int capital;
	static const char normal_digits[] = "1234567890";
	static const char shifted_digits[] = "!@#$%^&*()";

	if (usage >= 4u && usage <= 29u) {
		char ch = (char)('a' + (usage - 4u));
		capital = shift ^ (usb_keyboard_caps != 0u);
		return capital ? (char)(ch - ('a' - 'A')) : ch;
	}

	if (usage >= 30u && usage <= 39u)
		return shift ? shifted_digits[usage - 30u] :
			       normal_digits[usage - 30u];

	switch (usage) {
	case 40:
	case 88:
		return '\r';
	case 41:
		return 0x1b;
	case 42:
		return '\b';
	case 43:
		return '\t';
	case 44:
		return ' ';
	case 45:
		return shift ? '_' : '-';
	case 46:
		return shift ? '+' : '=';
	case 47:
		return shift ? '{' : '[';
	case 48:
		return shift ? '}' : ']';
	case 49:
		return shift ? '|' : '\\';
	case 51:
		return shift ? ':' : ';';
	case 52:
		return shift ? '"' : '\'';
	case 53:
		return shift ? '~' : '`';
	case 54:
		return shift ? '<' : ',';
	case 55:
		return shift ? '>' : '.';
	case 56:
		return shift ? '?' : '/';
	case 57:
		usb_keyboard_caps = (uint8_t)(usb_keyboard_caps == 0u);
		return 0;
	case 76:
		return 0x7f;
	case 84:
		return '/';
	case 85:
		return '*';
	case 86:
		return '-';
	case 87:
		return '+';
	case 89:
		return '1';
	case 90:
		return '2';
	case 91:
		return '3';
	case 92:
		return '4';
	case 93:
		return '5';
	case 94:
		return '6';
	case 95:
		return '7';
	case 96:
		return '8';
	case 97:
		return '9';
	case 98:
		return '0';
	case 99:
		return '.';
	default:
		return 0;
	}
}

static int usb_keyboard_poll(void)
{
	uint32_t index;
	int result;

	usb_keyboard_probe();
	if (!usb_keyboard_ready)
		return 0;

	memset(uhci_report, 0, sizeof(uhci_report));
	uhci_prepare_td(0u, UHCI_PID_IN, usb_keyboard_address,
			usb_keyboard_endpoint, usb_keyboard_toggle,
			usb_keyboard_max_packet, uhci_report,
			usb_keyboard_low_speed);
	result = uhci_run_tds(1u, 2u, 1);
	if (result != 1)
		return 0;

	usb_keyboard_toggle ^= 1u;

	for (index = 2u; index < UHCI_REPORT_BYTES; index++) {
		uint8_t usage = uhci_report[index];
		char ascii;

		if (usage == 0u || usb_report_has_key(usb_keyboard_last_report, usage))
			continue;

		ascii = usb_hid_usage_to_ascii(usage, uhci_report[0]);
		memcpy(usb_keyboard_last_report, uhci_report,
		       sizeof(usb_keyboard_last_report));
		return ascii;
	}

	memcpy(usb_keyboard_last_report, uhci_report,
	       sizeof(usb_keyboard_last_report));
	return 0;
}

static int usb_keyboard_dataready(void)
{
	if (usb_keyboard_last_key)
		return 1;

	usb_keyboard_last_key = (uint8_t)ehci_keyboard_poll();
	if (!usb_keyboard_last_key)
		usb_keyboard_last_key = (uint8_t)usb_keyboard_poll();
	if (!usb_keyboard_last_key)
		usb_keyboard_last_key = (uint8_t)xhci_keyboard_poll();
	return usb_keyboard_last_key != 0u;
}

static unsigned char usb_keyboard_readdata(void)
{
	unsigned char key;

	while (!usb_keyboard_dataready());
	key = usb_keyboard_last_key;
	usb_keyboard_last_key = 0u;
	return key;
}

#endif


/* ******************************************************************
 *      common functions, implementing simple concurrent console
 * ****************************************************************** */

int putchar(int c)
{
#ifdef CONFIG_DEBUG_CONSOLE_SERIAL
	serial_putchar(c);
#endif
#ifdef CONFIG_DEBUG_CONSOLE_VGA
	video_putchar(c);
#endif
	return c;
}

int availchar(void)
{
#ifdef CONFIG_DEBUG_CONSOLE_SERIAL
	if (uart_charav(CONFIG_SERIAL_PORT))
		return 1;
#endif
#ifdef CONFIG_DEBUG_CONSOLE_VGA
	if (keyboard_dataready())
		return 1;
#endif
#if defined(CONFIG_DRIVER_PCI)
	if (usb_keyboard_dataready())
		return 1;
#endif
	return 0;
}

int getchar(void)
{
#ifdef CONFIG_DEBUG_CONSOLE_SERIAL
	if (uart_charav(CONFIG_SERIAL_PORT))
		return (uart_getchar(CONFIG_SERIAL_PORT));
#endif
#ifdef CONFIG_DEBUG_CONSOLE_VGA
	if (keyboard_dataready())
		return (keyboard_readdata());
#endif
#if defined(CONFIG_DRIVER_PCI)
	if (usb_keyboard_dataready())
		return (usb_keyboard_readdata());
#endif
	return 0;
}

static int winterboot_key_ax_from_ascii(int ch)
{
	unsigned int ascii = (unsigned int)ch & 0xffu;
	unsigned int scan = 0u;

	switch (ascii) {
	case '\n':
		ascii = '\r';
		scan = 0x1cu;
		break;
	case '\r':
		scan = 0x1cu;
		break;
	case '\b':
	case 0x7fu:
		ascii = '\b';
		scan = 0x0eu;
		break;
	case 0x1bu:
		scan = 0x01u;
		break;
	default:
		break;
	}

	return (int)((scan << 8) | ascii);
}

int winterboot_console_poll_key(void)
{
	if (!availchar())
		return 0;

	return winterboot_key_ax_from_ascii(getchar());
}

void cls(void)
{
#ifdef CONFIG_DEBUG_CONSOLE_SERIAL
	serial_cls();
#endif
#ifdef CONFIG_DEBUG_CONSOLE_VGA
	video_cls();
#endif
}


#endif				// CONFIG_DEBUG_CONSOLE
