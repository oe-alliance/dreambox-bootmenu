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
#define C_BLACK		RGB(0, 0, 0)
#define C_WARN		RGB(235, 80, 70)
#define C_NOTE		RGB(235, 200, 80)

#define MENU_TOP	225	/* everything above belongs to the logo */
#define RING_Y		585	/* countdown or OK */

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

/* C: a wheel, the chosen entry large between two lines. */
static void draw_menu(struct screen *s, struct config *cfg, int selected,
		      int remaining, int src)
{
	static const char version[] = "v" BOOTMENU_VERSION;
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
			dot(s, cx - w / 2 - 16, cy + near[k].dy, 4, RGB(g, g + 20, g - 20));
			dot(s, cx + w / 2 + 16, cy + near[k].dy, 4, RGB(g, g + 20, g - 20));
		}
	}
	hline(s, 360, 920, cy - 32, RGB(60, 68, 58));
	hline(s, 360, 920, cy + 50, RGB(60, 68, 58));
	w = text_width(&font_sel, cfg->entry[selected].title);
	glow_text(s, &font_sel, cx - w / 2, cy + 10 - font_sel.height / 2,
		  cfg->entry[selected].title, C_WHITE, C_GLOW, 140, s->width);
	/* The default entry keeps a dot, wherever it is in the list. */
	if (selected == cfg->def) {
		dot(s, cx - w / 2 - 22, cy + 10, 6, C_GLOW);
		dot(s, cx + w / 2 + 22, cy + 10, 6, C_GLOW);
	}

	if (cfg->timeout > 0) {
		int permille = 1000 * remaining / (cfg->total ? cfg->total : 1);

		ring(s, cx, RING_Y, 18, 3, RGB(50, 56, 48), C_GLOW, permille);
		text_number(s, &font_tiny, cx - text_width(&font_tiny, remaining > 9 ? "00" : "0") / 2,
			    RING_Y - font_tiny.height / 2, remaining, RGB(220, 224, 214));
	} else {
		/* No countdown, or it was stopped: a full ring around "OK", waiting for it. */
		ring(s, cx, RING_Y, 18, 3, RGB(50, 56, 48), C_GLOW, 1000);
		text(s, &font_tiny, ink_left(&font_tiny, "OK", cx), RING_Y - font_tiny.height / 2,
		     "OK", RGB(220, 224, 214), s->width);
	}
	/* Where the last key came from: IR left of the ring, Bluetooth right of it. */
	if (src == SRC_IR)
		text(s, &font_micro, cx - 30 - text_width(&font_micro, "IR"), RING_Y - font_micro.height / 2,
		     "IR", C_GLOW, s->width);
	else if (src == SRC_BT)
		text(s, &font_micro, cx + 30, RING_Y - font_micro.height / 2, "BT", C_GLOW, s->width);
	if (cfg->source != CFG_FILE) {
		const char *note = cfg->source == CFG_BACKUP ?
			"bootconfig.txt is missing, these entries come from its backup" :
			"bootconfig.txt and its backup is missing, these are available fallback entries";

		text(s, &font_micro, cx - text_width(&font_micro, note) / 2, RING_Y + 70, note, C_WARN, s->width);
	} else if (!cfg->total) {
		/* timeout=0 shows no menu, only OK held at power-on brings it. */
		const char *note = "Menu forced by OK";

		text(s, &font_micro, cx - text_width(&font_micro, note) / 2, RING_Y + 70, note, C_NOTE, s->width);
	}
	text(s, &font_micro, ink_left(&font_micro, version, cx), RING_Y + 28, version,
	     RGB(90, 96, 86), s->width);
	flush(s, MENU_TOP, s->height - MENU_TOP);
}
