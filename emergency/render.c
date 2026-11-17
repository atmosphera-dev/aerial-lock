#define _GNU_SOURCE
#include "locker.h"
#include <limits.h>

bool emergency_dimensions(uint32_t w, uint32_t h, int scale, int *pw, int *ph, size_t *size) {
    if (!w || !h || scale < 1 || w > INT32_MAX / 4u / (unsigned)scale ||
        h > INT32_MAX / (unsigned)scale) return false;
    uint64_t width = (uint64_t)w * (unsigned)scale;
    uint64_t height = (uint64_t)h * (unsigned)scale;
    uint64_t bytes = width * height * 4;
    if (bytes > INT32_MAX) return false; /* wl_shm_pool uses signed int32 sizes. */
    *pw = (int)width; *ph = (int)height; *size = (size_t)bytes;
    return true;
}

struct box emergency_box(int w, int h, int scale, bool rtl) {
    int height = 56 * scale;
    if (height > h) height = h;
    int width = 360 * scale;
    if (width > w - 32 * scale) width = w - 32 * scale;
    if (width < height) width = w < height ? w : height;
    int slot = height < width ? height : width;
    struct box b = {.x = (w - width) / 2, .y = (h - height) / 2,
                    .width = width, .height = height, .slot_size = slot};
    b.slot_x = rtl ? b.x : b.x + width - slot;
    b.dot_start = b.x + (rtl ? slot : 0) + 16 * scale;
    b.dot_end = b.x + width - (rtl ? 0 : slot) - 16 * scale;
    return b;
}

static void rect(uint32_t *p, int w, int h, int x, int y, int rw, int rh, uint32_t color) {
    for (int j = y < 0 ? 0 : y; j < y + rh && j < h; ++j)
        for (int i = x < 0 ? 0 : x; i < x + rw && i < w; ++i) p[(size_t)j * (size_t)w + (size_t)i] = color;
}

void emergency_draw(uint32_t *p, int w, int h, int s, const struct picture *state) {
    for (size_t i = 0; i < (size_t)w * (size_t)h; ++i) p[i] = 0xff000000;
    struct box b = emergency_box(w, h, s, state->rtl);
    uint32_t border = state->error ? 0xffff6666 : 0xffffffff;
    rect(p, w, h, b.x, b.y, b.width, b.height, border);
    rect(p, w, h, b.x + 2*s, b.y + 2*s, b.width - 4*s, b.height - 4*s, 0xff242424);
    int cy = b.y + b.height/2;
    int cx = b.slot_x + b.slot_size/2;
    int radius = b.slot_size/6;
    uint32_t ink = state->busy ? (state->pulse ? 0xff999999 : 0xff555555) : 0xffffffff;
    /* The arrow's square is centered in a height-sized slot. No font/glyphs. */
    rect(p,w,h,cx-radius,cy-s,2*radius+1,2*s,ink);
    for (int d = 0; d <= radius; ++d) {
        int x = state->rtl ? cx-radius+d : cx+radius-d;
        rect(p,w,h,x-s,cy-d-s,2*s,2*s,ink);
        rect(p,w,h,x-s,cy+d-s,2*s,2*s,ink);
    }
    int step = 14*s, r = 3*s;
    int capacity = (b.dot_end - b.dot_start) / step;
    size_t visible = capacity > 0 ? (size_t)capacity : 0;
    if (visible > state->dots) visible = state->dots;
    for (size_t n = 0; n < visible; ++n) {
        int dx = state->rtl ? b.dot_end - step/2 - (int)n*step : b.dot_start + step/2 + (int)n*step;
        for (int y = -r; y <= r; ++y) for (int x = -r; x <= r; ++x)
            if (x*x+y*y <= r*r) rect(p,w,h,dx+x,cy+y,1,1,0xffffffff);
    }
}
