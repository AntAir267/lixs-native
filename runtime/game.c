/* What the shared runtime asks of the game-specific side, and the hooks into LEGO Island Xtreme
   Stunts' own functions (none yet). */
#include <string.h>
#include <stdlib.h>
#include "runtime.h"
#include "input.h"
#include "funcs.h"
#include "widescreen.h"

int ifc_rumble_wanted(void) { return 0; }
void ifc_frame(unsigned long frame) { (void)frame; }

/* DialogBoxParamA: none known yet; logged and cancelled. */
uint32_t game_dialog_box(CPU *c, uint32_t template_id, uint32_t proc, uint32_t param)
{
    (void)c; (void)param;
    LOG(LOG_ALWAYS, "dialog %u (procedure %08x): cancelled", template_id, proc);
    return 0xffffffffu;
}

/* The game's assert (0x4446f0, cdecl (file, line, expression)): it formats "%s(%d): Assert '%s'"
   to OutputDebugStringA and then executes int3 [verified: disassembly 0x4446f0-0x44470e], which on
   Windows without a debugger ends the program. Logged here and carried on from, so one missing
   piece does not hide the next; LIXS_ASSERT_FATAL=1 stops as the original does. */
static int assert_fatal = -1;
static int hook_game_assert_fn(CPU *c)
{
    uint32_t file = R32(c->esp + 4), line = R32(c->esp + 8), expr = R32(c->esp + 12);
    if (assert_fatal < 0) assert_fatal = getenv("LIXS_ASSERT_FATAL") != NULL;
    LOG(LOG_ALWAYS, "assert: %s(%u): %s (called from %08x)", file ? gstr(file) : "?", line,
        expr ? gstr(expr) : "?", R32(c->esp));
    if (assert_fatal)
        return 0;
    c->eax = 0;                         /* AL = 0: the inlined `test al, al / je / int3` skips its int3 */
    c->ret = R32(c->esp);
    c->esp += 4;
    return 1;
}

/* ------------------------------------------------------------------ the controller in menus
   The game reads every device into a 32-bit word of actions (bit n = action n; 0x418580) through
   the action table the device was given that frame: each frame its main loop (0x447340) gives the
   keyboard (device 0) and the first joystick (device 2) either the player's tables (0x6a08a0 /
   0x6a09a0, the Options > Controls page's) or fixed ones (0x6a0920 / 0x6a0a20: the defaults, for
   the menus: front end, pause, loading; chosen when its state is 9, 0x690a30 is 0 or 0x6909c0 is
   set), reads both, and ORs the joystick's word into the keyboard's [verified: disassembly
   0x4473c4-0x44744c]. The menus move on actions 0-3 (the stick), confirm on action 6 (Return;
   the joystick's button 3) and go back on action 16 (Escape; button 9) [verified: 0x5066e7-0x506822,
   0x508097, 0x50f44c test those bits of the pressed word 0x60de80]. So while the joystick has the
   fixed table, its A (button 2: action 5, jump) is Return and its B (button 3: action 6, the
   action key) is Escape, as on a console; the keyboard is as shipped. port_lixs.c sends the
   controller's buttons. */
#define INPUT_MGR 0x5eb694u             /* the input manager: +0x1c its devices (0 keyboard, 1 mouse, 2.. joysticks) */
#define JOY_MENU_TABLE 0x6a0a20u
#define JOY_BUTTONS 0x2e8u              /* a joystick device's DIJOYSTATE2 buttons (its state at +0x2b8) */
static int joy_menus = -1;              /* the joystick has the fixed (menu) table this frame */
static int hook_input_bind_fn(CPU *c)
{
    if (ARG(0) == 2) {
        int m = ARG(1) == JOY_MENU_TABLE;
        if (m != joy_menus)
            LOG(deterministic ? LOG_ALWAYS : LOG_INPUT, "controller: %s layout from frame %lu",
                m ? "menu" : "play", frame_count);
        joy_menus = m;
    }
    return 0;
}

static int hook_input_read_fn(CPU *c)
{
    uint32_t out = ARG(1);
    if (ARG(0) != 2 || joy_menus != 1)
        return 0;
    F_00409df0_orig(c);
    uint32_t w = c->eax, mgr = R32(INPUT_MGR);
    if ((w & 0x80000000u) || !mgr)      /* (no joystick: the game masks this word off) */
        return 1;
    uint32_t joy = R32(R32(mgr + 0x1c) + 8);
    int a = R8(joy + JOY_BUTTONS + 1) & 0x80, b = R8(joy + JOY_BUTTONS + 2) & 0x80;
    w &= ~(0x20u | 0x40u | 0x10000000u);   /* (A's actions 5 and 28, B's 6) */
    if (a)
        w |= 0x40u;                     /* Return */
    if (b)
        w |= 0x10000u;                  /* Escape */
    c->eax = w;
    if (out)
        W32(out, w);
    return 1;
}

void game_init(void)
{
    hook_game_assert = hook_game_assert_fn;
    hook_input_bind = hook_input_bind_fn;
    hook_input_read = hook_input_read_fn;
    widescreen_init();
}

/* (The command line AutoRun.exe gives Game.EXE is PORT_CMDLINE_ARGS in port.h.) */
