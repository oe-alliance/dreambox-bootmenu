/*
 * dreambox-bootmenu - https://github.com/oe-alliance/dreambox-bootmenu
 * Copyright (C) 2026 OE-Alliance
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Boot menu for the Dreambox One/Two, running as a u-boot standalone program.
 *
 * u-boot starts it with
 *
 *     go <load> <cfg> <cfglen> <result> [<fb> [<width> <height>]]
 *
 * where <cfg>/<cfglen> describe a bootconfig.txt already read into memory and
 * <result> is the address the chosen entry is written to.  The program never
 * boots anything itself: it writes the index and returns, the caller runs the
 * command.  Everything it touches is memory mapped, so no u-boot symbols are
 * needed.
 *
 * bootconfig.txt is read exactly as the built-in menu reads it, so it stays
 * usable as a fallback.  The keys of the Dreambox remote are built in.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef short s16;
typedef unsigned int u32;
typedef unsigned long u64;

#define UART_BASE	0xff803000UL	/* AO UART, the console of this board */
#define UART_WFIFO	0x00
#define UART_RFIFO	0x04
#define UART_STATUS	0x0c
#define UART_TX_FULL	(1u << 21)
#define UART_RX_EMPTY	(1u << 20)

#define AO_RTI_PIN_MUX	0xff800014UL	/* AO_5 selects IR in, see findings 4 */
#define AO_PIN_MUX_IR	0x00100000u
#define AO_PIN_MUX_MASK	0x00f00000u

#define IR_BASE		0xff808000UL
#define IR_PULSE	0x5c		/* time since the last edge, bits 28..16 */
#define IR_STATUS	0x58		/* toggles with every new pulse */

#define RC_TOGGLE	0x80u		/* set in the second frame of a press */

#define BOOTMENU_VERSION	"1.0"	/* a change here tags a release */

#define RESULT_MAGIC	0x4d454e55u	/* "MENU" */

#define MAX_ENTRIES	32
#define MAX_TITLE	64		/* bytes of UTF-8 */
#define MAX_EDGES	96

struct entry {
	char title[MAX_TITLE];
	const char *cmd;	/* points into bootconfig.txt, not copied */
	int cmdlen;
	const char *arg;
	int arglen;
};

struct config {
	struct entry entry[MAX_ENTRIES];
	int count;
	int def;
	int timeout;		/* seconds, 0 disables the countdown */
	int total;		/* timeout as configured, for the progress */
	int x, y;		/* fb_pos, 0 0 unless bootconfig.txt says so */
	int w, h;		/* fb_size, 0 means the rest of the screen */
	int font;		/* font_size in pixels, 0 picks one */
};

struct screen {
	volatile u16 *fb;
	int width;
	int height;
	int stride;		/* pixels per line */
};

static inline u32 readl(unsigned long addr)
{
	return *(volatile u32 *)addr;
}

static inline void writel(unsigned long addr, u32 value)
{
	*(volatile u32 *)addr = value;
}

/* The generic timer is the only clock available without u-boot's help. */
static u64 ticks(void)
{
	u64 value;

	asm volatile("isb; mrs %0, cntpct_el0" : "=r"(value));
	return value;
}

static u64 tick_rate(void)
{
	u64 value;

	asm volatile("mrs %0, cntfrq_el0" : "=r"(value));
	return value ? value : 24000000;
}

static void udelay(u32 usec)
{
	u64 end = ticks() + (tick_rate() / 1000000) * usec;

	while (ticks() < end)
		;
}

static void putc(char c)
{
	if (c == '\n')
		putc('\r');
	while (readl(UART_BASE + UART_STATUS) & UART_TX_FULL)
		;
	writel(UART_BASE + UART_WFIFO, (u8)c);
}

static void puts(const char *s)
{
	while (*s)
		putc(*s++);
}

static void puthex(u32 value)
{
	static const char digits[] = "0123456789abcdef";
	int i;

	puts("0x");
	for (i = 28; i >= 0; i -= 4)
		putc(digits[(value >> i) & 0xf]);
}

static void putdec(int value)
{
	char buf[12];
	int i = 0;

	if (value < 0) {
		putc('-');
		value = -value;
	}
	do {
		buf[i++] = '0' + value % 10;
		value /= 10;
	} while (value);
	while (i--)
		putc(buf[i]);
}

/* Returns -1 when no character is waiting. */
static int serial_getc(void)
{
	if (readl(UART_BASE + UART_STATUS) & UART_RX_EMPTY)
		return -1;
	return readl(UART_BASE + UART_RFIFO) & 0xff;
}

static int is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static u32 parse_number(const char *s, const char *end)
{
	u32 value = 0;
	int base = 10;

	while (s < end && is_space(*s))
		s++;
	if (s + 1 < end && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		s += 2;
	}
	while (s < end) {
		int digit;

		if (*s >= '0' && *s <= '9')
			digit = *s - '0';
		else if (*s >= 'a' && *s <= 'f')
			digit = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'F')
			digit = *s - 'A' + 10;
		else
			break;
		if (digit >= base)
			break;
		value = value * base + digit;
		s++;
	}
	return value;
}

/*
 * Arguments of "go" arrive as strings, and u-boot hands out numbers in hex
 * without a prefix -- ${filesize} is "1f4", not "0x1f4" -- so these are always
 * read as hex.
 */
static unsigned long parse_arg(const char *s)
{
	unsigned long value = 0;

	while (is_space(*s))
		s++;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	for (;;) {
		int digit;

		if (*s >= '0' && *s <= '9')
			digit = *s - '0';
		else if (*s >= 'a' && *s <= 'f')
			digit = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'F')
			digit = *s - 'A' + 10;
		else
			break;
		value = value * 16 + digit;
		s++;
	}
	return value;
}

/* Length of word when s starts with it, 0 otherwise. */
static int starts_with(const char *s, int len, const char *word)
{
	int i = 0;

	while (word[i]) {
		if (i >= len || s[i] != word[i])
			return 0;
		i++;
	}
	return i;
}

static int key_matches(const char *key, const char *name, const char *end)
{
	while (*key && name < end && *name && !is_space(*name) && *name != '=') {
		char a = *key, b = *name;

		if (a >= 'A' && a <= 'Z')
			a += 32;
		if (b >= 'A' && b <= 'Z')
			b += 32;
		if (a != b)
			return 0;
		key++;
		name++;
	}
	return *key == 0 && (name == end || *name == '=' || is_space(*name));
}

/* fb_pos and fb_size hold two numbers, separated by a comma or a space. */
static void parse_pair(const char *s, const char *end, int *first, int *second)
{
	*first = (int)parse_number(s, end);
	while (s < end && *s >= '0' && *s <= '9')
		s++;
	while (s < end && (*s < '0' || *s > '9'))
		s++;
	*second = (int)parse_number(s, end);
}

/*
 * bootconfig.txt as the DMM menu reads it: global keys first, then one
 * [Title] section per entry.  Only what the menu itself needs is parsed, the
 * cmd=/arg= lines stay with the caller.  Nothing is added to the file, so the
 * built-in menu keeps working as a fallback.
 */
static void parse_config(const char *buf, int len, struct config *cfg)
{
	const char *p = buf;
	const char *end = buf + len;

	if (len >= 3 && (u8)p[0] == 0xef && (u8)p[1] == 0xbb && (u8)p[2] == 0xbf)
		p += 3;		/* UTF-8 byte order mark */
	cfg->count = 0;
	cfg->def = 0;
	cfg->timeout = 5;
	cfg->x = 0;
	cfg->y = 0;
	cfg->w = 0;
	cfg->h = 0;
	cfg->font = 0;

	while (p < end) {
		const char *line = p;
		const char *stop;

		while (p < end && *p != '\n')
			p++;
		stop = p;
		if (p < end)
			p++;
		while (line < stop && is_space(*line))
			line++;
		while (stop > line && is_space(stop[-1]))
			stop--;
		if (line == stop || *line == '#' || *line == ';')
			continue;

		if (*line == '[') {
			const char *title = line + 1;
			const char *close = title;
			int i = 0;

			while (close < stop && *close != ']')
				close++;
			if (cfg->count >= MAX_ENTRIES)
				continue;
			while (title < close && i < MAX_TITLE - 1)
				cfg->entry[cfg->count].title[i++] = *title++;
			cfg->entry[cfg->count].title[i] = 0;
			cfg->entry[cfg->count].cmd = 0;
			cfg->entry[cfg->count].cmdlen = 0;
			cfg->entry[cfg->count].arg = 0;
			cfg->entry[cfg->count].arglen = 0;
			cfg->count++;
			continue;
		}

		{
			const char *value = line;

			while (value < stop && *value != '=')
				value++;
			if (value == stop)
				continue;
			value++;

			while (value < stop && is_space(*value))
				value++;

			if (cfg->count && key_matches("cmd", line, stop)) {
				cfg->entry[cfg->count - 1].cmd = value;
				cfg->entry[cfg->count - 1].cmdlen = (int)(stop - value);
			} else if (cfg->count && key_matches("arg", line, stop)) {
				cfg->entry[cfg->count - 1].arg = value;
				cfg->entry[cfg->count - 1].arglen = (int)(stop - value);
			} else if (key_matches("default", line, stop))
				cfg->def = (int)parse_number(value, stop);
			else if (key_matches("timeout", line, stop))
				cfg->timeout = (int)parse_number(value, stop);
			else if (key_matches("fb_pos", line, stop))
				parse_pair(value, stop, &cfg->x, &cfg->y);
			else if (key_matches("fb_size", line, stop))
				parse_pair(value, stop, &cfg->w, &cfg->h);
			else if (key_matches("font_size", line, stop))
				cfg->font = (int)parse_number(value, stop);
		}
	}

	if (cfg->def < 0 || cfg->def >= cfg->count)
		cfg->def = 0;
}

#include "gfx.h"
#include "fonts.h"
#include "fonts_bold.h"
#include "styles.h"

/*
 * The receiver only times the marks (IR_PULSE), the spaces that carry the
 * bits are not reported.  So the status register is used as an edge
 * detector -- it changes several times per edge -- and the edges are timed
 * with the CPU timer.  Linux's decoder setup is taken over, including the
 * enable bit in 0x5c without which the status stays still.
 */
static void ir_init(void)
{
	u32 mux = readl(AO_RTI_PIN_MUX);

	writel(AO_RTI_PIN_MUX, (mux & ~AO_PIN_MUX_MASK) | AO_PIN_MUX_IR);

	writel(IR_BASE + 0x40, 0x0046001e);
	writel(IR_BASE + 0x44, 0x0032000f);
	writel(IR_BASE + 0x48, 0x001e001a);
	writel(IR_BASE + 0x4c, 0x00420028);
	writel(IR_BASE + 0x50, 0x304e2013);
	writel(IR_BASE + 0x58, 0x05010900);
	writel(IR_BASE + 0x5c, 0x00009300);
	writel(IR_BASE + 0x60, 0x0000b90b);
	writel(IR_BASE + 0x64, 0x00610050);
	writel(IR_BASE + 0x68, 0x00780061);
	writel(IR_BASE + 0x74, 0x000fffff);
	writel(IR_BASE + 0x78, 0x00001388);
}

/*
 * Collects one frame as the lengths between edges, in microseconds.  Status
 * changes less than 100 us apart belong to the same edge.  *last carries the
 * status between calls, so an edge that arrives while the caller sleeps or
 * draws still starts the frame.
 */
static int ir_collect(u32 *last, u32 *len, int max)
{
	u64 rate = tick_rate();
	u64 merge = rate / 10000;	/* 100 us */
	u64 limit = rate / 100;		/* 10 ms without an edge ends a frame */
	u64 edge = 0, prev = 0;
	int count = 0, started = 0;

	for (;;) {
		u32 now = readl(IR_BASE + IR_STATUS);
		u64 t = ticks();

		if (now != *last) {
			*last = now;
			if (!started) {
				started = 1;
				edge = prev = t;
			} else if (t - prev > merge) {
				if (count < max)
					len[count++] = (u32)((t - edge) * 1000000 / rate);
				edge = t;
			}
			prev = t;
			continue;
		}
		if (!started || t - prev > limit)
			break;
	}
	return count;
}

/*
 * NEC: 9 ms mark, 4.5 ms space, then per bit a 560 us mark and a space of
 * 560 us for 0 or 1690 us for 1, least significant bit first.  A repeat
 * frame (9 ms, 2.25 ms, mark) is too short and gives 0.
 */
static u32 ir_decode(const u32 *len, int count)
{
	u32 code = 0;
	int i, start = -1, bits = 0;

	/* A repeat frame may run straight into the next press: take the last leader. */
	for (i = 0; i + 1 < count; i++)
		if (len[i] > 7000 && len[i + 1] > 3500)
			start = i;
	if (start < 0 || count - start < 2 + 2 * 32)
		return 0;

	for (i = start + 3; i < count && bits < 32; i += 2, bits++)
		if (len[i] > 1100)
			code |= 1u << bits;
	return bits == 32 ? code : 0;
}

enum action { ACT_NONE, ACT_UP, ACT_DOWN, ACT_OK, ACT_ABORT };

#define HOLD_DELAY_MS	400	/* held key starts to repeat after this */
#define HOLD_RATE_MS	80	/* then at most this often, the remote repeats every 108 ms */
#define PRESS_GAP_MS	250	/* frames further apart are separate presses */

struct remote {
	u32 last;		/* receiver status, see ir_collect() */
	enum action held;	/* action of the key being held */
	u64 seen;		/* last frame of the current press */
	u64 next;		/* when a held key moves again */
};

static u64 ms(u32 value)
{
	return tick_rate() / 1000 * value;
}

/* 9 ms mark, 2.25 ms space, mark: the key is still down. */
static int ir_is_repeat(const u32 *len, int count)
{
	return count >= 2 && count <= 4 && len[0] > 7000 && len[0] < 11000 &&
	       len[1] > 1700 && len[1] < 2800;
}

static void ir_report(const u32 *len, int count, u32 code)
{
	int i;

	puts("ir: unknown, ");
	putdec(count);
	puts(" edges, code ");
	puthex(code);
	puts(", us");
	for (i = 0; i < count && i < 8; i++) {
		putc(' ');
		putdec((int)len[i]);
	}
	puts(count > 8 ? " ...\n" : "\n");
}

/* Dreambox remotes, NEC with the address in the low byte. */
static const struct {
	u8 address;
	u8 up, down, ok;
} remotes[] = {
	{ 0x32, 0x11, 0x17, 0x14 },
	{ 0x34, 0x11, 0x19, 0x15 },
};

static enum action rc_action(u32 code)
{
	u32 address = code & 0xff;
	u32 command = (code >> 16) & 0xff & ~RC_TOGGLE;
	unsigned int i;

	if (((code ^ code >> 8) & 0xff) != 0xff ||
	    ((code >> 16 ^ code >> 24) & 0xff) != 0xff)
		return ACT_NONE;

	for (i = 0; i < sizeof(remotes) / sizeof(remotes[0]); i++) {
		if (remotes[i].address != address)
			continue;
		if (command == remotes[i].up)
			return ACT_UP;
		if (command == remotes[i].down)
			return ACT_DOWN;
		if (command == remotes[i].ok)
			return ACT_OK;
	}
	return ACT_NONE;
}

/*
 * A press sends the command twice, the second time with RC_TOGGLE set, with
 * repeat frames after each while the key is down.  The first frame of a
 * press acts at once; everything that follows only keeps the press alive,
 * and moves the bar again once the key has been held for HOLD_DELAY_MS.
 */
static enum action read_key(struct remote *rc)
{
	u32 len[MAX_EDGES];
	int count, c;
	u32 code, command;
	u64 now;
	enum action act;

	c = serial_getc();
	if (c >= 0) {
		if (c == 'u' || c == 'A')
			return ACT_UP;
		if (c == 'd' || c == 'B')
			return ACT_DOWN;
		if (c == '\r' || c == '\n' || c == ' ')
			return ACT_OK;
		if (c == 'q' || c == 0x03)
			return ACT_ABORT;
		return ACT_NONE;
	}

	count = ir_collect(&rc->last, len, MAX_EDGES);
	if (!count)
		return ACT_NONE;
	now = ticks();

	if (ir_is_repeat(len, count)) {
		if (rc->held == ACT_NONE || now - rc->seen > ms(PRESS_GAP_MS))
			return ACT_NONE;
		rc->seen = now;
	} else {
		code = ir_decode(len, count);
		act = rc_action(code);
		if (act == ACT_NONE) {
			ir_report(len, count, code);
			return ACT_NONE;
		}
		command = (code >> 16) & 0xff;
		if (!(command & RC_TOGGLE) || rc->held != act ||
		    now - rc->seen > ms(PRESS_GAP_MS)) {
			rc->held = act;
			rc->seen = now;
			rc->next = now + ms(HOLD_DELAY_MS);
			return act;
		}
		rc->seen = now;
	}

	if ((rc->held == ACT_UP || rc->held == ACT_DOWN) && now >= rc->next) {
		rc->next = now + ms(HOLD_RATE_MS);
		return rc->held;
	}
	return ACT_NONE;
}

static char *emit(char *p, const char *s)
{
	while (*s)
		*p++ = *s++;
	return p;
}

static char *emit_dec(char *p, int value)
{
	char buf[12];
	int i = 0;

	do {
		buf[i++] = '0' + value % 10;
		value /= 10;
	} while (value);
	while (i--)
		*p++ = buf[i];
	return p;
}

/*
 * The DMM menu passed arg= on unresolved, so the kernel saw a literal
 * "${bootargs}" in its command line.  It is dropped here; root= and console=
 * come from the header of kernel.img, so bm_arg is the whole of bootargs.
 */
static char *emit_value(char *p, const char *s, int len)
{
	int i = 0;

	while (i < len) {
		int n = starts_with(s + i, len - i, "${bootargs}");

		if (n) {
			i += n;
			while (i < len && s[i] == ' ')
				i++;
			continue;
		}
		if (s[i] == '\n' || s[i] == '\r' || s[i] == 0)
			break;
		*p++ = s[i++];
	}
	return p;
}

/*
 * The result is both an index and the entry as text, so a caller that has
 * "env import" need not know the entries at all:
 *
 *   +0x00  magic, written last
 *   +0x04  0, so a 64 bit read of +0x00 gives the magic alone
 *   +0x08  index of the chosen entry, counted from zero
 *   +0x0c  length of the text block including its terminator
 *   +0x10  bm_sel/bm_title/bm_cmd/bm_arg, one per line, NUL terminated
 */
static void write_result(unsigned long addr, struct config *cfg, int selected)
{
	struct entry *e = &cfg->entry[selected];
	char *text = (char *)(addr + 0x10);
	char *p = text;

	p = emit(p, "bm_sel=");
	p = emit_dec(p, selected);
	p = emit(p, "\nbm_title=");
	p = emit(p, e->title);
	p = emit(p, "\nbm_cmd=");
	p = emit_value(p, e->cmd, e->cmdlen);
	p = emit(p, "\nbm_arg=");
	p = emit_value(p, e->arg, e->arglen);
	*p++ = '\n';
	*p++ = 0;

	writel(addr + 4, 0);
	writel(addr + 8, (u32)selected);
	writel(addr + 12, (u32)(p - text));
	writel(addr, RESULT_MAGIC);
}

/* Linked first, at the load address, so "go" lands here. */
int menu_main(int argc, char * const argv[])
	__attribute__((section(".text.entry")));

int menu_main(int argc, char * const argv[])
{
	struct config cfg;
	struct screen screen;
	unsigned long cfg_addr, result_addr;
	u32 cfg_len;
	int selected, remaining, redraw = 1;
	u64 deadline, second;
	struct remote rc;

	/* "go" passes its own address as argv[0], the arguments follow. */
	if (argc < 4) {
		puts("menu: go <load> <cfg> <cfglen> <result> "
		     "[<fb> [<width> <height>]]\n");
		return 1;
	}

	cfg_addr = parse_arg(argv[1]);
	cfg_len = (u32)parse_arg(argv[2]);
	result_addr = parse_arg(argv[3]);

	/* The script passes 0x${initrd_high}, where u-boot put the logo. */
	screen.fb = (volatile u16 *)(argc > 4 ? parse_arg(argv[4]) : 0x7f800000UL);
	screen.width = argc > 6 ? (int)parse_arg(argv[5]) : 1280;
	screen.height = argc > 6 ? (int)parse_arg(argv[6]) : 720;
	screen.stride = screen.width;

	parse_config((const char *)cfg_addr, (int)cfg_len, &cfg);
	if (!cfg.count) {
		puts("menu: no entries in bootconfig.txt\n");
		return 1;	/* no magic, so the caller falls back */
	}

	cfg.total = cfg.timeout;
	ir_init();
	rc.last = readl(IR_BASE + IR_STATUS);
	rc.held = ACT_NONE;
	rc.seen = 0;
	rc.next = 0;
	/*
	 * A short look at the console before anything is drawn: should drawing
	 * ever crash, "q" here still gets the box past the menu.
	 */
	second = ticks() + ms(300);
	while (ticks() < second) {
		int c = serial_getc();

		if (c == 'q' || c == 0x03) {
			puts("menu: aborted\n");
			return 0;
		}
	}

	selected = cfg.def;
	remaining = cfg.timeout;
	second = ticks() + tick_rate();
	/* Counts timeout down to 0 inclusive, one second longer than set, as dm_bootmenu does. */
	deadline = ticks() + (u64)(cfg.timeout + 1) * tick_rate();

	puts("dreambox-bootmenu " BOOTMENU_VERSION " (C) OE-Alliance, GPL-3.0, "
	     "https://github.com/oe-alliance/dreambox-bootmenu\n");
	puts("menu " BOOTMENU_VERSION ": ");
	putdec(cfg.count);
	puts(" entries, default ");
	putdec(cfg.def + 1);
	puts(", timeout ");
	putdec(cfg.timeout);
	puts(" s\n");

	/* Like dm_bootmenu, timeout 0 shows no menu, but starts the default entry. */
	if (!cfg.timeout) {
		puts("menu: no menu, starting entry ");
		putdec(selected + 1);
		puts(" (");
		puts(cfg.entry[selected].title);
		puts(")\n");
		write_result(result_addr, &cfg, selected);
		return 0;
	}

	for (;;) {
		enum action act;

		if (redraw) {
			draw_menu(&screen, &cfg, selected, remaining);
			redraw = 0;
		}

		act = read_key(&rc);
		if (act == ACT_OK)
			break;
		if (act == ACT_ABORT) {
			/* No magic: the script ends and the built-in menu follows. */
			puts("menu: aborted\n");
			return 0;
		}
		if (act == ACT_UP) {
			selected = selected ? selected - 1 : cfg.count - 1;
			cfg.timeout = 0;
			redraw = 1;
		} else if (act == ACT_DOWN) {
			selected = selected + 1 < cfg.count ? selected + 1 : 0;
			cfg.timeout = 0;
			redraw = 1;
		}

		if (cfg.timeout > 0) {
			if (ticks() >= second) {
				second += tick_rate();
				if (remaining)
					remaining--;
				redraw = 1;
			}
			if (ticks() >= deadline)
				break;
		}
		udelay(200);
	}

	puts("menu: starting entry ");
	putdec(selected + 1);
	puts(" (");
	puts(cfg.entry[selected].title);
	puts(")\n");

	fade_out(&screen, 24, 40);
	write_result(result_addr, &cfg, selected);
	return 0;
}
