/* DirectShow, as much as a Win32 game's movie player usually needs (forked from hardtruck2-native's
   dshow.c through an earlier port; LIXS plays its Outro.mpg through it [guess: not yet reached in a
   test]): CoCreateInstance(CLSID_FilterGraph, IID_IGraphBuilder), RenderFile, QueryInterface for
   IMediaControl, IMediaEventEx, IMediaSeeking, IVideoWindow and IBasicVideo; IVideoWindow put_Owner,
   put_WindowStyle, put_WindowStyleEx, HideCursor, put_FullScreenMode, SetWindowPosition; IBasicVideo
   GetVideoSize; IMediaControl Run, Stop, Pause; IMediaSeeking GetPositions, SetPositions.

   A graph decodes its file's soundtrack whole with FFmpeg and plays it as a runtime voice; the
   position follows the clock from Run (the virtual clock under --test, where the soundtrack
   stays silent). The picture: decoded on the game's thread as the clock passes (video_idle,
   called while the game waits for messages) and drawn letterboxed into the host window with an
   SDL renderer, which goes when the film stops (the game makes its Direct3D device after the
   films; DXVK then takes the window). Headless runs draw nothing. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <SDL3/SDL.h>
#include "runtime.h"
#include "sound.h"
#include "win.h"
#include "options.h"

#define S_OK 0u
#define S_FALSE 1u
#define E_NOTIMPL 0x80004001u
#define E_NOINTERFACE 0x80004002u
#define E_POINTER 0x80004003u
#define E_FAIL 0x80004005u
#define E_ABORT 0x80004004u
#define VFW_E_NOT_FOUND 0x80040216u
#define EC_COMPLETE 1u

typedef struct Graph {
    int refs;
    int16_t *pcm;
    uint32_t frames;
    int channels, rate;
    int voice;                          /* runtime voice, -1 when not playing */
    int state;                          /* 0 stopped, 1 paused, 2 running */
    int complete_sent;
    int width, height;                  /* the picture's size */
    int64_t duration;                   /* 100 ns units */
    int64_t pos_at_run;                 /* the position when Run was called */
    double run_ms;                      /* the clock then */
    uint32_t obj_gb, obj_mc, obj_me, obj_ms, obj_vw, obj_bv;   /* the program's objects */
    char file[600];
    char host[2048];                    /* its host path, for the picture's decoder */
} Graph;

/* ------------------------------------------------------------------ the picture */
static struct {
    Graph *g;                           /* the film being drawn */
    AVFormatContext *fc;
    AVCodecContext *cc;
    AVFrame *fr, *next;
    AVPacket *pkt;
    int vi, eof, next_full;
    double tb;                          /* the stream's time base, seconds */
    SDL_Renderer *r;
    SDL_Texture *tex;
} pic = {.vi = -1};

static int pic_drawn;

static void pic_close(void)
{
    if (pic.g)
        LOG(LOG_ALWAYS, "DirectShow: %s: %d pictures drawn (renderer %s)", pic.g->file, pic_drawn,
            pic.r ? SDL_GetRendererName(pic.r) : "none");
    pic_drawn = 0;
    if (pic.tex) SDL_DestroyTexture(pic.tex);
    if (pic.r) SDL_DestroyRenderer(pic.r);
    pic.tex = NULL;
    pic.r = NULL;
    av_frame_free(&pic.fr);
    av_frame_free(&pic.next);
    pic.next_full = 0;
    av_packet_free(&pic.pkt);
    avcodec_free_context(&pic.cc);
    avformat_close_input(&pic.fc);
    pic.g = NULL;
    pic.vi = -1;
}

static int pic_open(Graph *g)
{
    pic_close();
    SDL_Window *w = host_sdl_window();
    if (headless || !w) return 0;
    if (avformat_open_input(&pic.fc, g->host, NULL, NULL) < 0 || avformat_find_stream_info(pic.fc, NULL) < 0)
        goto fail;
    pic.vi = av_find_best_stream(pic.fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (pic.vi < 0) goto fail;
    AVStream *st = pic.fc->streams[pic.vi];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec || !(pic.cc = avcodec_alloc_context3(codec)) ||
        avcodec_parameters_to_context(pic.cc, st->codecpar) < 0 || avcodec_open2(pic.cc, codec, NULL) < 0)
        goto fail;
    pic.tb = av_q2d(st->time_base);
    pic.fr = av_frame_alloc();
    pic.next = av_frame_alloc();
    pic.pkt = av_packet_alloc();
    pic.eof = 0;
    pic.r = SDL_CreateRenderer(w, NULL);
    if (!pic.r) {
        LOG(LOG_ALWAYS, "DirectShow: no renderer for the picture: %s", SDL_GetError());
        goto fail;
    }
    pic.tex = SDL_CreateTexture(pic.r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, g->width, g->height);
    pic.g = g;
    SDL_SetRenderDrawColor(pic.r, 0, 0, 0, 255);
    SDL_RenderClear(pic.r);
    SDL_RenderPresent(pic.r);
    return 1;
fail:
    pic_close();
    return 0;
}

static int64_t position(Graph *g);
static int graph_playing;               /* a film is running (skip_wanted applies) */
static int skip_wanted;

/* the frame due now drawn, if it is a new one (the game's thread): decoded frames wait in
   `next` until their time, the newest due one moves to `fr` and is drawn */
static double frame_time(const AVFrame *f)
{
    AVStream *st = pic.fc->streams[pic.vi];
    int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    return pts == AV_NOPTS_VALUE ? 0 : (pts - start) * pic.tb;
}

static void pic_step(void)
{
    Graph *g = pic.g;
    if (!g || g->state != 2) return;
    double t = position(g) / 1e7;       /* seconds into the film */
    int got = 0;
    while (!pic.eof) {
        if (pic.next_full) {
            if (frame_time(pic.next) > t) break;
            av_frame_unref(pic.fr);
            av_frame_move_ref(pic.fr, pic.next);
            pic.next_full = 0;
            got = 1;
            continue;
        }
        int r = avcodec_receive_frame(pic.cc, pic.next);
        if (r == 0) { pic.next_full = 1; continue; }
        if (r != AVERROR(EAGAIN)) { pic.eof = 1; break; }
        if (av_read_frame(pic.fc, pic.pkt) < 0) { avcodec_send_packet(pic.cc, NULL); continue; }
        if (pic.pkt->stream_index == pic.vi) avcodec_send_packet(pic.cc, pic.pkt);
        av_packet_unref(pic.pkt);
    }
    if (!got || !pic.fr->data[0]) return;
    SDL_UpdateYUVTexture(pic.tex, NULL, pic.fr->data[0], pic.fr->linesize[0], pic.fr->data[1],
                         pic.fr->linesize[1], pic.fr->data[2], pic.fr->linesize[2]);
    int ww = 1, wh = 1;
    SDL_GetCurrentRenderOutputSize(pic.r, &ww, &wh);
    float s = (float)ww / g->width < (float)wh / g->height ? (float)ww / g->width : (float)wh / g->height;
    SDL_FRect d = {(ww - g->width * s) / 2, (wh - g->height * s) / 2, g->width * s, g->height * s};
    SDL_RenderClear(pic.r);
    SDL_RenderTexture(pic.r, pic.tex, NULL, &d);
    SDL_RenderPresent(pic.r);
    pic_drawn++;
}

/* win.c, pad.c: any key or controller button skips the film playing (the game itself ends it
   only on Escape): it reports its stop position from then on */
void video_skip_press(void)
{
    if (pic.g || graph_playing) skip_wanted = 1;
}
int video_skip_taken(void) { return 0; }

/* user32.c: the game waits for a message (its film loop does, between 100 ms timer ticks) */
void video_idle(void)
{
    if (pic.g) pic_step();
}

static pthread_mutex_t gr_lock = PTHREAD_MUTEX_INITIALIZER;
static ComClass cls_gb, cls_mc, cls_me, cls_ms, cls_vw, cls_bv;

static const uint8_t g_unknown[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
/* {56a868XX-0ad4-11ce-b03a-0020af0ba770}: the DirectShow interfaces differ in the first byte */
static int is_ds_iid(uint32_t a, uint8_t first)
{
    static const uint8_t rest[15] = {0x68, 0xa8, 0x56, 0xd4, 0x0a, 0xce, 0x11, 0xb0, 0x3a,
                                     0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70};
    return a && R8(a) == first && !memcmp(gptr(a + 1), rest, 15);
}
static int is_unknown(uint32_t a) { return a && !memcmp(gptr(a), g_unknown, 16); }
/* IID_IMediaSeeking {36b73880-c2c8-11cf-8b46-00805f6cef60} */
static int is_seeking(uint32_t a)
{
    static const uint8_t g[16] = {0x80, 0x38, 0xb7, 0x36, 0xc8, 0xc2, 0xcf, 0x11, 0x8b, 0x46,
                                  0x00, 0x80, 0x5f, 0x6c, 0xef, 0x60};
    return a && !memcmp(gptr(a), g, 16);
}

/* ------------------------------------------------------------------ decoding */
static int decode_file(Graph *g, const char *host)
{
    AVFormatContext *fc = NULL;
    if (avformat_open_input(&fc, host, NULL, NULL) < 0)
        return 0;
    AVCodecContext *cc = NULL;
    SwrContext *sw = NULL;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *fr = av_frame_alloc();
    int ok = 0;
    if (avformat_find_stream_info(fc, NULL) < 0)
        goto out;
    if (fc->duration > 0)
        g->duration = fc->duration * 10;            /* AV_TIME_BASE (us) -> 100 ns */
    int vi = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vi >= 0) {
        g->width = fc->streams[vi]->codecpar->width;
        g->height = fc->streams[vi]->codecpar->height;
    }
    ok = 1;                             /* (a film without sound still plays) */
    int si = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (si < 0)
        goto out;
    const AVCodec *codec = avcodec_find_decoder(fc->streams[si]->codecpar->codec_id);
    if (!codec || !(cc = avcodec_alloc_context3(codec)) ||
        avcodec_parameters_to_context(cc, fc->streams[si]->codecpar) < 0 || avcodec_open2(cc, codec, NULL) < 0)
        goto out;
    int ch = cc->ch_layout.nb_channels >= 2 ? 2 : 1, rate = cc->sample_rate;
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, ch);
    if (swr_alloc_set_opts2(&sw, &out_layout, AV_SAMPLE_FMT_S16, rate, &cc->ch_layout, cc->sample_fmt, rate, 0, NULL) < 0 ||
        swr_init(sw) < 0)
        goto out;
    size_t cap = (size_t)rate * ch * 30, n = 0;     /* frames x channels, grown as needed */
    int16_t *pcm = malloc(cap * sizeof *pcm);
    while (av_read_frame(fc, pkt) >= 0) {
        if (pkt->stream_index == si && avcodec_send_packet(cc, pkt) >= 0) {
            while (avcodec_receive_frame(cc, fr) >= 0) {
                int out = swr_get_out_samples(sw, fr->nb_samples);
                if (n + (size_t)out * ch > cap) {
                    cap = (n + (size_t)out * ch) * 2;
                    pcm = realloc(pcm, cap * sizeof *pcm);
                }
                uint8_t *dst = (uint8_t *)(pcm + n);
                int got = swr_convert(sw, &dst, out, (const uint8_t **)fr->extended_data, fr->nb_samples);
                if (got > 0) n += (size_t)got * ch;
            }
        }
        av_packet_unref(pkt);
    }
    g->pcm = pcm;
    g->frames = (uint32_t)(n / ch);
    g->channels = ch;
    g->rate = rate;
out:
    swr_free(&sw);
    av_frame_free(&fr);
    av_packet_free(&pkt);
    avcodec_free_context(&cc);
    avformat_close_input(&fc);
    return ok;
}

static void stop_voice(Graph *g)
{
    if (g->voice >= 0) {
        sound_stream_stop(g->voice);
        g->voice = -1;
    }
}

/* the position now (100 ns units), under gr_lock */
static int64_t position(Graph *g)
{
    if (skip_wanted && g->state == 2) {
        LOG(LOG_ALWAYS, "DirectShow: %s skipped", g->file);
        skip_wanted = 0;
        g->pos_at_run = g->duration;
        g->run_ms = now_ms();
        stop_voice(g);
    }
    int64_t p = g->pos_at_run;
    if (g->state == 2)
        p += (int64_t)((now_ms() - g->run_ms) * 10000.0);
    return p > g->duration ? g->duration : p;
}

/* ------------------------------------------------------------------ the objects */
static Graph *graph_of(uint32_t obj) { return com_host(obj); }

static uint32_t qi(uint32_t self, uint32_t riid, uint32_t ppv)
{
    Graph *g = graph_of(self);
    uint32_t o = 0;
    if (!ppv) return E_POINTER;
    if (is_unknown(riid) || is_ds_iid(riid, 0xa9) || is_ds_iid(riid, 0x9f)) o = g->obj_gb;  /* IGraphBuilder, IFilterGraph */
    else if (is_ds_iid(riid, 0xb1)) o = g->obj_mc;          /* IMediaControl */
    else if (is_ds_iid(riid, 0xb6) || is_ds_iid(riid, 0xc0)) o = g->obj_me;   /* IMediaEvent(Ex) */
    else if (is_seeking(riid)) o = g->obj_ms;               /* IMediaSeeking */
    else if (is_ds_iid(riid, 0xb4)) o = g->obj_vw;          /* IVideoWindow */
    else if (is_ds_iid(riid, 0xb5)) o = g->obj_bv;          /* IBasicVideo */
    W32(ppv, o);
    if (!o) {
        LOG(LOG_ALWAYS, "DirectShow: QueryInterface {" GUID_FMT "}: not provided", GUID_ARGS(riid));
        return E_NOINTERFACE;
    }
    pthread_mutex_lock(&gr_lock);
    g->refs++;
    pthread_mutex_unlock(&gr_lock);
    return S_OK;
}

static uint32_t addref(uint32_t self)
{
    Graph *g = graph_of(self);
    pthread_mutex_lock(&gr_lock);
    int n = ++g->refs;
    pthread_mutex_unlock(&gr_lock);
    return n;
}

static uint32_t release(uint32_t self)
{
    Graph *g = graph_of(self);
    pthread_mutex_lock(&gr_lock);
    int n = --g->refs;
    if (n == 0) {
        if (pic.g == g) pic_close();
        stop_voice(g);
        free(g->pcm);
        g->pcm = NULL;
        g->frames = 0;
        LOG(LOG_SOUND, "DirectShow: graph of %s released", g->file);
    }
    pthread_mutex_unlock(&gr_lock);
    return n < 0 ? 0 : n;
}

#define UNK(p) \
    METHOD(p##_QueryInterface, 3) { return qi(ARG(0), ARG(1), ARG(2)); } \
    METHOD(p##_AddRef, 1) { return addref(ARG(0)); } \
    METHOD(p##_Release, 1) { return release(ARG(0)); }
#define DISP(p) \
    METHOD(p##_GetTypeInfoCount, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; } \
    METHOD(p##_GetTypeInfo, 4) { return E_NOTIMPL; } \
    METHOD(p##_GetIDsOfNames, 6) { return E_NOTIMPL; } \
    METHOD(p##_Invoke, 9) { return E_NOTIMPL; }
#define NOTIMPL(p, name, n) METHOD(p##_##name, n) { return E_NOTIMPL; }
#define OK(p, name, n) METHOD(p##_##name, n) { return S_OK; }
/* a get_ method: its out value 0 */
#define GET0(p, name) METHOD(p##_##name, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }

/* IGraphBuilder */
UNK(gb)
NOTIMPL(gb, AddFilter, 3) NOTIMPL(gb, RemoveFilter, 2) NOTIMPL(gb, EnumFilters, 2)
NOTIMPL(gb, FindFilterByName, 3) NOTIMPL(gb, ConnectDirect, 4) NOTIMPL(gb, Reconnect, 2)
NOTIMPL(gb, Disconnect, 2) OK(gb, SetDefaultSyncSource, 1)
NOTIMPL(gb, Connect, 3) NOTIMPL(gb, Render, 2) NOTIMPL(gb, AddSourceFilter, 4)
OK(gb, SetLogFile, 2) OK(gb, Abort, 1) OK(gb, ShouldOperationContinue, 1)

/* RenderFile(this, wide file name, playlist): the file's soundtrack decoded now */
METHOD(gb_RenderFile, 3)
{
    Graph *g = graph_of(ARG(0));
    uint32_t w = ARG(1);
    char name[600], host[2048];
    size_t i = 0;
    for (; w && i + 1 < sizeof name && R16(w + 2 * i); i++) name[i] = (char)R16(w + 2 * i);
    name[i] = 0;
    if (!path_to_host(name, host, sizeof host, 0)) {
        LOG(LOG_ALWAYS, "DirectShow: RenderFile(\"%s\"): no such file", name);
        return VFW_E_NOT_FOUND;
    }
    pthread_mutex_lock(&gr_lock);
    stop_voice(g);
    free(g->pcm);
    g->pcm = NULL;
    snprintf(g->file, sizeof g->file, "%s", name);
    snprintf(g->host, sizeof g->host, "%s", host);
    pthread_mutex_unlock(&gr_lock);
    Graph tmp = {0};
    if (!decode_file(&tmp, host)) {
        LOG(LOG_ALWAYS, "DirectShow: RenderFile(\"%s\"): cannot decode it", name);
        return E_FAIL;
    }
    pthread_mutex_lock(&gr_lock);
    g->pcm = tmp.pcm;
    g->frames = tmp.frames;
    g->channels = tmp.channels;
    g->rate = tmp.rate;
    g->width = tmp.width;
    g->height = tmp.height;
    g->duration = tmp.duration;
    g->pos_at_run = 0;
    g->state = 0;
    g->complete_sent = 0;
    pthread_mutex_unlock(&gr_lock);
    LOG(LOG_ALWAYS, "DirectShow: %s, %dx%d, %.1f s, sound %d ch %d Hz", name, g->width, g->height,
        g->duration / 1e7, g->channels, g->rate);
    return S_OK;
}

/* IMediaControl */
UNK(mc) DISP(mc)
METHOD(mc_Run, 1)
{
    Graph *g = graph_of(ARG(0));
    pthread_mutex_lock(&gr_lock);
    if (g->state != 2) {
        stop_voice(g);
        if (g->pcm && !deterministic && g->pos_at_run == 0)
            g->voice = sound_stream_start(g->pcm, g->frames, g->channels, g->rate);
        g->run_ms = now_ms();
        g->complete_sent = 0;
    }
    g->state = 2;
    graph_playing = 1;
    /* option intro_movies = on (default) | off: the logo films at start skipped */
    skip_wanted = !option_bool("intro_movies", 1);
    pthread_mutex_unlock(&gr_lock);
    if (pic.g != g && g->width > 0 && !skip_wanted)
        pic_open(g);
    LOG(LOG_ALWAYS, "DirectShow: Run %s", g->file);
    return S_OK;
}
METHOD(mc_Pause, 1)
{
    Graph *g = graph_of(ARG(0));
    pthread_mutex_lock(&gr_lock);
    g->pos_at_run = position(g);
    stop_voice(g);
    g->state = 1;
    pthread_mutex_unlock(&gr_lock);
    return S_OK;
}
METHOD(mc_Stop, 1)
{
    Graph *g = graph_of(ARG(0));
    pthread_mutex_lock(&gr_lock);
    g->pos_at_run = position(g);
    stop_voice(g);
    g->state = 0;
    graph_playing = 0;
    pthread_mutex_unlock(&gr_lock);
    if (pic.g == g)
        pic_close();
    LOG(LOG_ALWAYS, "DirectShow: Stop %s at %.1f s", g->file, g->pos_at_run / 1e7);
    return S_OK;
}
METHOD(mc_GetState, 3)
{
    Graph *g = graph_of(ARG(0));
    if (ARG(2)) W32(ARG(2), (uint32_t)g->state);
    return S_OK;
}
NOTIMPL(mc, RenderFile, 2) NOTIMPL(mc, AddSourceFilter, 3) NOTIMPL(mc, get_FilterCollection, 2)
NOTIMPL(mc, get_RegFilterCollection, 2) OK(mc, StopWhenReady, 1)

/* IMediaEvent(Ex) */
UNK(me) DISP(me)
GET0(me, GetEventHandle)
/* GetEvent(this, code*, param1*, param2*, timeout): EC_COMPLETE once a running film has ended */
METHOD(me_GetEvent, 5)
{
    Graph *g = graph_of(ARG(0));
    int done = 0;
    pthread_mutex_lock(&gr_lock);
    if (g->state == 2 && !g->complete_sent && position(g) >= g->duration) {
        done = 1;
        g->complete_sent = 1;
    }
    pthread_mutex_unlock(&gr_lock);
    if (!done)
        return E_ABORT;
    if (ARG(1)) W32(ARG(1), EC_COMPLETE);
    if (ARG(2)) W32(ARG(2), 0);
    if (ARG(3)) W32(ARG(3), 0);
    return S_OK;
}
METHOD(me_WaitForCompletion, 3) { if (ARG(2)) W32(ARG(2), 0); return E_ABORT; }
OK(me, CancelDefaultHandling, 2) OK(me, RestoreDefaultHandling, 2) OK(me, FreeEventParams, 4)
OK(me, SetNotifyWindow, 4) OK(me, SetNotifyFlags, 2) GET0(me, GetNotifyFlags)

/* IMediaSeeking: positions in 100 ns units (TIME_FORMAT_MEDIA_TIME, the default) */
UNK(ms)
GET0(ms, GetCapabilities) OK(ms, CheckCapabilities, 2) OK(ms, IsFormatSupported, 2)
NOTIMPL(ms, QueryPreferredFormat, 2) NOTIMPL(ms, GetTimeFormat, 2) OK(ms, IsUsingTimeFormat, 2)
OK(ms, SetTimeFormat, 2) NOTIMPL(ms, ConvertTimeFormat, 6) NOTIMPL(ms, GetAvailable, 3)
OK(ms, SetRate, 3) NOTIMPL(ms, GetRate, 2) NOTIMPL(ms, GetPreroll, 2)
static void w64(uint32_t a, int64_t v) { if (a) { W32(a, (uint32_t)v); W32(a + 4, (uint32_t)((uint64_t)v >> 32)); } }
METHOD(ms_GetDuration, 2) { Graph *g = graph_of(ARG(0)); w64(ARG(1), g->duration); return S_OK; }
METHOD(ms_GetStopPosition, 2) { Graph *g = graph_of(ARG(0)); w64(ARG(1), g->duration); return S_OK; }
METHOD(ms_GetCurrentPosition, 2)
{
    Graph *g = graph_of(ARG(0));
    pthread_mutex_lock(&gr_lock);
    int64_t p = position(g);
    pthread_mutex_unlock(&gr_lock);
    w64(ARG(1), p);
    return S_OK;
}
/* GetPositions(this, current*, stop*): the game's end-of-film test */
METHOD(ms_GetPositions, 3)
{
    Graph *g = graph_of(ARG(0));
    pthread_mutex_lock(&gr_lock);
    int64_t p = position(g);
    pthread_mutex_unlock(&gr_lock);
    w64(ARG(1), p);
    w64(ARG(2), g->duration);
    return S_OK;
}
/* SetPositions(this, current*, current flags, stop*, stop flags): only rewinding is used */
METHOD(ms_SetPositions, 5)
{
    Graph *g = graph_of(ARG(0));
    if (ARG(1) && (ARG(2) & 3) == 1) {                      /* AM_SEEKING_AbsolutePositioning */
        pthread_mutex_lock(&gr_lock);
        g->pos_at_run = (int64_t)((uint64_t)R32(ARG(1)) | ((uint64_t)R32(ARG(1) + 4) << 32));
        g->run_ms = now_ms();
        pthread_mutex_unlock(&gr_lock);
    }
    return S_OK;
}

/* IVideoWindow: the picture's window is the game's (one host window) */
UNK(vw) DISP(vw)
OK(vw, put_Caption, 2) GET0(vw, get_Caption) OK(vw, put_WindowStyle, 2) GET0(vw, get_WindowStyle)
OK(vw, put_WindowStyleEx, 2) GET0(vw, get_WindowStyleEx) OK(vw, put_AutoShow, 2) GET0(vw, get_AutoShow)
OK(vw, put_WindowState, 2) GET0(vw, get_WindowState) OK(vw, put_BackgroundPalette, 2)
GET0(vw, get_BackgroundPalette) OK(vw, put_Visible, 2) GET0(vw, get_Visible) OK(vw, put_Left, 2)
GET0(vw, get_Left) OK(vw, put_Width, 2) GET0(vw, get_Width) OK(vw, put_Top, 2) GET0(vw, get_Top)
OK(vw, put_Height, 2) GET0(vw, get_Height) OK(vw, put_Owner, 2) GET0(vw, get_Owner)
OK(vw, put_MessageDrain, 2) GET0(vw, get_MessageDrain) GET0(vw, get_BorderColor) OK(vw, put_BorderColor, 2)
GET0(vw, get_FullScreenMode) OK(vw, put_FullScreenMode, 2) OK(vw, SetWindowForeground, 2)
OK(vw, NotifyOwnerMessage, 5) OK(vw, SetWindowPosition, 5) NOTIMPL(vw, GetWindowPosition, 5)
NOTIMPL(vw, GetMinIdealImageSize, 3) NOTIMPL(vw, GetMaxIdealImageSize, 3) NOTIMPL(vw, GetRestorePosition, 5)
OK(vw, HideCursor, 2) GET0(vw, IsCursorHidden)

/* IBasicVideo */
UNK(bv) DISP(bv)
NOTIMPL(bv, get_AvgTimePerFrame, 2) NOTIMPL(bv, get_BitRate, 2) NOTIMPL(bv, get_BitErrorRate, 2)
METHOD(bv_get_VideoWidth, 2) { if (ARG(1)) W32(ARG(1), (uint32_t)graph_of(ARG(0))->width); return S_OK; }
METHOD(bv_get_VideoHeight, 2) { if (ARG(1)) W32(ARG(1), (uint32_t)graph_of(ARG(0))->height); return S_OK; }
OK(bv, put_SourceLeft, 2) GET0(bv, get_SourceLeft) OK(bv, put_SourceWidth, 2) GET0(bv, get_SourceWidth)
OK(bv, put_SourceTop, 2) GET0(bv, get_SourceTop) OK(bv, put_SourceHeight, 2) GET0(bv, get_SourceHeight)
OK(bv, put_DestinationLeft, 2) GET0(bv, get_DestinationLeft) OK(bv, put_DestinationWidth, 2)
GET0(bv, get_DestinationWidth) OK(bv, put_DestinationTop, 2) GET0(bv, get_DestinationTop)
OK(bv, put_DestinationHeight, 2) GET0(bv, get_DestinationHeight) OK(bv, SetSourcePosition, 5)
NOTIMPL(bv, GetSourcePosition, 5) OK(bv, SetDefaultSourcePosition, 1) OK(bv, SetDestinationPosition, 5)
NOTIMPL(bv, GetDestinationPosition, 5) OK(bv, SetDefaultDestinationPosition, 1)
/* GetVideoSize(this, width*, height*) */
METHOD(bv_GetVideoSize, 3)
{
    Graph *g = graph_of(ARG(0));
    if (ARG(1)) W32(ARG(1), (uint32_t)g->width);
    if (ARG(2)) W32(ARG(2), (uint32_t)g->height);
    return S_OK;
}
NOTIMPL(bv, GetVideoPaletteEntries, 5) NOTIMPL(bv, GetCurrentImage, 3)
OK(bv, IsUsingDefaultSource, 1) OK(bv, IsUsingDefaultDestination, 1)

#define CLASS(cls, iface, ...) do { \
        static void (*const fns[])(CPU *) = {__VA_ARGS__}; \
        static const char *names[sizeof fns / sizeof *fns]; \
        for (size_t i = 0; i < sizeof fns / sizeof *fns; i++) names[i] = "method"; \
        com_class_init(&cls, iface, (int)(sizeof fns / sizeof *fns), fns, names); \
    } while (0)

static void classes_init(void)
{
    CLASS(cls_gb, "IGraphBuilder", gb_QueryInterface, gb_AddRef, gb_Release, gb_AddFilter, gb_RemoveFilter,
          gb_EnumFilters, gb_FindFilterByName, gb_ConnectDirect, gb_Reconnect, gb_Disconnect,
          gb_SetDefaultSyncSource, gb_Connect, gb_Render, gb_RenderFile, gb_AddSourceFilter, gb_SetLogFile,
          gb_Abort, gb_ShouldOperationContinue);
    CLASS(cls_mc, "IMediaControl", mc_QueryInterface, mc_AddRef, mc_Release, mc_GetTypeInfoCount, mc_GetTypeInfo,
          mc_GetIDsOfNames, mc_Invoke, mc_Run, mc_Pause, mc_Stop, mc_GetState, mc_RenderFile, mc_AddSourceFilter,
          mc_get_FilterCollection, mc_get_RegFilterCollection, mc_StopWhenReady);
    CLASS(cls_me, "IMediaEventEx", me_QueryInterface, me_AddRef, me_Release, me_GetTypeInfoCount, me_GetTypeInfo,
          me_GetIDsOfNames, me_Invoke, me_GetEventHandle, me_GetEvent, me_WaitForCompletion,
          me_CancelDefaultHandling, me_RestoreDefaultHandling, me_FreeEventParams, me_SetNotifyWindow,
          me_SetNotifyFlags, me_GetNotifyFlags);
    CLASS(cls_ms, "IMediaSeeking", ms_QueryInterface, ms_AddRef, ms_Release, ms_GetCapabilities,
          ms_CheckCapabilities, ms_IsFormatSupported, ms_QueryPreferredFormat, ms_GetTimeFormat,
          ms_IsUsingTimeFormat, ms_SetTimeFormat, ms_GetDuration, ms_GetStopPosition, ms_GetCurrentPosition,
          ms_ConvertTimeFormat, ms_SetPositions, ms_GetPositions, ms_GetAvailable, ms_SetRate, ms_GetRate,
          ms_GetPreroll);
    CLASS(cls_vw, "IVideoWindow", vw_QueryInterface, vw_AddRef, vw_Release, vw_GetTypeInfoCount, vw_GetTypeInfo,
          vw_GetIDsOfNames, vw_Invoke, vw_put_Caption, vw_get_Caption, vw_put_WindowStyle, vw_get_WindowStyle,
          vw_put_WindowStyleEx, vw_get_WindowStyleEx, vw_put_AutoShow, vw_get_AutoShow, vw_put_WindowState,
          vw_get_WindowState, vw_put_BackgroundPalette, vw_get_BackgroundPalette, vw_put_Visible, vw_get_Visible,
          vw_put_Left, vw_get_Left, vw_put_Width, vw_get_Width, vw_put_Top, vw_get_Top, vw_put_Height,
          vw_get_Height, vw_put_Owner, vw_get_Owner, vw_put_MessageDrain, vw_get_MessageDrain,
          vw_get_BorderColor, vw_put_BorderColor, vw_get_FullScreenMode, vw_put_FullScreenMode,
          vw_SetWindowForeground, vw_NotifyOwnerMessage, vw_SetWindowPosition, vw_GetWindowPosition,
          vw_GetMinIdealImageSize, vw_GetMaxIdealImageSize, vw_GetRestorePosition, vw_HideCursor,
          vw_IsCursorHidden);
    CLASS(cls_bv, "IBasicVideo", bv_QueryInterface, bv_AddRef, bv_Release, bv_GetTypeInfoCount, bv_GetTypeInfo,
          bv_GetIDsOfNames, bv_Invoke, bv_get_AvgTimePerFrame, bv_get_BitRate, bv_get_BitErrorRate,
          bv_get_VideoWidth, bv_get_VideoHeight, bv_put_SourceLeft, bv_get_SourceLeft, bv_put_SourceWidth,
          bv_get_SourceWidth, bv_put_SourceTop, bv_get_SourceTop, bv_put_SourceHeight, bv_get_SourceHeight,
          bv_put_DestinationLeft, bv_get_DestinationLeft, bv_put_DestinationWidth, bv_get_DestinationWidth,
          bv_put_DestinationTop, bv_get_DestinationTop, bv_put_DestinationHeight, bv_get_DestinationHeight,
          bv_SetSourcePosition, bv_GetSourcePosition, bv_SetDefaultSourcePosition, bv_SetDestinationPosition,
          bv_GetDestinationPosition, bv_SetDefaultDestinationPosition, bv_GetVideoSize,
          bv_GetVideoPaletteEntries, bv_GetCurrentImage, bv_IsUsingDefaultSource, bv_IsUsingDefaultDestination);
}

/* For ole32.c's CoCreateInstance: CLSID_FilterGraph {e436ebb3-524f-11ce-9f53-0020af0ba770} with
   any of its interfaces; REGDB_E_CLASSNOTREG for any other class */
uint32_t video_cocreate(uint32_t clsid, uint32_t iid, uint32_t ppv)
{
    static const uint8_t fg[16] = {0xb3, 0xeb, 0x36, 0xe4, 0x4f, 0x52, 0xce, 0x11, 0x9f, 0x53,
                                   0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70};
    if (!clsid || memcmp(gptr(clsid), fg, 16))
        return 0x80040154u;
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, classes_init);
    Graph *g = calloc(1, sizeof *g);
    g->voice = -1;
    g->obj_gb = com_new(&cls_gb, g);
    g->obj_mc = com_new(&cls_mc, g);
    g->obj_me = com_new(&cls_me, g);
    g->obj_ms = com_new(&cls_ms, g);
    g->obj_vw = com_new(&cls_vw, g);
    g->obj_bv = com_new(&cls_bv, g);
    g->refs = 0;
    return qi(g->obj_gb, iid, ppv);
}
