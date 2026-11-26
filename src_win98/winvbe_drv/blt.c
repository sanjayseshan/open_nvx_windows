/*
 * WINVBE - BitBlt and rectangle filling
 */
#include "drv.h"

/* Shared scratch used by StretchBlt and source format conversion. */
BYTE bufT[ROWBUF];

/* build n pixels of pattern for destination row y starting at x */
void pattern_row(BYTE *dst, LPPBRUSH br, LPDRAWMODE dm, SURF *d, WORD y, WORD x, WORD n)
{
    WORD i;
    BYTE r = (BYTE)(y & 7);
    if (!br || br->style == BS_HOLLOW) {
        memset(dst, 0, n * d->Bpp);
        return;
    }
    if (d->mono) {
        BYTE bits = br->mono[r];
        if (br->flags & PBF_MONOPAT) {
            /* mono pattern brush on mono dest: 0 -> text color, 1 -> bk color */
            BYTE fg = dm ? mono_of(dm->TextColor) : 0x00;
            BYTE bg = dm ? mono_of(dm->bkColor) : 0xFF;
            BYTE m = br->mask[r];
            for (i = 0; i < n; i++)
                dst[i] = (m & (0x80 >> ((x + i) & 7))) ? bg : fg;
            return;
        }
        for (i = 0; i < n; i++)
            dst[i] = (bits & (0x80 >> ((x + i) & 7))) ? 0xFF : 0x00;
        return;
    }
    if (br->flags & PBF_SOLID) {
        pix_fill(dst, n, pix_get(br->pat, d->Bpp), d->Bpp);
        return;
    }
    if (br->flags & PBF_MONOPAT) {
        DWORD fg = pix_of(dm ? dm->TextColor : 0), bg = pix_of(dm ? dm->bkColor : phys_white());
        BYTE m = br->mask[r];
        BYTE Bpp = d->Bpp;
        for (i = 0; i < n; i++) {
            pix_put(dst, (m & (0x80 >> ((x + i) & 7))) ? bg : fg, Bpp);
            dst += Bpp;
        }
        return;
    }
    {
        const BYTE *row = br->pat + r * 32;
        BYTE Bpp = d->Bpp;
        WORD c = x & 7;
        WORD k;
        /* first partial period */
        while (n && c) {
            for (k = 0; k < Bpp; k++) *dst++ = row[c * Bpp + k];
            c = (c + 1) & 7; n--;
        }
        /* full periods */
        {
            WORD per = 8 * Bpp;
            while (n >= 8) { memcpy(dst, row, per); dst += per; n -= 8; }
            if (n) memcpy(dst, row, n * Bpp);
        }
    }
}

/* convert a source row into destination working format in bufS */
void load_src(SURF *s, SURF *d, WORD y, WORD x, WORD n, LPDRAWMODE dm, BYTE rop3)
{
    WORD i;
    if (s->planar) {
        static const DWORD vga16[16] = {
            0x000000UL, 0x000080UL, 0x008000UL, 0x008080UL, 0x800000UL, 0x800080UL, 0x808000UL, 0xC0C0C0UL,
            0x808080UL, 0x0000FFUL, 0x00FF00UL, 0x00FFFFUL, 0xFF0000UL, 0xFF00FFUL, 0xFFFF00UL, 0xFFFFFFUL };
        DWORD map[16];
        BYTE Bpp = d->Bpp;
        for (i = 0; i < 16; i++)
            map[i] = d->mono ? (rgb_is_white(vga16[i]) ? 0xFF : 0) : pix_of(rgb_to_phys(vga16[i]));
        row_read(s, y, x, n, bufS);
        for (i = n; i-- > 0;)
            pix_put(bufS + i * Bpp, map[bufS[i] & 15], Bpp);
        return;
    }
    if (s->mono == d->mono && (s->mono || s->Bpp == d->Bpp)) {
        row_read(s, y, x, n, bufS);
        return;
    }
    if (s->mono) {
        DWORD fg, bg;
        BYTE Bpp;
        /* mono -> color text: 0 = text color, 1 = background color */
        fg = dm ? pix_of(dm->TextColor) : 0;
        bg = dm ? pix_of(dm->bkColor) : pix_of(phys_white());
        Bpp = d->Bpp;
        row_read(s, y, x, n, bufS);
        for (i = n; i-- > 0;)
            pix_put(bufS + i * Bpp, bufS[i] ? bg : fg, Bpp);
        return;
    }
    if (d->mono) {
        /* color -> mono: pixels equal to the background become white */
        DWORD bg = dm ? pix_of(dm->bkColor) : pix_of(phys_white());
        BYTE Bpp = s->Bpp;
        row_read(s, y, x, n, bufS);
        for (i = 0; i < n; i++)
            bufS[i] = (pix_get(bufS + i * Bpp, Bpp) == bg) ? 0xFF : 0x00;
        return;
    }

    /* Color-to-color blits may cross formats (notably 24-bit DDBs into the
       32-bit desktop). The old final branch treated every differing color
       format as color-to-mono and wrote one byte per pixel into bufS. That
       corrupted icons and bitmap content, and left most of the destination
       row uninitialized. Convert each source pixel through logical RGB into
       the destination's working pixel format instead. */
    {
        BYTE srcBpp = s->Bpp, dstBpp = d->Bpp;
        row_read(s, y, x, n, bufT);
        for (i = 0; i < n; i++) {
            const BYTE *src = bufT + i * srcBpp;
            DWORD rgb;
            if (srcBpp == 1) {
                BYTE index = src[0];
                rgb = (DWORD)g.pal[index * 4] |
                      ((DWORD)g.pal[index * 4 + 1] << 8) |
                      ((DWORD)g.pal[index * 4 + 2] << 16);
            } else if (srcBpp == 3) {
                /* DIB/DDB 24-bit pixels are stored B, G, R. */
                rgb = (DWORD)src[2] | ((DWORD)src[1] << 8) |
                      ((DWORD)src[0] << 16);
            } else {
                rgb = pixel_to_rgb(pix_get(src, srcBpp));
            }
            pix_put(bufS + i * dstBpp, pix_of(rgb_to_phys(rgb)), dstBpp);
        }
    }
}

/* solid fill of a rectangle (already clipped) with a physical color */
void solid_rect(SURF *d, SHORT x, SHORT y, SHORT w, SHORT h, DWORD phys, BYTE rop3)
{
    WORD r;
    DWORD pix = d->mono ? (DWORD)mono_of(phys) : pix_of(phys);
    BYTE Bpp = d->Bpp;
    if (w <= 0 || h <= 0) return;
    if (rop3 == 0xF0 && d->screen) {
        static WORD fill_count = 0;
        static WORD gpu_fill_ok = 0, gpu_fill_fail = 0;
        DWORD gpu_pix = pix;
        if (fill_count < 10 || (fill_count % 100) == 0) {
            DBG("NVX [2D-FILL #%u]: screen solid fill (%d,%d) %dx%d color=%08lx (wc=%u shadow=%u)\n",
                fill_count, x, y, w, h, pix, g.wc ? 1 : 0, g.shadow);
        }
        fill_count++;
        if (g.svga && (DWORD)w * h >= 2048 && svga_fill(pix, x, y, w, h)) return;
        /* The CPU scanout path makes XRGB pixels opaque before writing the
           A8R8G8B8 mode. Match that behavior for the GPU path. Avoid a VxD
           call for tiny glyph/edge spans; its synchronous CE fence costs more
           than filling a few pixels on the CPU. */
        if (Bpp == 4 && g.pf == PF_32)
            gpu_pix |= 0xFF000000UL;
        if (g.ini_gpufill && Bpp == 4 && (DWORD)w * (DWORD)h >= 256UL &&
            !g.xf && !g.in_background && !lut_hc &&
            nvx_accel_fill_rect(x, y, w, h, gpu_pix)) {
            if (gpu_fill_ok < 4) {
                DBG("NVX [2D-GPU]: fill rect %dx%d at (%d,%d) color=%08lx\n",
                    w, h, x, y, gpu_pix);
                ++gpu_fill_ok;
            }
            /* Keep GDI's RAM shadow coherent; the VxD has already filled the
               GPU scanout. Do not fall through to the CPU framebuffer copy. */
            if (g.shadow)
                for (r = 0; r < (WORD)h; ++r)
                    scr_fill_shadow((WORD)(y + r), (WORD)x, (WORD)w, pix);
            return;
        }
        if (g.ini_gpufill && Bpp == 4 && (DWORD)w * (DWORD)h >= 256UL &&
            !g.xf && !g.in_background && !lut_hc && gpu_fill_fail < 4) {
            DBG("NVX [2D-GPU]: fill rect fell back to CPU %dx%d at (%d,%d)\n",
                w, h, x, y);
            ++gpu_fill_fail;
        }
        for (r = 0; r < (WORD)h; r++)
            scr_fill(y + r, x, w, pix);
        return;
    }
    if (rop3 == 0xF0 && !d->mono) {
        for (r = 0; r < (WORD)h; r++) {
            LPBYTE p = mem_row(d, y + r) + x * Bpp;
            if (Bpp == 1) far_memset(p, (BYTE)pix, w);
            else {
                WORD left = w, xx = 0;
                while (left) {
                    WORD c = left > CHUNKPX ? CHUNKPX : left;
                    pix_fill(bufP, c, pix, Bpp);
                    far_memcpy(p + xx * Bpp, bufP, c * Bpp);
                    xx += c; left -= c;
                }
            }
        }
        return;
    }
    {
        BYTE needD = ROP_NEEDS_D(rop3) ? 1 : 0;
        for (r = 0; r < (WORD)h; r++) {
            WORD left = w, xx = x;
            while (left) {
                WORD c = left > CHUNKPX ? CHUNKPX : left;
                pix_fill(bufP, c, pix, Bpp);
                if (needD) row_read(d, y + r, xx, c, bufD);
                rop_apply(rop3, bufD, NULL, bufP, c * Bpp);
                row_write(d, y + r, xx, c, bufD);
                xx += c; left -= c;
            }
        }
    }
}

/* pattern fill of a clipped rectangle */
void fill_rect(SURF *d, SHORT x, SHORT y, SHORT w, SHORT h, BYTE rop3, LPPBRUSH br, LPDRAWMODE dm)
{
    WORD r;
    BYTE needD, needP;
    if (w <= 0 || h <= 0) return;
    needP = ROP_NEEDS_P(rop3) ? 1 : 0;
    needD = ROP_NEEDS_D(rop3) ? 1 : 0;
    if (!needP || (br && (br->flags & PBF_SOLID) && !(br->flags & PBF_MONOPAT))) {
        DWORD c = 0;
        if (br && needP) c = d->mono ? (br->mono[0] ? phys_white() : 0) : br->fg;
        solid_rect(d, x, y, w, h, c, rop3);
        return;
    }
    if (br && br->style == BS_HOLLOW)
        return;
    for (r = 0; r < (WORD)h; r++) {
        WORD left = w, xx = x;
        while (left) {
            WORD c = left > CHUNKPX ? CHUNKPX : left;
            pattern_row(bufP, br, dm, d, y + r, xx, c);
            if (needD) row_read(d, y + r, xx, c, bufD);
            rop_apply(rop3, bufD, NULL, bufP, c * d->Bpp);
            row_write(d, y + r, xx, c, bufD);
            xx += c; left -= c;
        }
    }
}

WORD EXPORT BitBlt(LPVOID lpDst, SHORT dx, SHORT dy, LPVOID lpSrc, SHORT sx, SHORT sy,
                   SHORT w, SHORT h, DWORD rop, LPPBRUSH br, LPDRAWMODE dm)
{
    static WORD blt_probe_count;
    TRACE("BitBlt\n");
    SURF d, s;
    BYTE rop3 = (BYTE)(rop >> 16);
    BYTE needS = ROP_NEEDS_S(rop3) ? 1 : 0;
    BYTE needP = ROP_NEEDS_P(rop3) ? 1 : 0;
    BYTE needD = ROP_NEEDS_D(rop3) ? 1 : 0;
    SHORT r, rstart, rend, rstep;
    BYTE rtl = 0, same = 0;
    SHORT el, et, er, eb;
    static WORD icon_rop_trace_count;
    BYTE trace_icon = 0, trace_memory = 0;
    static WORD memory_trace_count;
    DWORD memory_source_chroma = 0, memory_output_chroma = 0;
    DWORD icon_color_pixels = 0, icon_nonwhite_pixels = 0, icon_chroma_pixels = 0;
    DWORD icon_output_chroma = 0;

    if (g.debug && blt_probe_count < 24) {
        BYTE dstType = lpDst ? ((LPGBITMAP)lpDst)->bmType : 0xFF;
        BYTE srcType = (needS && lpSrc) ? ((LPGBITMAP)lpSrc)->bmType : 0xFF;
        DBG("NVX [2D-PROBE #%u]: BitBlt dstType=%u srcType=%u rop3=%02x needS=%u brush=%x/%x size=%dx%d\n",
            blt_probe_count, dstType, srcType, rop3, needS,
            br ? br->style : 0xFFFF, br ? br->flags : 0, w, h);
        ++blt_probe_count;
    }

    if (!g.enabled && ((LPGBITMAP)lpDst)->bmType) return 1;
#ifndef OLDWIN
    {
        typedef WORD (__far __pascal *PF_DIB_BITBLT)(LPVOID, SHORT, SHORT, LPVOID, SHORT, SHORT, SHORT, SHORT, DWORD, LPPBRUSH, LPDRAWMODE);
        if (dibeng_on) {
            BYTE screen_copy = ((LPGBITMAP)lpDst)->bmType && needS && lpSrc &&
                               ((LPGBITMAP)lpSrc)->bmType && rop3 == 0xCC;
            BYTE screen_solid_fill = ((LPGBITMAP)lpDst)->bmType && !needS &&
                                     rop3 == 0xF0 && br &&
                                     (br->flags & PBF_SOLID) &&
                                     !(br->flags & PBF_MONOPAT);
            if (screen_solid_fill && g.debug)
                DBG("NVX [2D-PROBE]: dispatching screen solid fill to NVX BitBlt\n");
            if (!screen_copy && !screen_solid_fill) {
                return ((PF_DIB_BITBLT)dibe_tab[0])(lpDst, dx, dy, lpSrc, sx, sy, w, h, rop, br, dm);
            }
        }
    }
#endif
    surf_from_dev(&d, lpDst);
    if (g.debug > 1)
        dbg_printf("BitBlt d=%u/%u (%d,%d) s=%u (%d,%d) %dx%d rop=%lx br=%x/%x fg=%lx\n", d.screen, d.Bpp, dx, dy,
                   needS ? (((LPGBITMAP)lpSrc)->bmType ? 1 : ((LPGBITMAP)lpSrc)->bmBitsPixel) : 99, sx, sy, w, h, rop,
                   br ? br->style : 0xFFFF, br ? br->flags : 0, br ? br->fg : 0UL);
    if (g.debug > 1 && dm)
        dbg_printf("   dm text=%lx bk=%lx bkmode=%u\n", dm->TextColor, dm->bkColor, dm->bkMode);
#ifdef BLTDBG
    if (g.debug == 1)
        dbg_printf("BB %x<%x d=%u/%u (%d,%d) s=%u/%u (%d,%d) %dx%d rop=%lx br=%x fg=%lx dm=%lx/%lx\n",
                   d.screen ? 0 : FP_SEG(((LPGBITMAP)lpDst)->bmBits), (needS && !((LPGBITMAP)lpSrc)->bmType) ? FP_SEG(((LPGBITMAP)lpSrc)->bmBits) : 0,
                   d.screen, d.Bpp, dx, dy,
                   needS ? (((LPGBITMAP)lpSrc)->bmType ? 1 : 0) : 9, needS ? ((LPGBITMAP)lpSrc)->bmBitsPixel * 10 + ((LPGBITMAP)lpSrc)->bmPlanes : 0, sx, sy, w, h, rop,
                   br ? br->style : 0xFFFF, br ? br->fg : 0UL, dm ? dm->TextColor : 0UL, dm ? dm->bkColor : 0UL);
#endif
    if (needS) {
        if (!lpSrc) return 0;
        surf_from_dev(&s, lpSrc);
    }
    /* Capture one real 32x32 icon mask/color pair at the driver boundary.
       Win98 draws color icons as MERGEPAINT(mask), then SRCAND(color). This
       trace records the input pixels and destination around each real ROP so
       mask interpretation can be checked without substituting a new rule. */
    if (d.screen && needS && lpSrc && w == 32 && h == 32 &&
        ((rop3 == 0xBB && s.mono) || (rop3 == 0x88 && !s.mono)) &&
        icon_rop_trace_count < 4) {
        trace_icon = 1;
        ++icon_rop_trace_count;
        DBG("NVX [ICON-ROP #%u]: rop=%02x src=(%d,%d) dst=(%d,%d) srcfmt=%ux%ubpp/%u mono=%u pitch=%u\n",
            icon_rop_trace_count - 1, rop3, sx, sy, dx, dy, s.width, s.height,
            ((LPGBITMAP)lpSrc)->bmBitsPixel, s.mono, s.pitch);
        DBG("NVX [ICON-ROP-OBJ]: bitmap=%x:%x bits=%x:%x\n",
            FP_SEG(lpSrc), FP_OFF(lpSrc), FP_SEG(((LPGBITMAP)lpSrc)->bmBits),
            FP_OFF(((LPGBITMAP)lpSrc)->bmBits));
    }
    if (needS && !d.screen && !s.screen && w >= 16 && w <= 128 &&
        h >= 16 && h <= 32 && memory_trace_count < 48) {
        trace_memory = 1;
        ++memory_trace_count;
        DBG("NVX [BITMAP-XFER]: rop=%02x src=%x:%x %ux%u/%u mono=%u (%d,%d) dst=%x:%x %ux%u/%u mono=%u (%d,%d) size=%dx%d text=%lx bk=%lx\n",
            rop3, s.sel, s.off, s.width, s.height, s.Bpp, s.mono, sx, sy,
            d.sel, d.off, d.width, d.height, d.Bpp, d.mono, dx, dy, w, h,
            dm ? dm->TextColor : 0UL, dm ? dm->bkColor : 0UL);
    }
    /* clip destination */
    if (dx < 0) { w += dx; if (needS) sx -= dx; dx = 0; }
    if (dy < 0) { h += dy; if (needS) sy -= dy; dy = 0; }
    if (dx + w > (SHORT)d.width) w = d.width - dx;
    if (dy + h > (SHORT)d.height) h = d.height - dy;
    if (needS) {
        if (sx < 0) { w += sx; dx -= sx; sx = 0; }
        if (sy < 0) { h += sy; dy -= sy; sy = 0; }
        if (sx + w > (SHORT)s.width) w = s.width - sx;
        if (sy + h > (SHORT)s.height) h = s.height - sy;
    }
    if (w <= 0 || h <= 0) return 1;

#ifdef BLTDBG
    if (g.debug == 1 && needS && !s.screen && s.Bpp == 4 && !s.mono && w == 32 && h == 32) {
        WORD k;
        row_read(&s, sy + 16, sx, 32, bufS);
        dbg_printf("  row16:");
        for (k = 0; k < 32; k++) dbg_printf(" %lx", ((LPDWORD)bufS)[k]);
        dbg_printf("\n");
    }
    if (g.debug == 1 && needS && !s.screen && s.Bpp == 1 && !s.mono && w == 16 && h == 16) {
        WORD k, q;
        for (q = 0; q < 16; q += 5) {
            row_read(&s, sy + q, sx, 16, bufS);
            dbg_printf("  row%u:", q);
            for (k = 0; k < 16; k++) dbg_printf(" %02x", bufS[k]);
            dbg_printf("\n");
        }
    }
#endif

    el = dx; et = dy; er = dx + w; eb = dy + h;
    if (needS && s.screen) {
        if (sx < el) el = sx;
        if (sy < et) et = sy;
        if (sx + w > er) er = sx + w;
        if (sy + h > eb) eb = sy + h;
    }
    if (d.screen || (needS && s.screen)) {
        screen_busy++;
        cursor_exclude(el, et, er, eb);
    }

    if (!needS) {
        if (needP && br && br->style == BS_HOLLOW && !needD) { /* nothing */ }
        else fill_rect(&d, dx, dy, w, h, rop3, needP ? br : NULL, dm);
        goto done;
    }

    same = (d.screen && s.screen) ||
           (!d.screen && !s.screen && d.sel == s.sel && d.off == s.off);
    if (d.screen && s.screen && rop3 == 0xCC) {
        static WORD blt_copy_count = 0;
        if (blt_copy_count < 10 || (blt_copy_count % 100) == 0) {
            DBG("NVX [COPY #%u]: 2D screen copy (%d,%d)->(%d,%d) %dx%d (wc=%u shadow=%u)\n",
                blt_copy_count, sx, sy, dx, dy, w, h, g.wc ? 1 : 0, g.shadow);
        }
        blt_copy_count++;
    } else if (d.screen && needS) {
        static WORD blt_src_count = 0;
        if (blt_src_count < 5) {
            DBG("NVX [2D-BLT #%u]: src->screen blt (%d,%d)->(%d,%d) %dx%d rop=%02x src=%ux%ux%u pitch=%u (wc=%u shadow=%u)\n",
                blt_src_count, sx, sy, dx, dy, w, h, rop3, s.width, s.height,
                ((LPGBITMAP)lpSrc)->bmBitsPixel, ((LPGBITMAP)lpSrc)->bmWidthBytes,
                g.wc ? 1 : 0, g.shadow);
        }
        blt_src_count++;
    }
    /* A WC framebuffer is cheap to write but expensive to read back on the
       CPU. Route large 32bpp copies through the verified CE path. The miniport
       uses reserved scratch for overlap so BitBlt keeps memmove semantics. */
    if (d.screen && s.screen && rop3 == 0xCC && !g.shadow && !g.xf &&
        !lut_hc && g.ini_gpucopy && d.Bpp == 4 && s.Bpp == 4 &&
        !s.planar && !s.mono && (DWORD)w * (DWORD)h >= 1024UL) {
        static WORD gpu_copy_ok = 0, gpu_copy_fail = 0;
        if (nvx_accel_copy_rect(sx, sy, dx, dy, w, h)) {
            if (gpu_copy_ok < 4) {
                DBG("NVX [2D-GPU]: screen copy %dx%d (%d,%d)->(%d,%d)\n",
                    w, h, sx, sy, dx, dy);
                ++gpu_copy_ok;
            }
            goto done;
        }
        if (gpu_copy_fail < 4) {
            DBG("NVX [2D-GPU]: screen copy fell back to CPU %dx%d (%d,%d)->(%d,%d)\n",
                w, h, sx, sy, dx, dy);
            ++gpu_copy_fail;
        }
    }
    /* VMware SVGA II: screen to screen copy by the host */
    if (d.screen && s.screen && rop3 == 0xCC && g.svga && svga_copy(sx, sy, dx, dy, w, h)) goto done;
    rstart = 0; rend = h; rstep = 1;
    if (same && sy < dy) { rstart = h - 1; rend = -1; rstep = -1; }
    if (same && sy == dy && sx < dx) rtl = 1;

    {
        BYTE simple = (rop3 == 0xCC) && !s.planar && (s.mono == d.mono) && (s.mono || s.Bpp == d.Bpp);
        WORD Bpp = d.Bpp;
        for (r = rstart; r != rend; r += rstep) {
            WORD left = w;
            SHORT off = 0;
            if (rtl) off = w;
            while (left) {
                WORD c = left > CHUNKPX ? CHUNKPX : left;
                SHORT xo;
                if (rtl) { off -= c; xo = off; } else { xo = off; off += c; }
                if (simple) {
                    row_read(&s, sy + r, sx + xo, c, bufD);
                } else {
                    if (needD) row_read(&d, dy + r, dx + xo, c, bufD);
                    load_src(&s, &d, sy + r, sx + xo, c, dm, rop3);
                    if (needP) pattern_row(bufP, br, dm, &d, dy + r, dx + xo, c);
                    if (trace_memory && !d.mono && Bpp >= 3) {
                        WORD k;
                        for (k = 0; k < c; ++k) {
                            DWORD v = pix_get(bufS + k * Bpp, Bpp);
                            if ((BYTE)v != (BYTE)(v >> 8) || (BYTE)(v >> 8) != (BYTE)(v >> 16))
                                ++memory_source_chroma;
                        }
                    }

                    if (trace_icon && rop3 == 0x88 && d.Bpp >= 3) {
                        WORD k;
                        for (k = 0; k < c; ++k) {
                            DWORD v = pix_get(bufS + k * Bpp, Bpp);
                            BYTE b0 = (BYTE)v, b1 = (BYTE)(v >> 8), b2 = (BYTE)(v >> 16);
                            ++icon_color_pixels;
                            if ((v & 0x00FFFFFFUL) != 0x00FFFFFFUL) ++icon_nonwhite_pixels;
                            if (b0 != b1 || b1 != b2) ++icon_chroma_pixels;
                        }
                    }
                    if (trace_icon && (r & 3) == 0 && xo == 0) {
                        WORD k;
                        DBG("NVX [ICON-ROP]: row%u x0/4/8/12/16/20/24/28 pre/src=", r);
                        for (k = 0; k < 8; ++k) {
                            WORD q = (WORD)(k * 4);
                            DWORD before = pix_get(bufD + q * Bpp, Bpp);
                            DWORD source = pix_get(bufS + q * Bpp, Bpp);
                            DBG(" %06lx/%06lx", before, source);
                        }
                        DBG("\n");
                    }
                    rop_apply(rop3, bufD, bufS, bufP, c * Bpp);
                    if (trace_icon && rop3 == 0x88 && d.Bpp >= 3) {
                        WORD k;
                        for (k = 0; k < c; ++k) {
                            DWORD rgb = pixel_to_rgb(pix_get(bufD + k * Bpp, Bpp));
                            if ((BYTE)rgb != (BYTE)(rgb >> 8) ||
                                (BYTE)(rgb >> 8) != (BYTE)(rgb >> 16))
                                ++icon_output_chroma;
                        }
                    }
                    if (trace_icon && (r & 3) == 0 && xo == 0) {
                        WORD k;
                        DBG("NVX [ICON-ROP]: row%u out=", r);
                        for (k = 0; k < 8; ++k)
                            DBG(" %06lx", pix_get(bufD + k * 4 * Bpp, Bpp));
                        DBG("\n");
                    }
                }
                if (trace_memory && !d.mono && Bpp >= 3) {
                    WORD k;
                    for (k = 0; k < c; ++k) {
                        DWORD v = pix_get(bufD + k * Bpp, Bpp);
                        if ((BYTE)v != (BYTE)(v >> 8) || (BYTE)(v >> 8) != (BYTE)(v >> 16))
                            ++memory_output_chroma;
                    }
                }
                row_write(&d, dy + r, dx + xo, c, bufD);
                left -= c;
            }
        }
    }
done:
    if (trace_memory)
        DBG("NVX [BITMAP-XFER-RESULT]: source-chromatic=%lu output-chromatic=%lu\n",
            memory_source_chroma, memory_output_chroma);
    if (trace_icon && rop3 == 0x88)
        DBG("NVX [ICON-COLOR]: source pixels=%lu nonwhite=%lu chromatic=%lu output-chromatic=%lu\n",
            icon_color_pixels, icon_nonwhite_pixels, icon_chroma_pixels,
            icon_output_chroma);
    if (d.screen || (needS && s.screen)) {
        cursor_unexclude();
        screen_busy--;
    }
    return 1;
}

/* nearest-neighbour stretch of bufS (n src pixels) into out (dn dest pixels) */
void stretch_row(BYTE *out, const BYTE *src, WORD sw, WORD dw, WORD dfirst, WORD dn, BYTE flip, BYTE Bpp)
{
    WORD i;
    DWORD acc = (DWORD)dfirst * sw;
    for (i = 0; i < dn; i++, acc += sw) {
        WORD sxp = (WORD)(acc / dw);
        if (flip) sxp = sw - 1 - sxp;
        switch (Bpp) {
        case 1: out[i] = src[sxp]; break;
        case 2: ((WORD *)out)[i] = ((const WORD *)src)[sxp]; break;
        case 3: out[i * 3] = src[sxp * 3]; out[i * 3 + 1] = src[sxp * 3 + 1]; out[i * 3 + 2] = src[sxp * 3 + 2]; break;
        default: ((DWORD *)out)[i] = ((const DWORD *)src)[sxp]; break;
        }
    }
}

WORD EXPORT StretchBlt(LPVOID lpDst, SHORT dx, SHORT dy, SHORT dw, SHORT dh, LPVOID lpSrc,
                       SHORT sx, SHORT sy, SHORT sw, SHORT sh, DWORD rop, LPPBRUSH br,
                       LPDRAWMODE dm, LPGRECT clip)
{
    TRACE("StretchBlt\n");
    SURF d, s;
    BYTE rop3 = (BYTE)(rop >> 16);
    BYTE needS = ROP_NEEDS_S(rop3) ? 1 : 0;
    BYTE needP = ROP_NEEDS_P(rop3) ? 1 : 0;
    BYTE needD = ROP_NEEDS_D(rop3) ? 1 : 0;
    BYTE flipx = 0, flipy = 0;
    SHORT cl, ct, cr, cb, y;
    SHORT lastsy = -32768;

    if (!g.enabled && ((LPGBITMAP)lpDst)->bmType) return 1;
    if (!dw || !dh) return 1;
    if (dw < 0) { dx += dw + 1; dw = -dw; flipx ^= 1; }
    if (dh < 0) { dy += dh + 1; dh = -dh; flipy ^= 1; }
    if (needS) {
        if (!lpSrc || !sw || !sh) return 1;
        if (sw < 0) { sx += sw + 1; sw = -sw; flipx ^= 1; }
        if (sh < 0) { sy += sh + 1; sh = -sh; flipy ^= 1; }
        surf_from_dev(&s, lpSrc);
        if (sw > CHUNKPX) return (WORD)-1;
        if (sx < 0 || sy < 0 || sx + sw > (SHORT)s.width || sy + sh > (SHORT)s.height) return (WORD)-1;
        if (s.screen && ((LPGBITMAP)lpDst)->bmType) return (WORD)-1;   /* overlapping screen stretch */
    }
    surf_from_dev(&d, lpDst);
#ifdef BLTDBG
    if (g.debug == 1)
        dbg_printf("SB %x<%x d=%u/%u (%d,%d %dx%d) s=%u (%d,%d %dx%d) rop=%lx fg=%lx dm=%lx/%lx\n",
                   d.screen ? 0 : FP_SEG(((LPGBITMAP)lpDst)->bmBits), (needS && !s.screen) ? FP_SEG(((LPGBITMAP)lpSrc)->bmBits) : 0, d.screen, d.Bpp, dx, dy, dw, dh,
                   needS ? ((LPGBITMAP)lpSrc)->bmBitsPixel : 0, sx, sy, sw, sh, rop, br ? br->fg : 0UL,
                   dm ? dm->TextColor : 0UL, dm ? dm->bkColor : 0UL);
#endif
    cl = dx; ct = dy; cr = dx + dw; cb = dy + dh;
    if (clip) {
        if (clip->left > cl) cl = clip->left;
        if (clip->top > ct) ct = clip->top;
        if (clip->right < cr) cr = clip->right;
        if (clip->bottom < cb) cb = clip->bottom;
    }
    if (cl < 0) cl = 0;
    if (ct < 0) ct = 0;
    if (cr > (SHORT)d.width) cr = d.width;
    if (cb > (SHORT)d.height) cb = d.height;
    if (cr <= cl || cb <= ct) return 1;
    if (!needS) {
        BEGIN_SCREEN(&d, cl, ct, cr, cb);
        fill_rect(&d, cl, ct, cr - cl, cb - ct, rop3, needP ? br : NULL, dm);
        END_SCREEN(&d);
        return 1;
    }
    if (d.screen || s.screen) {
        screen_busy++;
        cursor_exclude(cl, ct, cr, cb);
        if (s.screen) cursor_exclude(sx, sy, sx + sw, sy + sh);
    }
    for (y = ct; y < cb; y++) {
        SHORT ry = (SHORT)(((DWORD)(y - dy) * sh) / dh);
        SHORT srow = flipy ? sy + sh - 1 - ry : sy + ry;
        SHORT x0 = cl;
        if (srow != lastsy) {
            load_src(&s, &d, srow, sx, sw, dm, rop3);
            memcpy(bufT, bufS, sw * d.Bpp);
            lastsy = srow;
        }
        while (x0 < cr) {
            WORD n = cr - x0 > CHUNKPX ? CHUNKPX : cr - x0;
            if (rop3 == 0xCC) {
                stretch_row(bufD, bufT, sw, dw, x0 - dx, n, flipx, d.Bpp);
            } else {
                stretch_row(bufS, bufT, sw, dw, x0 - dx, n, flipx, d.Bpp);
                if (needD) row_read(&d, y, x0, n, bufD);
                if (needP) pattern_row(bufP, br, dm, &d, y, x0, n);
                rop_apply(rop3, bufD, bufS, bufP, n * d.Bpp);
            }
            row_write(&d, y, x0, n, bufD);
            x0 += n;
        }
    }
    if (d.screen || s.screen) {
        cursor_unexclude();
        screen_busy--;
    }
    return 1;
}
