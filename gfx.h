/*
 * dreambox-bootmenu - https://github.com/oe-alliance/dreambox-bootmenu
 * Copyright (C) 2026 OE-Alliance
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Drawing on the RGB565 framebuffer: alpha blended text from prerendered
 * 4 bit glyphs, a glow around text, rounded panels and a countdown ring.
 * Integer only, the program is built without FP registers.
 */

struct glyph {
	u8 w, h;
	s16 xoff, yoff;		/* from the pen position and the top of the line */
	u8 adv;
	u32 off;		/* into the font's bits */
};

struct font {
	const struct glyph *g;
	const u8 *bits;
	const u16 *codes;	/* code point of each glyph, ascending */
	int ascent;
	int height;
	int count;
};

#define RGB(r, g, b)	((u32)(r) << 16 | (u32)(g) << 8 | (u32)(b))

/* Scratch for the glow: plain DRAM, clear of the program, the logo and the
 * secure monitor carve-out at 0x05000000. */
#define GLOW_BUF	0x20000000UL
#define GLOW_TMP	0x20400000UL

static inline u16 pack565(u32 r, u32 g, u32 b)
{
	return (u16)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

static void put(struct screen *s, int x, int y, u32 rgb)
{
	if (x < 0 || y < 0 || x >= s->width || y >= s->height)
		return;
	s->fb[(long)y * s->stride + x] = pack565(rgb >> 16 & 0xff, rgb >> 8 & 0xff, rgb & 0xff);
}

/* a: 0..255 */
static void blend(struct screen *s, int x, int y, u32 rgb, u32 a)
{
	volatile u16 *p;
	u32 d, dr, dg, db, r, g, b;

	if (!a || x < 0 || y < 0 || x >= s->width || y >= s->height)
		return;
	if (a >= 255) {
		put(s, x, y, rgb);
		return;
	}
	p = s->fb + (long)y * s->stride + x;
	d = *p;
	dr = (d >> 11 & 0x1f) << 3;
	dg = (d >> 5 & 0x3f) << 2;
	db = (d & 0x1f) << 3;
	r = rgb >> 16 & 0xff;
	g = rgb >> 8 & 0xff;
	b = rgb & 0xff;
	r = dr + ((int)(r - dr) * (int)a) / 255;
	g = dg + ((int)(g - dg) * (int)a) / 255;
	b = db + ((int)(b - db) * (int)a) / 255;
	*p = pack565(r, g, b);
}

/* Adds light, for the glow: d + c * a, saturating. */
static void lighten(struct screen *s, int x, int y, u32 rgb, u32 a)
{
	volatile u16 *p;
	u32 d, r, g, b;

	if (!a || x < 0 || y < 0 || x >= s->width || y >= s->height)
		return;
	p = s->fb + (long)y * s->stride + x;
	d = *p;
	r = ((d >> 11 & 0x1f) << 3) + (rgb >> 16 & 0xff) * a / 255;
	g = ((d >> 5 & 0x3f) << 2) + (rgb >> 8 & 0xff) * a / 255;
	b = ((d & 0x1f) << 3) + (rgb & 0xff) * a / 255;
	*p = pack565(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

static void fill(struct screen *s, int x, int y, int w, int h, u32 rgb)
{
	u16 c = pack565(rgb >> 16 & 0xff, rgb >> 8 & 0xff, rgb & 0xff);
	int row, col;

	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > s->width)
		w = s->width - x;
	if (y + h > s->height)
		h = s->height - y;
	for (row = y; row < y + h; row++) {
		volatile u16 *line = s->fb + (long)row * s->stride;

		for (col = x; col < x + w; col++)
			line[col] = c;
	}
}

static void hline(struct screen *s, int x0, int x1, int y, u32 rgb)
{
	fill(s, x0, y, x1 - x0, 1, rgb);
}

/*
 * u-boot may run with the data cache on, then the picture sits in the cache
 * and never reaches the display.  Clean the drawn lines to memory.
 */
static void flush(struct screen *s, int y, int h)
{
	unsigned long start, end, step;
	u64 ctr;

	if (y < 0) {
		h += y;
		y = 0;
	}
	if (y + h > s->height)
		h = s->height - y;
	asm volatile("mrs %0, ctr_el0" : "=r"(ctr));
	step = 4UL << ((ctr >> 16) & 0xf);
	start = (unsigned long)(s->fb + (long)y * s->stride) & ~(step - 1);
	end = (unsigned long)(s->fb + (long)(y + h) * s->stride);
	for (; start < end; start += step)
		asm volatile("dc cvac, %0" : : "r"(start) : "memory");
	asm volatile("dsb sy" : : : "memory");
}

/* Next code point of a UTF-8 string; a broken sequence gives one '?'. */
static u32 utf8_next(const char **s)
{
	const u8 *p = (const u8 *)*s;
	u32 c = *p++;
	int need = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : c >= 0x80 ? -1 : 0;

	if (need < 0) {
		c = '?';
		need = 0;
	} else if (need) {
		c &= 0x3f >> need;
	}
	while (need--) {
		if ((*p & 0xc0) != 0x80) {
			c = '?';
			break;
		}
		c = c << 6 | (*p++ & 0x3f);
	}
	*s = (const char *)p;
	return c;
}

static int glyph_index(const struct font *f, u32 c)
{
	int lo = 0, hi = f->count - 1;

	while (lo <= hi) {
		int mid = (lo + hi) / 2;

		if (f->codes[mid] == c)
			return mid;
		if (f->codes[mid] < c)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

/* Characters the font does not have are drawn as '?'. */
static const struct glyph *glyph_of(const struct font *f, u32 c)
{
	int i = glyph_index(f, c);

	return &f->g[i < 0 ? glyph_index(f, '?') : i];
}

static int text_width(const struct font *f, const char *t)
{
	int w = 0;

	while (*t)
		w += glyph_of(f, utf8_next(&t))->adv;
	return w;
}

/*
 * Calls out(x, y, alpha) for every covered pixel of the text, with the top
 * of the line at y.  Glyphs that would cross "right" are left out.
 */
#define FOR_TEXT_PIXELS(f, x0, y0, t, right, BODY)				\
	do {									\
		const char *t_ = (t);						\
		int pen_ = (x0);						\
		while (*t_) {							\
			const struct glyph *g_ = glyph_of((f), utf8_next(&t_));	\
			const u8 *b_ = (f)->bits + g_->off;			\
			int stride_ = (g_->w + 1) / 2, gx_, gy_;		\
			if (pen_ + g_->xoff + g_->w > (right))			\
				break;						\
			for (gy_ = 0; gy_ < g_->h; gy_++)			\
				for (gx_ = 0; gx_ < g_->w; gx_++) {		\
					u32 v_ = b_[gy_ * stride_ + gx_ / 2];	\
					u32 a_ = (gx_ & 1 ? v_ & 0xf : v_ >> 4) * 17;	\
					int px = pen_ + g_->xoff + gx_;		\
					int py = (y0) + g_->yoff + gy_;		\
					if (a_) { BODY; }			\
				}						\
			pen_ += g_->adv;					\
		}								\
	} while (0)

static void text(struct screen *s, const struct font *f, int x, int y,
		 const char *t, u32 rgb, int right)
{
	FOR_TEXT_PIXELS(f, x, y, t, right, blend(s, px, py, rgb, a_));
}

static void text_number(struct screen *s, const struct font *f, int x, int y,
			int n, u32 rgb)
{
	char buf[12];
	int i = 11;

	buf[i] = 0;
	do {
		buf[--i] = (char)('0' + n % 10);
		n /= 10;
	} while (n);
	text(s, f, x, y, buf + i, rgb, s->width);
}

/* Box blur, one direction, radius r, in place via tmp. */
static void blur_pass(u8 *buf, u8 *tmp, int w, int h, int r, int vertical)
{
	int n = vertical ? h : w, lines = vertical ? w : h;
	int step = vertical ? w : 1, next = vertical ? 1 : w;
	int i, j;

	for (i = 0; i < lines; i++) {
		u8 *p = buf + (long)i * next;
		int sum = 0;

		for (j = -r; j <= r; j++)
			if (j >= 0 && j < n)
				sum += p[(long)j * step];
		for (j = 0; j < n; j++) {
			tmp[j] = (u8)(sum / (2 * r + 1));
			if (j - r >= 0)
				sum -= p[(long)(j - r) * step];
			if (j + r + 1 < n)
				sum += p[(long)(j + r + 1) * step];
		}
		for (j = 0; j < n; j++)
			p[(long)j * step] = tmp[j];
	}
}

/*
 * The logo glows, so does the chosen entry: the text is drawn into an alpha
 * buffer, blurred three times (close to a gaussian) and added as light
 * before the text itself goes on top.
 */
static void glow_text(struct screen *s, const struct font *f, int x, int y,
		      const char *t, u32 rgb, u32 glow, u32 strength, int right)
{
	const int pad = 14;
	u8 *buf = (u8 *)GLOW_BUF, *tmp = (u8 *)GLOW_TMP;
	int w = text_width(f, t) + 2 * pad, h = f->height + 2 * pad;
	int i, j, k;

	for (i = 0; i < w * h; i++)
		buf[i] = 0;
	FOR_TEXT_PIXELS(f, pad, pad, t, w, if (px < w && py < h) buf[py * w + px] = (u8)a_);
	for (k = 0; k < 3; k++) {
		blur_pass(buf, tmp, w, h, 3, 0);
		blur_pass(buf, tmp, w, h, 3, 1);
	}
	for (j = 0; j < h; j++)
		for (i = 0; i < w; i++)
			lighten(s, x - pad + i, y - pad + j, glow, buf[j * w + i] * strength / 255);
	text(s, f, x, y, t, rgb, right);
}

static u32 isqrt(u32 v)
{
	u32 r = 0, bit = 1u << 30;

	while (bit > v)
		bit >>= 2;
	while (bit) {
		if (v >= r + bit) {
			v -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return r;
}

/* Coverage of a pixel at distance d16 (1/16 px) from an edge at r16. */
static u32 edge_alpha(int d16, int r16)
{
	int c = r16 - d16 + 8;

	return c <= 0 ? 0 : c >= 16 ? 255 : (u32)c * 255 / 16;
}

static void rounded_rect(struct screen *s, int x0, int y0, int x1, int y1,
			 int r, u32 fill_rgb, u32 border_rgb)
{
	int x, y;

	for (y = y0; y < y1; y++)
		for (x = x0; x < x1; x++) {
			int cx = x < x0 + r ? x0 + r : x >= x1 - r ? x1 - r - 1 : x;
			int cy = y < y0 + r ? y0 + r : y >= y1 - r ? y1 - r - 1 : y;
			int dx = x - cx, dy = y - cy;
			int d16 = (int)isqrt((u32)(dx * dx + dy * dy) * 256);
			u32 outer = edge_alpha(d16, r * 16);
			u32 inner = edge_alpha(d16, (r - 1) * 16);

			if (!outer)
				continue;
			blend(s, x, y, border_rgb, outer);
			blend(s, x, y, fill_rgb, inner);
		}
}

/*
 * Fades the whole picture to black: every step darkens what is there by an
 * eighth, the last one clears it.
 */
static void fade_out(struct screen *s, int steps, u32 step_ms)
{
	int i, n = s->stride * s->height, k;

	for (k = 0; k < steps; k++) {
		u64 end = ticks() + tick_rate() / 1000 * step_ms;

		for (i = 0; i < n; i++) {
			u32 d = s->fb[i];
			u32 r = (d >> 11 & 0x1f) * 7 / 8;
			u32 g = (d >> 5 & 0x3f) * 7 / 8;
			u32 b = (d & 0x1f) * 7 / 8;

			s->fb[i] = (u16)(r << 11 | g << 5 | b);
		}
		flush(s, 0, s->height);
		while (ticks() < end)
			;
	}
	fill(s, 0, 0, s->width, s->height, RGB(0, 0, 0));
	flush(s, 0, s->height);
}

/* Angle in 1/10 degree, clockwise from 12 o'clock. */
static int angle10(int dx, int dy)
{
	int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
	int a;

	/* atan(t) ~ 45*t - t*(t-1)*(14.0+3.83*t), t = min/max, in 1/1000 */
	if (!ax && !ay)
		return 0;
	{
		int t = ax < ay ? ax * 1000 / ay : ay * 1000 / ax;

		a = 450 * t / 1000 - t * (t - 1000) / 1000 * (140 + 38 * t / 1000) / 1000;
	}
	if (ax > ay)
		a = 900 - a;
	/* a: from the vertical axis, 0..900 */
	if (dx >= 0 && dy < 0)
		return a;
	if (dx >= 0)
		return 1800 - a;
	if (dy >= 0)
		return 1800 + a;
	return 3600 - a;
}

static void ring(struct screen *s, int cx, int cy, int r, int width,
		 u32 track, u32 done, int permille)
{
	int x, y;

	for (y = cy - r - width; y <= cy + r + width; y++)
		for (x = cx - r - width; x <= cx + r + width; x++) {
			int dx = x - cx, dy = y - cy;
			int d16 = (int)isqrt((u32)(dx * dx + dy * dy) * 256);
			u32 a = edge_alpha(d16, (r + width / 2) * 16);
			u32 in = edge_alpha(d16, (r - width / 2 - 1) * 16);

			a = a > in ? a - in : 0;
			if (a)
				blend(s, x, y, angle10(dx, dy) < permille * 36 / 10 ? done : track, a);
		}
}
