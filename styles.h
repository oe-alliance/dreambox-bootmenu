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

#define MENU_TOP	300	/* everything above belongs to the logo */

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


/* C: a wheel, the chosen entry large between two lines. */
static void draw_menu(struct screen *s, struct config *cfg, int selected,
		      int remaining)
{
	const int cx = 640, cy = 470;
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
		text(s, f, cx - text_width(f, t) / 2, cy + near[k].dy - f->height / 2, t,
		     RGB(g, g, g - 6), s->width);
	}
	hline(s, 360, 920, cy - 32, RGB(60, 68, 58));
	hline(s, 360, 920, cy + 50, RGB(60, 68, 58));
	w = text_width(&font_sel, cfg->entry[selected].title);
	glow_text(s, &font_sel, cx - w / 2, cy + 10 - font_sel.height / 2,
		  cfg->entry[selected].title, C_WHITE, C_GLOW, 140, s->width);

	if (cfg->timeout > 0) {
		int permille = 1000 * remaining / (cfg->total ? cfg->total : 1);

		ring(s, cx, 660, 18, 3, RGB(50, 56, 48), C_GLOW, permille);
		text_number(s, &font_tiny, cx - text_width(&font_tiny, remaining > 9 ? "00" : "0") / 2,
			    660 - font_tiny.height / 2, remaining, RGB(220, 224, 214));
	}
	flush(s, MENU_TOP, s->height - MENU_TOP);
}
