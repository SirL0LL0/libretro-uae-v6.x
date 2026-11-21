/*
 * PUAE libretro - host side of the RTG display (Picasso96 / uaegfx and
 * hardware graphics boards).
 *
 * Based on WinUAE od-unix/graphics.cpp (the RTG part). The emulation side
 * (uaegfx.card, P96 board functions) is retrodep/rtg.c, from od-unix/rtg.cpp.
 *
 * The RTG screen is rendered into its own 32-bit framebuffer (rtg_fb), at
 * the resolution the Amiga selected; it does not touch the chipset raster.
 * retro_run() presents it instead of the chipset picture while the Amiga
 * shows the RTG screen (retro_rtg_frame()).
 */

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef PICASSO96

#include "options.h"
#include "custom.h"
#include "xwin.h"
#include "drawing.h"
#include "memory.h"
#include "picasso96.h"
#include "rtg_overlay.h"
#include "gfxboard.h"
#include "threaddep/thread.h"

extern void picasso_trigger_vblank(void);
extern void unix_rtg_overlay_sprite(int monid, uae_u32 *dst, int width, int height, int rowpixels);

int p96syncrate = 312;
int p96refresh_active;
uae_u32 p96_rgbx16[65536];
float p96vblank = 60.0f;
struct picasso96_state_struct picasso96_state[MAX_AMIGAMONITORS];
struct picasso_vidbuf_description picasso_vidinfo[MAX_AMIGAMONITORS];

/* Largest RTG screen the frontend can show */
#define RTG_FB_MAX_WIDTH  1920
#define RTG_FB_MAX_HEIGHT 1200

/* misc.c: the RTG screen is shown */
int screen_is_picasso;

#ifndef WITH_X86
/* qemuvga/cirrus_vga.c: x86 bridgeboard VGA linear framebuffer (x86.c) */
void x86_map_lfb(int v)
{
}
#endif

static uae_u32 *rtg_fb;
static int rtg_fb_width, rtg_fb_height;
static bool rtg_fb_valid;

enum {
	RETRO_PICASSO_STATE_SETDISPLAY = 1,
	RETRO_PICASSO_STATE_SETPANNING = 2,
	RETRO_PICASSO_STATE_SETGC = 4,
	RETRO_PICASSO_STATE_SETDAC = 8,
	RETRO_PICASSO_STATE_SETSWITCH = 16,
	RETRO_PICASSO_STATE_SPRITE = 32
};

static int retro_picasso_bytes_per_pixel(RGBFTYPE rgbfmt)
{
	switch (rgbfmt) {
	case RGBFB_CLUT:
	case RGBFB_Y4U1V1:
		return 1;
	case RGBFB_R5G6B5:
	case RGBFB_R5G5B5:
	case RGBFB_R5G6B5PC:
	case RGBFB_R5G5B5PC:
	case RGBFB_B5G6R5PC:
	case RGBFB_B5G5R5PC:
	case RGBFB_Y4U2V2:
		return 2;
	case RGBFB_R8G8B8:
	case RGBFB_B8G8R8:
		return 3;
	case RGBFB_A8R8G8B8:
	case RGBFB_A8B8G8R8:
	case RGBFB_R8G8B8A8:
	case RGBFB_B8G8R8A8:
		return 4;
	default:
		return 0;
	}
}

static uae_u16 retro_picasso_load_host_u16(const uae_u8 *src)
{
	uae_u16 value;
	memcpy(&value, src, sizeof value);
	return value;
}

static bool retro_rtg_fb_alloc(void)
{
	if (!rtg_fb)
		rtg_fb = xcalloc(uae_u32, RTG_FB_MAX_WIDTH * RTG_FB_MAX_HEIGHT);
	return rtg_fb != NULL;
}

void InitPicasso96(int monid)
{
	if (monid < 0 || monid >= MAX_AMIGAMONITORS)
		return;

	memset(&picasso96_state[monid], 0, sizeof picasso96_state[monid]);
	memset(&picasso_vidinfo[monid], 0, sizeof picasso_vidinfo[monid]);
	picasso_vidinfo[monid].splitypos = -1;
	picasso_vidinfo[monid].rgbformat = RGBFB_B8G8R8A8;
	picasso_vidinfo[monid].selected_rgbformat = RGBFB_B8G8R8A8;
	picasso_vidinfo[monid].host_mode = RGBFB_B8G8R8A8;
	picasso_vidinfo[monid].pixbytes = 4;
	for (int i = 0; i < 256; i++) {
		picasso96_state[monid].CLUT[i].Red = i;
		picasso96_state[monid].CLUT[i].Green = i;
		picasso96_state[monid].CLUT[i].Blue = i;
		picasso_vidinfo[monid].clut[i] = 0xff000000 | (i << 16) | (i << 8) | i;
	}
	if (monid == 0)
		rtg_fb_valid = false;
	/* 16-bit pixel formats to host 32-bit */
	alloc_colors_picasso(8, 8, 8, 16, 8, 0, RGBFB_R8G8B8A8, p96_rgbx16);
}

/* Size of the RTG screen; describes rtg_fb to gfxboard.c and the uaegfx code */
static bool retro_picasso_ensure_buffer(int monid)
{
	struct picasso96_state_struct *state = &picasso96_state[monid];
	struct picasso_vidbuf_description *pvidinfo = &picasso_vidinfo[monid];
	int width = state->Width > 0 ? state->Width : 640;
	int height = state->Height > 0 ? state->Height : 480;

	if (monid != 0 || !retro_rtg_fb_alloc())
		return false;
	if (width > RTG_FB_MAX_WIDTH)
		width = RTG_FB_MAX_WIDTH;
	if (height > RTG_FB_MAX_HEIGHT)
		height = RTG_FB_MAX_HEIGHT;

	pvidinfo->width = width;
	pvidinfo->height = height;
	pvidinfo->depth = state->GC_Depth ? (state->GC_Depth + 7) / 8 : 0;
	pvidinfo->pixbytes = 4;
	pvidinfo->rowbytes = RTG_FB_MAX_WIDTH * 4;
	pvidinfo->maxwidth = RTG_FB_MAX_WIDTH;
	pvidinfo->maxheight = RTG_FB_MAX_HEIGHT;
	pvidinfo->rgbformat = state->RGBFormat;
	pvidinfo->selected_rgbformat = state->RGBFormat;
	pvidinfo->host_mode = RGBFB_B8G8R8A8;
	return true;
}

static uae_u32 retro_picasso_convert_pixel(const uae_u8 *src, RGBFTYPE fmt,
	const uae_u32 *clut, const uae_u32 *rgbx16)
{
	switch (fmt) {
	case RGBFB_CLUT:
		return clut[src[0]];
	case RGBFB_R8G8B8:
		return 0xff000000 | ((uae_u32)src[0] << 16) | ((uae_u32)src[1] << 8) | src[2];
	case RGBFB_B8G8R8:
		return 0xff000000 | ((uae_u32)src[2] << 16) | ((uae_u32)src[1] << 8) | src[0];
	case RGBFB_R8G8B8A8:
		return ((uae_u32)src[3] << 24) | ((uae_u32)src[0] << 16) | ((uae_u32)src[1] << 8) | src[2];
	case RGBFB_B8G8R8A8:
		return ((uae_u32)src[3] << 24) | ((uae_u32)src[2] << 16) | ((uae_u32)src[1] << 8) | src[0];
	case RGBFB_A8R8G8B8:
		return ((uae_u32)src[0] << 24) | ((uae_u32)src[1] << 16) | ((uae_u32)src[2] << 8) | src[3];
	case RGBFB_A8B8G8R8:
		return ((uae_u32)src[0] << 24) | ((uae_u32)src[3] << 16) | ((uae_u32)src[2] << 8) | src[1];
	case RGBFB_R5G6B5:
	case RGBFB_R5G5B5:
	case RGBFB_R5G6B5PC:
	case RGBFB_R5G5B5PC:
	case RGBFB_B5G6R5PC:
	case RGBFB_B5G5R5PC:
		return rgbx16[retro_picasso_load_host_u16(src)];
	default:
		return 0xff000000;
	}
}

static void retro_picasso_render_pixels(const uae_u8 *srcbase, int width, int height,
	int srcrowbytes, int srcpixbytes, RGBFTYPE rgbfmt, const uae_u32 *clut,
	uae_u8 *dstbase, int dstrowbytes, const uae_u32 *rgbx16)
{
	for (int y = 0; y < height; y++) {
		const uae_u8 *src = srcbase + y * srcrowbytes;
		uae_u32 *dst = (uae_u32 *)(dstbase + y * dstrowbytes);
		for (int x = 0; x < width; x++)
			dst[x] = retro_picasso_convert_pixel(src + x * srcpixbytes, rgbfmt, clut, rgbx16);
	}
}

static uae_u32 retro_picasso_convert_scaled_pixel(const uae_u8 *src, int x, int sxfrac,
	int convert_mode, const uae_u32 *rgbx16, const uae_u32 *clut, bool yuv_swap)
{
	switch (convert_mode) {
	case RGBFB_R8G8B8_32:
		return 0xff000000 | ((uae_u32)src[x * 3 + 0] << 16) |
			((uae_u32)src[x * 3 + 1] << 8) | src[x * 3 + 2];
	case RGBFB_B8G8R8_32:
		return 0xff000000 | ((uae_u32)src[x * 3 + 2] << 16) |
			((uae_u32)src[x * 3 + 1] << 8) | src[x * 3 + 0];
	case RGBFB_R8G8B8A8_32:
		return 0xff000000 | ((uae_u32)src[x * 4 + 0] << 16) |
			((uae_u32)src[x * 4 + 1] << 8) | src[x * 4 + 2];
	case RGBFB_A8R8G8B8_32:
		return ((uae_u32)src[x * 4 + 0] << 24) | ((uae_u32)src[x * 4 + 1] << 16) |
			((uae_u32)src[x * 4 + 2] << 8) | src[x * 4 + 3];
	case RGBFB_A8B8G8R8_32:
		return ((uae_u32)src[x * 4 + 0] << 24) | ((uae_u32)src[x * 4 + 3] << 16) |
			((uae_u32)src[x * 4 + 2] << 8) | src[x * 4 + 1];
	case RGBFB_B8G8R8A8_32:
		return ((uae_u32)src[x * 4 + 3] << 24) | ((uae_u32)src[x * 4 + 2] << 16) |
			((uae_u32)src[x * 4 + 1] << 8) | src[x * 4 + 0];
	case RGBFB_R5G6B5PC_32:
	case RGBFB_R5G5B5PC_32:
	case RGBFB_R5G6B5_32:
	case RGBFB_R5G5B5_32:
	case RGBFB_B5G6R5PC_32:
	case RGBFB_B5G5R5PC_32:
		return rgbx16 ? rgbx16[retro_picasso_load_host_u16(src + x * 2)] : 0xff000000;
	case RGBFB_CLUT_RGBFB_32:
		return clut ? clut[src[x]] : (0xff000000 | src[x] | ((uae_u32)src[x] << 8) |
			((uae_u32)src[x] << 16));
	case RGBFB_Y4U2V2_32:
	{
		/* P96 memory windows use the Windows driver's packed word order */
		const uae_u8 *packet = src + x * 4;
		uae_u32 val = packet[0] | ((uae_u32)packet[1] << 8) |
			((uae_u32)packet[2] << 16) | ((uae_u32)packet[3] << 24);
		if (yuv_swap)
			val = ((val & 0xff00ff00) >> 8) | ((val & 0x00ff00ff) << 8);
		uae_u8 y = (sxfrac & 0x80) ? (uae_u8)(val >> 24) : (uae_u8)(val >> 8);
		uae_u8 u = (uae_u8)val;
		uae_u8 v = (uae_u8)(val >> 16);
		uae_u16 rgb = p96_yuvtorgb(y, u, v);
		return rgbx16 ? rgbx16[rgb] : 0xff000000;
	}
	case RGBFB_Y4U1V1_32:
	{
		const uae_u8 *packet = src + x * 4;
		uae_u32 val = packet[0] | ((uae_u32)packet[1] << 8) |
			((uae_u32)packet[2] << 16) | ((uae_u32)packet[3] << 24);
		/* ACCUPAK: four 5-bit luma samples and two 6-bit chroma samples */
		int sample = (sxfrac >> 6) & 3;
		uae_u8 y = ((val >> (12 + sample * 5)) & 31) * 8;
		uae_u8 u = (val & 63) * 4;
		uae_u8 v = ((val >> 6) & 63) * 4;
		uae_u16 rgb = p96_yuvtorgb(y, u, v);
		return rgbx16 ? rgbx16[rgb] : 0xff000000;
	}
	default:
		return 0xff000000;
	}
}

static bool retro_picasso_colorkey_matches(const uae_u8 *screen, int dx, int screenpixbytes,
	uae_u32 colorkey)
{
	if (!screen || dx < 0 || screenpixbytes <= 0)
		return false;
	switch (screenpixbytes) {
	case 1:
		return screen[dx] == (uae_u8)colorkey;
	case 2:
		return do_get_mem_word((uae_u16 *)(screen + dx * 2)) == (uae_u16)colorkey;
	case 3:
		return screen[dx * 3 + 0] == (uae_u8)(colorkey >> 16) &&
			screen[dx * 3 + 1] == (uae_u8)(colorkey >> 8) &&
			screen[dx * 3 + 2] == (uae_u8)colorkey;
	case 4:
		return do_get_mem_long((uae_u32 *)(screen + dx * 4)) == colorkey;
	default:
		return false;
	}
}

static void retro_picasso_store_scaled_pixel(uae_u8 *dst, int dx, int dstpixbytes, uae_u32 color)
{
	switch (dstpixbytes) {
	case 1:
		dst[dx] = (uae_u8)color;
		break;
	case 2:
		do_put_mem_word((uae_u16 *)(dst + dx * 2), (uae_u16)color);
		break;
	case 3:
		dst[dx * 3 + 0] = (uae_u8)(color >> 16);
		dst[dx * 3 + 1] = (uae_u8)(color >> 8);
		dst[dx * 3 + 2] = (uae_u8)color;
		break;
	case 4:
		do_put_mem_long((uae_u32 *)(dst + dx * 4), color);
		break;
	}
}

/* Used by gfxboard.c (hardware boards with a video overlay) */
void copyrow_scale(int monid, uae_u8 *src, uae_u8 *src_screen, uae_u8 *dst,
	int sx, int sy, int sxadd, int width, int srcbytesperrow, int srcpixbytes,
	int screenbytesperrow, int screenpixbytes,
	int dx, int dy, int dstwidth, int dstheight, int dstbytesperrow, int dstpixbytes,
	bool ck, uae_u32 colorkey, int convert_mode, uae_u32 *p96_rgbx16p,
	uae_u32 *clut, bool yuv_swap)
{
	uae_u8 *srcrow, *dstrow, *screenrow;
	int endx;

	if (!src || !dst || sxadd <= 0 || width <= 0 || dx < 0 || dy < 0 ||
		dx >= dstwidth || dy >= dstheight || dstpixbytes <= 0)
		return;

	srcrow = src + sy * srcbytesperrow;
	dstrow = dst + dy * dstbytesperrow;
	screenrow = src_screen ? src_screen + dy * screenbytesperrow : NULL;
	endx = (sx + width) << 8;

	if (convert_mode == RGBFB_Y4U2V2_32) {
		endx /= 2;
		sxadd /= 2;
	} else if (convert_mode == RGBFB_Y4U1V1_32) {
		endx /= 4;
		sxadd /= 4;
	}

	while (sx < endx && dx < dstwidth) {
		int x = sx >> 8;
		if (!ck || retro_picasso_colorkey_matches(screenrow, dx, screenpixbytes, colorkey)) {
			uae_u32 color = retro_picasso_convert_scaled_pixel(srcrow, x, sx & 255,
				convert_mode, p96_rgbx16p, clut, yuv_swap);
			retro_picasso_store_scaled_pixel(dstrow, dx, dstpixbytes, color);
		}
		sx += sxadd;
		dx++;
	}
}

/* Screenshot of the RTG screen (savestate thumbnails, debugger) */
uae_u8 *uaegfx_getrtgbuffer(int monid, int *widthp, int *heightp, int *pitch, int *depth,
	uae_u8 *palette)
{
	if (monid < 0 || monid >= MAX_AMIGAMONITORS)
		monid = 0;
	int bankid = monid < MAX_RTG_BOARDS && gfxmem_banks[monid] ? monid : 0;
	addrbank *bank = gfxmem_banks[bankid];
	struct picasso96_state_struct *state = &picasso96_state[monid];
	struct picasso_vidbuf_description *pvidinfo = &picasso_vidinfo[monid];
	int width = state->VirtualWidth ? state->VirtualWidth : state->Width;
	int height = state->VirtualHeight ? state->VirtualHeight : state->Height;
	int srcpixbytes = retro_picasso_bytes_per_pixel((RGBFTYPE)state->RGBFormat);
	int dstpixbytes = state->BytesPerPixel == 1 && palette ? 1 : 4;
	uae_u32 offset, needed;
	uae_u8 *dst;

	if (!bank || !bank->baseaddr || width <= 0 || height <= 0 || srcpixbytes <= 0 ||
		state->BytesPerRow <= 0 || (uae_u32)state->XYOffset < bank->start)
		return NULL;
	offset = (uae_u32)state->XYOffset - bank->start;
	needed = offset + (height - 1) * state->BytesPerRow + width * srcpixbytes;
	if (needed > bank->allocated_size)
		return NULL;

	dst = xmalloc(uae_u8, (size_t)width * (size_t)height * (size_t)dstpixbytes);
	if (!dst)
		return NULL;
	if (dstpixbytes == 1) {
		for (int y = 0; y < height; y++)
			memcpy(dst + (size_t)y * (size_t)width,
				bank->baseaddr + offset + (size_t)y * (size_t)state->BytesPerRow,
				(size_t)width);
		for (int i = 0; i < 256; i++) {
			palette[i * 3 + 0] = state->CLUT[i].Red;
			palette[i * 3 + 1] = state->CLUT[i].Green;
			palette[i * 3 + 2] = state->CLUT[i].Blue;
		}
	} else {
		alloc_colors_picasso(8, 8, 8, 16, 8, 0, (RGBFTYPE)state->RGBFormat, p96_rgbx16);
		retro_picasso_render_pixels(bank->baseaddr + offset, width, height, state->BytesPerRow,
			srcpixbytes, (RGBFTYPE)state->RGBFormat, pvidinfo->clut, dst, width * dstpixbytes,
			p96_rgbx16);
	}

	*widthp = width;
	*heightp = height;
	*pitch = width * dstpixbytes;
	*depth = dstpixbytes * 8;
	return dst;
}

void uaegfx_freertgbuffer(int monid, uae_u8 *dst)
{
	xfree(dst);
}

/* The lower screen of a split screen starts at VRAM base */
static bool retro_picasso_scanout_offset(uae_u32 pan, int split, int y,
	int pitch, int rowbytes, uae_u32 size, uae_u32 *offset)
{
	uae_u64 start = pan;

	if (y < 0 || pitch <= 0 || rowbytes <= 0)
		return false;
	if (split >= 0 && y >= split) {
		start = 0;
		y -= split;
	}
	start += (uae_u64)y * pitch;
	if (start + rowbytes > size)
		return false;
	*offset = (uae_u32)start;
	return true;
}

/* uaegfx: convert the visible part of the board memory into rtg_fb */
static void retro_picasso_render(int monid)
{
	struct picasso96_state_struct *state = &picasso96_state[monid];
	struct picasso_vidbuf_description *pvidinfo = &picasso_vidinfo[monid];
	addrbank *bank = gfxmem_banks[0];
	int srcpixbytes, width, height;
	uae_u32 offset;
	uae_u64 needed;

	if (monid != 0 || !retro_picasso_ensure_buffer(monid))
		return;

	srcpixbytes = retro_picasso_bytes_per_pixel((RGBFTYPE)state->RGBFormat);
	if (!bank || !bank->baseaddr || !srcpixbytes || !state->Address || !state->BytesPerRow ||
		!state->Width || !state->Height)
		return;
	if ((uae_u32)state->XYOffset < bank->start)
		return;

	width = pvidinfo->width;
	height = pvidinfo->height;
	offset = (uae_u32)state->XYOffset - bank->start;
	needed = (uae_u64)offset + (height - 1) * (uae_u64)state->BytesPerRow + width * srcpixbytes;
	if (pvidinfo->splitypos < 0 && needed > bank->allocated_size)
		return;

	alloc_colors_picasso(8, 8, 8, 16, 8, 0, (RGBFTYPE)state->RGBFormat, p96_rgbx16);

	if (pvidinfo->splitypos >= 0) {
		RGBFTYPE format = (RGBFTYPE)state->RGBFormat;
		for (int y = 0; y < height; y++) {
			bool lower = y >= pvidinfo->splitypos;
			RGBFTYPE rowformat = (RGBFTYPE)pvidinfo->dacrgbformat[lower ? 1 : 0];
			uae_u8 *dst = (uae_u8 *)(rtg_fb + y * RTG_FB_MAX_WIDTH);
			uae_u32 rowoffset;
			int bpp, pitch;

			if (!state->advDragging || !retro_picasso_bytes_per_pixel(rowformat))
				rowformat = (RGBFTYPE)state->RGBFormat;
			if (format != rowformat) {
				format = rowformat;
				alloc_colors_picasso(8, 8, 8, 16, 8, 0, format, p96_rgbx16);
			}
			bpp = retro_picasso_bytes_per_pixel(format);
			pitch = state->BytesPerRow * bpp / srcpixbytes;
			if (retro_picasso_scanout_offset(offset, pvidinfo->splitypos, y,
				pitch, width * bpp, bank->allocated_size, &rowoffset))
				retro_picasso_render_pixels(bank->baseaddr + rowoffset, width, 1, pitch, bpp,
					format, pvidinfo->clut, dst, RTG_FB_MAX_WIDTH * 4, p96_rgbx16);
			else
				memset(dst, 0, width * sizeof(uae_u32));
		}
	} else {
		retro_picasso_render_pixels(bank->baseaddr + offset, width, height, state->BytesPerRow,
			srcpixbytes, (RGBFTYPE)state->RGBFormat, pvidinfo->clut,
			(uae_u8 *)rtg_fb, RTG_FB_MAX_WIDTH * 4, p96_rgbx16);
	}
	/* Software mouse pointer (P96 hardware sprite) */
	unix_rtg_overlay_sprite(monid, rtg_fb, width, height, RTG_FB_MAX_WIDTH);

	rtg_fb_width = width;
	rtg_fb_height = height;
	rtg_fb_valid = true;
}

bool picasso_is_active(int monid)
{
	if (monid < 0 || monid >= MAX_AMIGAMONITORS)
		return false;
	return picasso_vidinfo[monid].picasso_active;
}

void picasso_enablescreen(int monid, int on)
{
	if (monid < 0 || monid >= MAX_AMIGAMONITORS)
		return;
	picasso_vidinfo[monid].picasso_active = on != 0;
	if (on)
		picasso_refresh(monid);
}

void picasso_refresh(int monid)
{
	if (monid < 0 || monid >= MAX_AMIGAMONITORS)
		return;
	if (currprefs.rtgboards[0].rtgmem_type >= GFXBOARD_HARDWARE) {
		gfxboard_refresh(monid);
		return;
	}
	retro_picasso_render(monid);
}

void init_hz_p96(int monid)
{
	if (currprefs.host_rtgvblankrate <= 0)
		p96vblank = vblank_hz;
	else
		p96vblank = (float)currprefs.host_rtgvblankrate;
	if (p96vblank <= 0)
		p96vblank = 60;
	if (p96vblank >= 300)
		p96vblank = 300;
	p96syncrate = (int)(maxvpos_nom * vblank_hz / p96vblank);
	write_log(_T("RTGFREQ: %d*%.4f = %.4f / %.1f = %d\n"), maxvpos_nom,
		vblank_hz, maxvpos_nom * vblank_hz, p96vblank, p96syncrate);
}

void gfx_set_picasso_modeinfo(int monid, RGBFTYPE rgbfmt)
{
	struct picasso96_state_struct *state;

	if (monid < 0 || monid >= MAX_AMIGAMONITORS)
		return;
	state = &picasso96_state[monid];
	picasso_vidinfo[monid].rgbformat = rgbfmt;
	picasso_vidinfo[monid].selected_rgbformat = rgbfmt;
	picasso_vidinfo[monid].depth = state->GC_Depth ? (state->GC_Depth + 7) / 8 : 0;
	picasso_vidinfo[monid].picasso_changed = true;
	retro_picasso_ensure_buffer(monid);
}

void gfx_set_picasso_colors(int monid, RGBFTYPE rgbfmt)
{
	if (monid >= 0 && monid < MAX_AMIGAMONITORS)
		picasso_vidinfo[monid].rgbformat = rgbfmt;
}

void gfx_set_picasso_state(int monid, int on)
{
	if (monid >= 0 && monid < MAX_AMIGAMONITORS)
		picasso_vidinfo[monid].picasso_active = on != 0;
}

/* Hardware boards (gfxboard.c) draw straight into rtg_fb */
uae_u8 *gfx_lock_picasso(int monid, bool fullupdate)
{
	if (monid != 0 || !retro_picasso_ensure_buffer(monid))
		return NULL;
	return (uae_u8 *)rtg_fb;
}

void gfx_unlock_picasso(int monid, bool dorender)
{
	struct picasso_vidbuf_description *pvidinfo = &picasso_vidinfo[0];

	if (monid != 0)
		return;
	if (dorender) {
		rtg_fb_width = pvidinfo->width;
		rtg_fb_height = pvidinfo->height;
		rtg_fb_valid = true;
	}
}

void picasso_invalidate(int monid, int x, int y, int w, int h)
{
}

void picasso_handle_vsync(void)
{
	for (int monid = 0; monid < MAX_AMIGAMONITORS; monid++) {
		struct amigadisplay *ad = &adisplays[monid];
		struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
		int state = vidinfo->picasso_state_change;
		bool uaegfx = currprefs.rtgboards[0].rtgmem_type < GFXBOARD_HARDWARE;

		if (state) {
			atomic_and(&vidinfo->picasso_state_change, ~state);
			if (state & RETRO_PICASSO_STATE_SETGC)
				gfx_set_picasso_modeinfo(monid, (RGBFTYPE)picasso96_state[monid].RGBFormat);
			if (state & RETRO_PICASSO_STATE_SETSWITCH)
				vidinfo->picasso_active = ad->picasso_requested_on;
			if (state & (RETRO_PICASSO_STATE_SETGC | RETRO_PICASSO_STATE_SETPANNING |
				RETRO_PICASSO_STATE_SETDAC | RETRO_PICASSO_STATE_SETDISPLAY))
				vidinfo->full_refresh = 1;
		}
		if (!uaegfx)
			continue;

		/* P96 waits for the board vblank before it switches to a new
		 * screen, so it must run while the native display is shown too */
		if (monid == currprefs.rtgboards[0].monitor_id)
			picasso_trigger_vblank();
		if (ad->picasso_on || vidinfo->picasso_active)
			picasso_refresh(monid);
	}
	gfxboard_vsync_handler(false, true);
}

void fb_copyrow(int monid, uae_u8 *src, uae_u8 *dst, int x, int y, int width, int srcpixbytes, int dy)
{
	int rowbytes, pixbytes;

	if (!src || !dst || monid < 0 || monid >= MAX_AMIGAMONITORS)
		return;
	rowbytes = picasso_vidinfo[monid].rowbytes;
	pixbytes = picasso_vidinfo[monid].pixbytes ? picasso_vidinfo[monid].pixbytes : srcpixbytes;
	if (rowbytes <= 0 || pixbytes <= 0 || width <= 0)
		return;
	memcpy(dst + dy * rowbytes + x * pixbytes, src, (size_t)width * (size_t)srcpixbytes);
}

/* Frontend: the RTG frame to show instead of the chipset picture, or false
 * while the Amiga shows its native display */
bool retro_rtg_frame(const uae_u32 **buf, int *width, int *height, int *pitch)
{
	screen_is_picasso = adisplays[0].picasso_on;
	if (!adisplays[0].picasso_on || !rtg_fb_valid || !rtg_fb
		|| rtg_fb_width <= 0 || rtg_fb_height <= 0)
		return false;
	*buf = rtg_fb;
	*width = rtg_fb_width;
	*height = rtg_fb_height;
	*pitch = RTG_FB_MAX_WIDTH * 4;
	return true;
}

void retro_rtg_free(void)
{
	xfree(rtg_fb);
	rtg_fb = NULL;
	rtg_fb_valid = false;
}

#else

bool retro_rtg_frame(const uae_u32 **buf, int *width, int *height, int *pitch)
{
	return false;
}

void retro_rtg_free(void)
{
}

#endif /* PICASSO96 */
