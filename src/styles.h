/*
 * dreambox-bootmenu - https://github.com/oe-alliance/dreambox-bootmenu
 * Copyright (C) 2026 OE-Alliance
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * The look of the menu, laid out for the 1280x720 logo with the Dreambox
 * logo moved up; colours come from the logo: off-white text and its
 * greenish glow on black.
 */

#define C_WHITE		RGB(255, 255, 255)
#define C_LOGO		RGB(240, 243, 236)
#define C_GLOW		RGB(190, 215, 160)
#define C_ACCENT	RGB(110, 230, 140)	/* ring and default dots */
#define C_BLACK		RGB(0, 0, 0)
#define C_WARN		RGB(235, 80, 70)
#define C_NOTE		RGB(235, 200, 80)

#define MENU_TOP	225	/* everything above belongs to the logo */
#define RING_Y		585	/* countdown or OK */
#define ABOUT_Y		(RING_Y + 34)	/* version line below the ring */
#define NOTE_Y		(RING_Y + 76)	/* yellow or red note below that */

static int first_row(int count, int selected, int rows)
{
	int first = selected - rows / 2;

	if (first > count - rows)
		first = count - rows;
	return first < 0 ? 0 : first;
}

static void slot_label(char *buf, int n, int count)
{
	char *p = buf;
	const char *w = "Slot ";
	char num[12];
	int i;

	while (*w)
		*p++ = *w++;
	for (i = 0; i < 2; i++) {
		int v = i ? count : n, k = 11;

		num[k] = 0;
		do {
			num[--k] = (char)('0' + v % 10);
			v /= 10;
		} while (v);
		for (w = num + k; *w; )
			*p++ = *w++;
		if (!i) {
			*p++ = ' ';
			*p++ = '/';
			*p++ = ' ';
		}
	}
	*p = 0;
}

static void boot_in(char *buf, int remaining)
{
	const char *a = "Boot in ";
	char *p = buf;
	char num[12];
	int k = 11;

	while (*a)
		*p++ = *a++;
	num[k] = 0;
	do {
		num[--k] = (char)('0' + remaining % 10);
		remaining /= 10;
	} while (remaining);
	for (a = num + k; *a; )
		*p++ = *a++;
	*p++ = ' ';
	*p++ = 's';
	*p = 0;
}

/* Left edge that centres t on cx by its ink, the advance leaves a gap on the right. */
static int ink_left(const struct font *f, const char *t, int cx)
{
	int pen = 0, lo = 0x7fff, hi = -0x7fff;

	while (*t) {
		const struct glyph *g = glyph_of(f, utf8_next(&t));

		if (g->w) {
			if (pen + g->xoff < lo)
				lo = pen + g->xoff;
			if (pen + g->xoff + g->w > hi)
				hi = pen + g->xoff + g->w;
		}
		pen += g->adv;
	}
	return lo > hi ? cx : cx - (lo + hi) / 2;
}

/* The version and how to reach the license page, below the ring. */
static void draw_about(struct screen *s, int cx, u32 color)
{
	static const char about[] = "v" BOOTMENU_VERSION " \xc2\xb7 INFO \xc2\xb7 License";

	text(s, &font_micro, ink_left(&font_micro, about, cx), ABOUT_Y, about, color, s->width);
}

/* INFO or HELP: what the menu is made of and under which licenses. */
static void draw_info(struct screen *s)
{
	static const char *const lines[] = {
		"Boot menu for the Dreambox One and Two",
		"Copyright (C) 2026 OE-Alliance",
		"GNU General Public License version 3",
		"Source: github.com/oe-alliance/dreambox-bootmenu",
		"Glyphs from the Roboto fonts by Google, Apache License 2.0",
		"The license texts are in /data/bootmenu",
	};
	static const char title[] = "dreambox-bootmenu v" BOOTMENU_VERSION;
	static const char hint[] = "Any key returns to the list";
	const int cx = 640;
	unsigned int i;

	fill(s, 0, MENU_TOP, s->width, s->height - MENU_TOP, C_BLACK);
	text(s, &font_head, cx - text_width(&font_head, title) / 2, MENU_TOP + 7, title,
	     RGB(214, 220, 206), s->width);
	for (i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
		text(s, &font_far, cx - text_width(&font_far, lines[i]) / 2, MENU_TOP + 80 + (int)i * 40,
		     lines[i], i < 1 ? C_LOGO : RGB(160, 166, 154), s->width);
	text(s, &font_micro, cx - text_width(&font_micro, hint) / 2, ABOUT_Y, hint,
	     RGB(90, 96, 86), s->width);
	flush(s, MENU_TOP, s->height - MENU_TOP);
}

/* Battery of the Bluetooth remote, its level in the middle: red up to 10 %, yellow up to 20 %. */
static void draw_battery(struct screen *s, int x, int cy, int level)
{
	const int w = 50, h = 24, t = 2;
	const u32 c = level <= 10 ? C_WARN : level <= 20 ? C_NOTE : RGB(220, 224, 214);
	const u32 dim = RGB((c >> 16 & 0xff) * 2 / 7, (c >> 8 & 0xff) * 2 / 7, (c & 0xff) * 2 / 7);
	const int y = cy - h / 2, inner = w - 4 * t;
	char buf[8], *p = buf + 7;
	int n = level;

	fill(s, x, y, w, t, c);
	fill(s, x, y + h - t, w, t, c);
	fill(s, x, y, t, h, c);
	fill(s, x + w - t, y, t, h, c);
	fill(s, x + w, cy - 5, 3, 10, c);
	fill(s, x + 2 * t, y + 2 * t, (inner * level + 50) / 100, h - 4 * t, dim);
	*p = 0;
	*--p = '%';
	do {
		*--p = (char)('0' + n % 10);
		n /= 10;
	} while (n);
	text(s, &font_micro, ink_left(&font_micro, p, x + w / 2), cy - 9, p, c, s->width);
}

/* Signal of the Bluetooth remote as four bars, one each from -90, -80, -70 and -60 dBm. */
static void draw_signal(struct screen *s, int x, int cy, int rssi)
{
	const int bars = rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : 1;
	int i;

	for (i = 0; i < 4; i++) {
		int h = 6 + 4 * i;

		fill(s, x + 6 * i, cy + 9 - h, 4, h, i < bars ? RGB(220, 224, 214) : RGB(60, 66, 58));
	}
}

/* C: a wheel, the chosen entry large between two lines. */
static void draw_menu(struct screen *s, struct config *cfg, int selected,
		      int remaining, int src, int battery, int rssi)
{
	const int cx = 640, cy = 395;
	static const struct { int off, dy; } near[] = {
		{ -2, -104 }, { -1, -52 }, { 1, 74 }, { 2, 126 },
	};
	char buf[24];
	unsigned int k;
	int w;

	fill(s, 0, MENU_TOP, s->width, s->height - MENU_TOP, C_BLACK);
	slot_label(buf, selected + 1, cfg->count);
	text(s, &font_head, cx - text_width(&font_head, buf) / 2, cy - 163, buf,
	     RGB(214, 220, 206), s->width);

	for (k = 0; k < sizeof(near) / sizeof(near[0]); k++) {
		int i = selected + near[k].off;
		const struct font *f = near[k].off == -2 || near[k].off == 2 ? &font_far : &font_list;
		u32 g = f == &font_far ? 70 : 130;
		const char *t;

		if (i < 0 || i >= cfg->count)
			continue;
		t = cfg->entry[i].title;
		w = text_width(f, t);
		text(s, f, cx - w / 2, cy + near[k].dy - f->height / 2, t,
		     RGB(g, g, g - 6), s->width);
		if (i == cfg->def) {
			dot(s, cx - w / 2 - 16, cy + near[k].dy, 4, RGB(g * 110 / 230, g, g * 140 / 230));
			dot(s, cx + w / 2 + 16, cy + near[k].dy, 4, RGB(g * 110 / 230, g, g * 140 / 230));
		}
	}
	hline(s, 360, 920, cy - 32, RGB(60, 68, 58));
	hline(s, 360, 920, cy + 50, RGB(60, 68, 58));
	w = text_width(&font_sel, cfg->entry[selected].title);
	glow_text(s, &font_sel, cx - w / 2, cy + 10 - font_sel.height / 2,
		  cfg->entry[selected].title, C_WHITE, C_GLOW, 140, s->width);
	/* The default entry keeps a dot, wherever it is in the list. */
	if (selected == cfg->def) {
		dot(s, cx - w / 2 - 22, cy + 10, 6, C_ACCENT);
		dot(s, cx + w / 2 + 22, cy + 10, 6, C_ACCENT);
	}

	if (cfg->timeout > 0) {
		int permille = 1000 * remaining / (cfg->total ? cfg->total : 1);

		ring(s, cx, RING_Y, 18, 3, RGB(50, 56, 48), C_ACCENT, permille);
		text_number(s, &font_tiny, cx - text_width(&font_tiny, remaining > 9 ? "00" : "0") / 2,
			    RING_Y - font_tiny.height / 2, remaining, RGB(220, 224, 214));
	} else {
		/* No countdown, or it was stopped: a full ring around "OK", waiting for it. */
		ring(s, cx, RING_Y, 18, 3, RGB(50, 56, 48), C_ACCENT, 1000);
		text(s, &font_tiny, ink_left(&font_tiny, "OK", cx), RING_Y - font_tiny.height / 2,
		     "OK", RGB(220, 224, 214), s->width);
	}
	/* Where the last key came from: IR left of the ring, Bluetooth right of it. */
	if (src == SRC_IR)
		text(s, &font_micro, cx - 30 - text_width(&font_micro, "IR"), RING_Y - font_micro.height / 2,
		     "IR", C_GLOW, s->width);
	else if (src == SRC_BT)
		text(s, &font_micro, cx + 30, RING_Y - font_micro.height / 2, "BT", C_GLOW, s->width);
	if (battery > 0)
		draw_battery(s, cx + 58, RING_Y, battery);
	if (rssi)
		draw_signal(s, cx + 122, RING_Y, rssi);
	if (cfg->source != CFG_FILE) {
		const char *note = cfg->source == CFG_BACKUP ?
			"bootconfig.txt is missing, these entries come from its backup" :
			"bootconfig.txt and its backup is missing, these are available fallback entries";

		text(s, &font_micro, cx - text_width(&font_micro, note) / 2, NOTE_Y, note, C_WARN, s->width);
	} else if (!cfg->total) {
		/* timeout=0 shows no menu, only OK held at power-on brings it. */
		const char *note = "Menu forced by OK";

		text(s, &font_micro, cx - text_width(&font_micro, note) / 2, NOTE_Y, note, C_NOTE, s->width);
	}
	draw_about(s, cx, RGB(90, 96, 86));
	flush(s, MENU_TOP, s->height - MENU_TOP);
}
