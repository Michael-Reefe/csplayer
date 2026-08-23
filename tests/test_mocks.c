/*
 * test_mocks.c — SDL stub implementations for csplayer integration tests.
 *
 * Each stub checks a per-call "fail-next" flag (one-shot: clears on fire),
 * then either returns a failure sentinel or forwards to the real SDL symbol.
 *
 * This file must NOT include test_mocks.h — it calls the real SDL names
 * directly to avoid circular macro expansion.
 */

#include <stdbool.h>
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "types.h"   /* STEM_ENGINE_EXE / STEM_MODEL_FILE for the tools stub */

/* ── Fail-next flags (definitions) ──────────────────────────────────────── */

bool g_mock_MIX_Init_fail                        = false;
bool g_mock_MIX_CreateMixerDevice_fail           = false;
bool g_mock_MIX_CreateTrack_fail                 = false;
bool g_mock_SDL_CreateMutex_fail                 = false;
bool g_mock_MIX_LoadAudio_fail                   = false;
bool g_mock_MIX_SetTrackAudio_fail               = false;
bool g_mock_MIX_ResumeTrack_fail                 = false;
bool g_mock_SDL_CreateProperties_fail            = false;
bool g_mock_MIX_PlayTrack_fail                   = false;
bool g_mock_MIX_PauseTrack_fail                  = false;
bool g_mock_MIX_StopTrack_fail                   = false;
bool g_mock_MIX_SetTrackPlaybackPosition_fail    = false;
bool g_mock_MIX_GetTrackPlaybackPosition_neg     = false;
bool g_mock_MIX_GetTrackPlaybackPosition_over    = false;
bool g_mock_TTF_RenderText_Solid_fail            = false;
bool g_mock_SDL_CreateTextureFromSurface_fail    = false;
bool g_mock_SDL_CreateTexture_fail               = false;
bool g_mock_MIX_LoadRawAudioNoCopy_fail          = false;
bool g_mock_MIX_SetTrackRawCallback_fail         = false;
bool g_mock_MIX_CreateAudioDecoder_fail          = false;
bool g_mock_MIX_TagTrack_fail                    = false;
bool g_mock_MIX_PlayTag_fail                     = false;
bool g_mock_SDL_CreateDirectory_fail             = false;
bool g_mock_SDL_CreateProcess_fail               = false;

/* Sticky mode flags — see the note in test_mocks.h. */
bool g_mock_process_fake      = false;
bool g_mock_process_exited    = false;
int  g_mock_process_exitcode  = 0;
bool g_mock_stems_skip_tools  = false;
bool g_mock_stems_engine_only = false;
bool g_mock_SDL_GetBasePath_null = false;

/*
 * Sentinel handle handed out in place of a real child process.  Its address is
 * unique, so the wait/kill/destroy stubs can tell a faked job from a real one
 * and must never pass this pointer to SDL.
 */
static int s_fake_proc_token;
#define FAKE_PROC ((SDL_Process *)&s_fake_proc_token)

/* ── Stub implementations ────────────────────────────────────────────────── */

bool csp_mock_MIX_Init(void)
{
    if (g_mock_MIX_Init_fail) { g_mock_MIX_Init_fail = false; return false; }
    return MIX_Init();
}

MIX_Mixer *csp_mock_MIX_CreateMixerDevice(SDL_AudioDeviceID dev,
                                            const SDL_AudioSpec *spec)
{
    if (g_mock_MIX_CreateMixerDevice_fail) {
        g_mock_MIX_CreateMixerDevice_fail = false;
        return NULL;
    }
    return MIX_CreateMixerDevice(dev, spec);
}

MIX_Track *csp_mock_MIX_CreateTrack(MIX_Mixer *mixer)
{
    if (g_mock_MIX_CreateTrack_fail) {
        g_mock_MIX_CreateTrack_fail = false;
        return NULL;
    }
    return MIX_CreateTrack(mixer);
}

SDL_Mutex *csp_mock_SDL_CreateMutex(void)
{
    if (g_mock_SDL_CreateMutex_fail) {
        g_mock_SDL_CreateMutex_fail = false;
        return NULL;
    }
    return SDL_CreateMutex();
}

MIX_Audio *csp_mock_MIX_LoadAudio(MIX_Mixer *mixer,
                                    const char *path, bool predecode)
{
    if (g_mock_MIX_LoadAudio_fail) {
        g_mock_MIX_LoadAudio_fail = false;
        return NULL;
    }
    return MIX_LoadAudio(mixer, path, predecode);
}

bool csp_mock_MIX_SetTrackAudio(MIX_Track *track, MIX_Audio *audio)
{
    if (g_mock_MIX_SetTrackAudio_fail) {
        g_mock_MIX_SetTrackAudio_fail = false;
        return false;
    }
    return MIX_SetTrackAudio(track, audio);
}

bool csp_mock_MIX_ResumeTrack(MIX_Track *track)
{
    if (g_mock_MIX_ResumeTrack_fail) {
        g_mock_MIX_ResumeTrack_fail = false;
        return false;
    }
    return MIX_ResumeTrack(track);
}

SDL_PropertiesID csp_mock_SDL_CreateProperties(void)
{
    if (g_mock_SDL_CreateProperties_fail) {
        g_mock_SDL_CreateProperties_fail = false;
        return 0;
    }
    return SDL_CreateProperties();
}

bool csp_mock_MIX_PlayTrack(MIX_Track *track, SDL_PropertiesID props)
{
    if (g_mock_MIX_PlayTrack_fail) {
        g_mock_MIX_PlayTrack_fail = false;
        return false;
    }
    return MIX_PlayTrack(track, props);
}

bool csp_mock_MIX_PauseTrack(MIX_Track *track)
{
    if (g_mock_MIX_PauseTrack_fail) {
        g_mock_MIX_PauseTrack_fail = false;
        return false;
    }
    return MIX_PauseTrack(track);
}

bool csp_mock_MIX_StopTrack(MIX_Track *track, int flags)
{
    if (g_mock_MIX_StopTrack_fail) {
        g_mock_MIX_StopTrack_fail = false;
        return false;
    }
    return MIX_StopTrack(track, flags);
}

bool csp_mock_MIX_SetTrackPlaybackPosition(MIX_Track *track, Sint64 frame)
{
    if (g_mock_MIX_SetTrackPlaybackPosition_fail) {
        g_mock_MIX_SetTrackPlaybackPosition_fail = false;
        return false;
    }
    return MIX_SetTrackPlaybackPosition(track, frame);
}

Sint64 csp_mock_MIX_GetTrackPlaybackPosition(MIX_Track *track)
{
    if (g_mock_MIX_GetTrackPlaybackPosition_neg) {
        g_mock_MIX_GetTrackPlaybackPosition_neg = false;
        return -1;
    }
    if (g_mock_MIX_GetTrackPlaybackPosition_over) {
        /* Return a value larger than any realistic duration_frames to force
         * pos > 1.0f in audio_get_pos. */
        g_mock_MIX_GetTrackPlaybackPosition_over = false;
        return (Sint64)0x7FFFFFFFFFFFFFFF;
    }
    return MIX_GetTrackPlaybackPosition(track);
}

SDL_Surface *csp_mock_TTF_RenderText_Solid(TTF_Font *font,
                                            const char *text,
                                            size_t length,
                                            SDL_Color fg)
{
    if (g_mock_TTF_RenderText_Solid_fail) {
        g_mock_TTF_RenderText_Solid_fail = false;
        return NULL;
    }
    return TTF_RenderText_Solid(font, text, length, fg);
}

SDL_Texture *csp_mock_SDL_CreateTextureFromSurface(SDL_Renderer *renderer,
                                                    SDL_Surface *surface)
{
    if (g_mock_SDL_CreateTextureFromSurface_fail) {
        g_mock_SDL_CreateTextureFromSurface_fail = false;
        return NULL;
    }
    return SDL_CreateTextureFromSurface(renderer, surface);
}

SDL_Texture *csp_mock_SDL_CreateTexture(SDL_Renderer *renderer,
                                         SDL_PixelFormat format,
                                         SDL_TextureAccess access,
                                         int w, int h)
{
    if (g_mock_SDL_CreateTexture_fail) {
        g_mock_SDL_CreateTexture_fail = false;
        return NULL;
    }
    return SDL_CreateTexture(renderer, format, access, w, h);
}

MIX_Audio *csp_mock_MIX_LoadRawAudioNoCopy(MIX_Mixer *mixer, const void *data,
                                             size_t datalen,
                                             const SDL_AudioSpec *spec,
                                             bool free_when_done)
{
    if (g_mock_MIX_LoadRawAudioNoCopy_fail) {
        g_mock_MIX_LoadRawAudioNoCopy_fail = false;
        return NULL;
    }
    return MIX_LoadRawAudioNoCopy(mixer, data, datalen, spec, free_when_done);
}

bool csp_mock_MIX_SetTrackRawCallback(MIX_Track *track,
                                       MIX_TrackMixCallback cb, void *userdata)
{
    if (g_mock_MIX_SetTrackRawCallback_fail) {
        g_mock_MIX_SetTrackRawCallback_fail = false;
        return false;
    }
    return MIX_SetTrackRawCallback(track, cb, userdata);
}

MIX_AudioDecoder *csp_mock_MIX_CreateAudioDecoder(const char *path,
                                                   SDL_PropertiesID props)
{
    if (g_mock_MIX_CreateAudioDecoder_fail) {
        g_mock_MIX_CreateAudioDecoder_fail = false;
        return NULL;
    }
    return MIX_CreateAudioDecoder(path, props);
}

bool csp_mock_MIX_TagTrack(MIX_Track *track, const char *tag)
{
    if (g_mock_MIX_TagTrack_fail) {
        g_mock_MIX_TagTrack_fail = false;
        return false;
    }
    return MIX_TagTrack(track, tag);
}

bool csp_mock_MIX_PlayTag(MIX_Mixer *mixer, const char *tag,
                           SDL_PropertiesID options)
{
    if (g_mock_MIX_PlayTag_fail) {
        g_mock_MIX_PlayTag_fail = false;
        return false;
    }
    return MIX_PlayTag(mixer, tag, options);
}

/* ── Filesystem / process stubs ──────────────────────────────────────────── */

bool csp_mock_SDL_CreateDirectory(const char *path)
{
    if (g_mock_SDL_CreateDirectory_fail) {
        g_mock_SDL_CreateDirectory_fail = false;
        return false;
    }
    return SDL_CreateDirectory(path);
}

bool csp_mock_SDL_GetPathInfo(const char *path, SDL_PathInfo *info)
{
    /* Pretend the sidecar assets exist, so a job can be started in a tree
     * without them while every other stat still hits the real disk.  With
     * engine_only the model stays "missing", which is how a user who has not
     * downloaded the weights sees it. */
    bool fake_engine = (g_mock_stems_skip_tools || g_mock_stems_engine_only);
    if (path != NULL &&
        ((fake_engine             && SDL_strstr(path, STEM_ENGINE_EXE) != NULL) ||
         (g_mock_stems_skip_tools && SDL_strstr(path, STEM_MODEL_FILE) != NULL))) {
        if (info != NULL) {
            SDL_memset(info, 0, sizeof(*info));
            info->type = SDL_PATHTYPE_FILE;
            info->size = 1;
        }
        return true;
    }
    return SDL_GetPathInfo(path, info);
}

SDL_Process *csp_mock_SDL_CreateProcessWithProperties(SDL_PropertiesID props)
{
    if (g_mock_SDL_CreateProcess_fail) {
        g_mock_SDL_CreateProcess_fail = false;
        return NULL;
    }
    if (g_mock_process_fake) { return FAKE_PROC; }
    return SDL_CreateProcessWithProperties(props);
}

/* Staged stdout for the fake child, exposed as a single consume-once stream so
 * it behaves like the real pipe (a second drain finds nothing). */
static SDL_IOStream *s_fake_out;

void csp_mock_stage_process_output(const char *text)
{
    if (s_fake_out != NULL) {
        (void)SDL_CloseIO(s_fake_out);
        s_fake_out = NULL;
    }
    if (text != NULL) {
        s_fake_out = SDL_IOFromConstMem(text, SDL_strlen(text));
    }
}

const char *csp_mock_SDL_GetBasePath(void)
{
    if (g_mock_SDL_GetBasePath_null) {
        g_mock_SDL_GetBasePath_null = false;
        return NULL;
    }
    return SDL_GetBasePath();
}

SDL_IOStream *csp_mock_SDL_GetProcessOutput(SDL_Process *process)
{
    if (process == FAKE_PROC) { return s_fake_out; }
    return SDL_GetProcessOutput(process);
}

bool csp_mock_SDL_WaitProcess(SDL_Process *process, bool block, int *exitcode)
{
    if (process == FAKE_PROC) {
        if (exitcode != NULL) { *exitcode = g_mock_process_exitcode; }
        /* A blocking wait on a fake child returns immediately (it is already
         * "reaped"); a poll reports whatever the test has staged. */
        return block ? true : g_mock_process_exited;
    }
    return SDL_WaitProcess(process, block, exitcode);
}

bool csp_mock_SDL_KillProcess(SDL_Process *process, bool force)
{
    if (process == FAKE_PROC) { return true; }
    return SDL_KillProcess(process, force);
}

void csp_mock_SDL_DestroyProcess(SDL_Process *process)
{
    if (process == FAKE_PROC) { return; }
    SDL_DestroyProcess(process);
}
