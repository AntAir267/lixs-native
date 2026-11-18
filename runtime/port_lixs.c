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
