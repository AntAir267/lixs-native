/* LEGO Island Xtreme Stunts' hooks into the AntiStatic runtime (weak defaults there). */
#include "runtime.h"

uint32_t game_dialog_box(CPU *c, uint32_t template_id, uint32_t proc, uint32_t param);
uint32_t port_dialog_box(CPU *c, uint32_t template_id, uint32_t proc, uint32_t param)
{
    return game_dialog_box(c, template_id, proc, param);
}

/* the linked-in CRT's memory and string routines (crt.c) */
void crt_init(void);
void port_runtime_ready(void) { crt_init(); }

/* DirectMusic 8 (the toolkit's runtime/music/dmusic8.c); DirectShow is video.c's own
   video_cocreate */
uint32_t dmusic_cocreate(uint32_t clsid, uint32_t iid, uint32_t ppv);
uint32_t port_cocreate(uint32_t clsid, uint32_t iid, uint32_t ppv) { return dmusic_cocreate(clsid, iid, ppv); }

/* a film's next picture while the program waits for messages (video.c) */
void video_idle(void);
void port_idle_hook(void) { video_idle(); }

/* ------------------------------------------------------------------ the controller
   The game's own joystick support [verified: disassembly and Ghidra decompiles]: DirectInput 8
   game controllers (DI8DEVTYPE_JOYSTICK or _GAMEPAD: 0x4187f0) become devices 2.. of its input
   manager (0x418340), each with the range -1000..1000 on every axis (0x4264f0); the first joystick
   (device 2) is read each frame beside the keyboard and ORed into it (0x447340). A joystick action
   is X or Y past 200 (codes 0-3, analogue beyond that: 0x426180) or a button (codes 4..: button
   n-4); the POV and the other axes are never read. Its default actions (0x5147c0 through
   0x426490, for a joystick with 10 or more buttons and a name not in the game's list of known pads,
   0x426630: ours is "Controller (XBOX 360 For Windows)"), with the keyboard's (table 0x578288:
   DIK + 0x20) and what the manual says each does:
     button 1  action 4  Right Ctrl   pizza / brake, reverse
     button 2  action 5  Right Shift  jump / accelerate (also action 28)
     button 3  action 6  Return       talk, interact, skateboard, leave a vehicle, land, stunt
                                      (held, with the arrows), power-up; menus: confirm
     button 4  action 7  Space        play (turn a thing into something else) / horn (also action 29)
     button 5  action 17 Backspace    the PDA
     button 6  action 10 /            camera (held, with the arrows)
     button 7  action 12 ,            camera zoom
     button 8  action 13 .            camera reset (held: first person)
     button 9  action 16 Escape       pause menu; menus: back
   (Options > Controls rebinds them: actions 4, 5, 6, 7, 17, 10, 12, 13; 0x514250.)
   So the Xbox layout, sent to the merged joystick (pad.c): X / LT button 1 (pizza, brake), A / RT
   button 2 (jump, accelerate), B button 3 (talk, action, stunt), Y button 4 (play, horn), Back
   button 5 (PDA), right stick click button 6 (camera), LB button 7 (zoom), RB button 8 (camera
   reset), Start button 9 (pause), left stick click button 10 (free, for rebinding). The left
   stick, or the D-pad at full lock while the stick rests, is X and Y (walking, steering; the
   menus). In menus A is Return and B Escape (game.c). The right stick is the camera as the manual
   gives it for the keyboard ("/" held with the arrows): pushed past 40 % it holds button 6 and is
   X and Y instead of the left stick. A, B or Start skips a film (video.c). */
#include <math.h>
#include <string.h>
#include "input.h"
#include "pad.h"

void video_skip_press(void);

#define TRIGGER_DOWN 6000               /* a trigger counts as a button from ~18 % of its travel */

int port_pad_send(int joy, const PadState *st)
{
    static uint8_t was[PAD_BUTTONS];
    static const int skip[] = {PAD_A, PAD_B, PAD_START};
    for (size_t i = 0; i < sizeof skip / sizeof *skip; i++)
        if (st->button[skip[i]] && !was[skip[i]])
            video_skip_press();
    memcpy(was, st->button, sizeof was);

    int sx = st->axis[PAD_LX], sy = st->axis[PAD_LY];
    if (sx == 0)
        sx = st->button[PAD_DLEFT] ? -32768 : st->button[PAD_DRIGHT] ? 32767 : 0;
    if (sy == 0)
        sy = st->button[PAD_DUP] ? -32768 : st->button[PAD_DDOWN] ? 32767 : 0;
    /* the right stick: the camera (button 6 held, the stick as X and Y) */
    int rx = st->axis[PAD_RX], ry = st->axis[PAD_RY], cam = hypot(rx, ry) > 0.4 * 32767;
    if (cam) {
        sx = rx;
        sy = ry;
    }
    input_joy_axis(joy, INPUT_AXIS_X, sx);
    input_joy_axis(joy, INPUT_AXIS_Y, sy);
    input_joy_axis(joy, INPUT_AXIS_RX, st->axis[PAD_RX]);
    input_joy_axis(joy, INPUT_AXIS_RY, st->axis[PAD_RY]);

    int lt = st->axis[PAD_LT] > TRIGGER_DOWN, rt = st->axis[PAD_RT] > TRIGGER_DOWN;
    const uint8_t *b = st->button;
    int jb[16] = {
        b[PAD_X] || lt, b[PAD_A] || rt, b[PAD_B], b[PAD_Y], b[PAD_BACK], b[PAD_RSTICK] || cam,
        b[PAD_LB], b[PAD_RB], b[PAD_START], b[PAD_LSTICK], b[PAD_GUIDE],
    };
    for (int i = 0; i < 16; i++)
        input_joy_button(joy, i, jb[i]);
    int up = b[PAD_DUP], dn = b[PAD_DDOWN], l = b[PAD_DLEFT], r = b[PAD_DRIGHT];
    int v = INPUT_POV_CENTERED;
    if (up && r) v = 4500; else if (r && dn) v = 13500;
    else if (dn && l) v = 22500; else if (l && up) v = 31500;
    else if (up) v = 0; else if (r) v = 9000; else if (dn) v = 18000; else if (l) v = 27000;
    input_joy_pov(joy, 0, v);           /* (the game never reads it) */
    return 1;
}
