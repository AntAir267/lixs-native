/* What the shared runtime asks of the game-specific side, and the hooks into LEGO Island Xtreme
   Stunts' own functions (none yet). */
#include <string.h>
#include <stdlib.h>
#include "runtime.h"
#include "input.h"
#include "funcs.h"

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

void game_init(void)
{
    hook_game_assert = hook_game_assert_fn;
}

/* (The command line AutoRun.exe gives Game.EXE is PORT_CMDLINE_ARGS in port.h.) */
