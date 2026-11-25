/* The playing size and widescreen (DESIGN.md decision 10).
   option resolution = native (the screen's size) | original (the game's own choice: 640x480, or
   its Options menu's 800x600 .. 1280x1024) | WxH
   option widescreen = on (default) | off: on, a picture wider than 4:3 shows more at the sides
   (Hor+) and the 2D keeps its 4:3 shape in the middle; off, the original look: the game's own
   size (unless resolution says otherwise) and its 4:3 picture, stretched to a wider screen.
   Test runs (--test) default to off, so the reference routes stay as recorded; options.txt turns
   it on there. */
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <math.h>
#include "runtime.h"
#include "win.h"
#include "options.h"
#include "funcs.h"
#include "widescreen.h"

int ws_on;                              /* Hor+ and the 2D kept 4:3 */
int ws_w, ws_h;                         /* the playing size (0: the game's own) */
static void widescreen_hooks(void);

void widescreen_init(void)
{
    ws_on = option_bool("widescreen", !deterministic);
    const char *r = option_str("resolution", ws_on ? "native" : "original");
    int w = 0, h = 0;
    if (!strcasecmp(r, "native"))
        host_desktop_size(&w, &h);
    else if (sscanf(r, "%dx%d", &w, &h) != 2 || w < 640 || h < 480)
        w = h = 0;
    ws_w = w;
    ws_h = h;
    if (!w || 3 * w <= 4 * h)
        ws_on = 0;                      /* (4:3 or narrower: nothing to widen) */
    if (!w)
        LOG(LOG_ALWAYS, "playing size: the game's own (resolution = %s)", r);
    else
        LOG(LOG_ALWAYS, "playing size: %dx%d (resolution = %s), widescreen %s", w, h, r, ws_on ? "on" : "off");
    widescreen_hooks();
}

/* (d3d8.c) the back buffer: the playing size whatever the game asks; the game reads it back into
   its present parameters (0x5eb244, D3DPRESENT_PARAMETERS) and sizes everything from there */
void port_d3d8_backbuffer(unsigned *w, unsigned *h)
{
    if (!ws_w)
        return;
    *w = (unsigned)ws_w;
    *h = (unsigned)ws_h;
}

/* ------------------------------------------------------------------ the projection (Hor+)
   0x418890 (cdecl: camera + 0x9c, fov, aspect, near, far, centre x, centre y; 1 if it built the
   matrices) builds the projection at +0x88 of its first argument (camera + 0x124, given to
   SetTransform) and that times the view (+0x48) at +8 (camera + 0xa4: c[76] of the game's vertex
   shaders, through the copy at 0x5eb73c) [verified: disassembly of 0x409f90, 0x4c27b0]. Its x
   scale is cot(fov / 2), its y scale that times the aspect (4/3): the fov is the horizontal one,
   so a wider back buffer stretches the picture. Scaling the x column of both matrices by
   1 / k (k = the size's aspect / 4:3) keeps the vertical view and widens the horizontal. */
static float ws_k = 1.0f;

static void scale_col0(uint32_t m, float s)
{
    for (int r = 0; r < 4; r++) {
        float v;
        memcpy(&v, gptr(m + 16 * r), 4);
        v *= s;
        memcpy(gptr(m + 16 * r), &v, 4);
    }
}

static int hook_ws_projection_fn(CPU *c)
{
    if (!ws_on)
        return 0;
    uint32_t p = R32(c->esp + 4);
    F_00418890_orig(c);
    if (c->eax) {
        scale_col0(p + 0x88, 1.0f / ws_k);
        scale_col0(p + 8, 1.0f / ws_k);
    }
    return 1;
}

/* 0x40a360 (cdecl: camera) builds the view's corner (+0x48, and from the eye +0x54) and its
   per-pixel steps across (+0x60) and down (+0x6c) from the fov and aspect, for culling (0x418aa0
   builds the frustum planes from them) and picking: widened by the same k about the centre. */
static int hook_ws_frustum_fn(CPU *c)
{
    if (!ws_on)
        return 0;
    uint32_t p = R32(c->esp + 4);
    F_0040a360_orig(c);
    float corner[3], step[3], eye[3];
    memcpy(corner, gptr(p + 0x48), 12);
    memcpy(step, gptr(p + 0x60), 12);
    memcpy(eye, gptr(p), 12);
    float half = 0.5f * (float)ws_w * (ws_k - 1.0f);
    for (int i = 0; i < 3; i++) {
        corner[i] -= half * step[i];
        step[i] *= ws_k;
        eye[i] = corner[i] - eye[i];
    }
    memcpy(gptr(p + 0x48), corner, 12);
    memcpy(gptr(p + 0x54), eye, 12);
    memcpy(gptr(p + 0x60), step, 12);
    return 1;
}

/* ------------------------------------------------------------------ the 2D, kept 4:3
   The game draws its 2D (the title, menu panels, text, the HUD, wipes, films) through one queue:
   0x4053a0 (cdecl: texture or 0, vertices (x, y, colour[, u, v]), count, mode) adds a strip after
   its own scale and offset (0x5c0778, 0x5c0618), 0x405650 (cdecl: 1, or 2 when its buffer is
   full) draws the batch with vertex shader constants c[0] (0x578710: x scale, y scale, x offset,
   y offset) and a viewport (0x412aa0: SetViewport's wrapper, cdecl D3DVIEWPORT8 *) that 0x405ae0
   works out [verified: disassembly of 0x4053a0, 0x405650, 0x405ae0; the films (0x40a700) go
   through 0x405ae0 too]. Two ways are used [verified: logged and seen on the route at 1280x720]:
   the title and menus are 640x480 units stretched over the whole back buffer (c[0] at the title
   1.01/320, -1.01/180; the logo 2 x 1.45 times its 640x480 size), so a wider back buffer
   stretched them; the star wipe draws in the screen's own pixels about the centre (its frame
   -320..960 x -120..600 at 1280x720), already the right shape.
   While 0x405ae0 runs, its viewport is narrowed about the screen's centre by how much wider a
   unit is drawn than tall (queue scale x c[0] x viewport, x against y): the stretched 2D keeps
   its 4:3 shape in the middle of the screen, clipped there as at 4:3, the 3D (Hor+) showing at
   the sides; 2D drawn in the screen's pixels is left as it is. */
#define Q2D_SCALE 0x5c0778u             /* the queue's x, y scale */
#define C0 0x578710u                    /* c[0] */
static int in_2d;

static float rdf(uint32_t a) { float v; memcpy(&v, gptr(a), 4); return v; }

static int hook_ws_2d_view_fn(CPU *c)
{
    if (!ws_on)
        return 0;
    in_2d = 1;
    F_00405ae0_orig(c);
    in_2d = 0;
    return 1;
}

static int hook_ws_viewport_fn(CPU *c)
{
    if (!in_2d)
        return 0;
    uint32_t v = R32(c->esp + 4);
    uint32_t x = R32(v), w = R32(v + 8), h = R32(v + 12);
    float ux = fabsf(rdf(Q2D_SCALE) * rdf(C0)) * (float)w, uy = fabsf(rdf(Q2D_SCALE + 4) * rdf(C0 + 4)) * (float)h;
    float kk = uy > 0.0f ? ux / uy : 1.0f;
    if (!(kk > 1.001f && kk < 4.0f))
        return 0;                       /* (square units: the screen's pixels) */
    float mid = 0.5f * (float)ws_w;
    int x0 = (int)lroundf(mid + ((float)x - mid) / kk);
    int x1 = (int)lroundf(mid + ((float)(x + w) - mid) / kk);
    W32(v, (uint32_t)x0);
    W32(v + 8, (uint32_t)(x1 - x0));
    F_00412aa0_orig(c);
    W32(v, x);
    W32(v + 8, w);
    return 1;
}

static void widescreen_hooks(void)
{
    if (ws_on)
        ws_k = ((float)ws_w / (float)ws_h) / (4.0f / 3.0f);
    hook_ws_projection = hook_ws_projection_fn;
    hook_ws_frustum = hook_ws_frustum_fn;
    hook_ws_2d_view = hook_ws_2d_view_fn;
    hook_ws_viewport = hook_ws_viewport_fn;
}
