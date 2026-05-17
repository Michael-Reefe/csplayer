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
bool g_mock_MIX_LoadRawAudioNoCopy_fail          = false;
bool g_mock_MIX_SetTrackRawCallback_fail         = false;

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
