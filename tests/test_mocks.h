/*
 * test_mocks.h — Compile-time SDL call interception for branch coverage.
 *
 * Included only when CSPLAYER_TESTING is defined (audio.c, ui.c).
 * Each SDL function that guards an error branch is redirected to a stub in
 * test_mocks.c.  The stub checks a per-call "fail-next" flag; when set, it
 * clears the flag and returns a failure sentinel.  Otherwise it forwards to
 * the real SDL symbol.
 *
 * test_mocks.c must NOT include this header — it calls the real SDL names
 * directly to avoid circular macro expansion.
 */

#ifndef CSPLAYER_TEST_MOCKS_H
#define CSPLAYER_TEST_MOCKS_H

#include <stdbool.h>
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>

/* ── Per-call fail-next flags ────────────────────────────────────────────── */

extern bool g_mock_MIX_Init_fail;
extern bool g_mock_MIX_CreateMixerDevice_fail;
extern bool g_mock_MIX_CreateTrack_fail;
extern bool g_mock_SDL_CreateMutex_fail;
extern bool g_mock_MIX_LoadAudio_fail;
extern bool g_mock_MIX_SetTrackAudio_fail;
extern bool g_mock_MIX_ResumeTrack_fail;
extern bool g_mock_SDL_CreateProperties_fail;
extern bool g_mock_MIX_PlayTrack_fail;
extern bool g_mock_MIX_PauseTrack_fail;
extern bool g_mock_MIX_StopTrack_fail;
extern bool g_mock_MIX_SetTrackPlaybackPosition_fail;
extern bool g_mock_MIX_GetTrackPlaybackPosition_neg;   /* return -1 */
extern bool g_mock_MIX_GetTrackPlaybackPosition_over;  /* return duration+1 */
extern bool g_mock_TTF_RenderText_Solid_fail;
extern bool g_mock_SDL_CreateTextureFromSurface_fail;

/* ── Stub declarations ───────────────────────────────────────────────────── */

bool              csp_mock_MIX_Init(void);
MIX_Mixer        *csp_mock_MIX_CreateMixerDevice(SDL_AudioDeviceID dev,
                                                   const SDL_AudioSpec *spec);
MIX_Track        *csp_mock_MIX_CreateTrack(MIX_Mixer *mixer);
SDL_Mutex        *csp_mock_SDL_CreateMutex(void);
MIX_Audio        *csp_mock_MIX_LoadAudio(MIX_Mixer *mixer,
                                          const char *path, bool predecode);
bool              csp_mock_MIX_SetTrackAudio(MIX_Track *track,
                                              MIX_Audio *audio);
bool              csp_mock_MIX_ResumeTrack(MIX_Track *track);
SDL_PropertiesID  csp_mock_SDL_CreateProperties(void);
bool              csp_mock_MIX_PlayTrack(MIX_Track *track,
                                          SDL_PropertiesID props);
bool              csp_mock_MIX_PauseTrack(MIX_Track *track);
bool              csp_mock_MIX_StopTrack(MIX_Track *track, int flags);
bool              csp_mock_MIX_SetTrackPlaybackPosition(MIX_Track *track,
                                                         Sint64 frame);
Sint64            csp_mock_MIX_GetTrackPlaybackPosition(MIX_Track *track);
SDL_Surface      *csp_mock_TTF_RenderText_Solid(TTF_Font *font,
                                                 const char *text,
                                                 size_t length,
                                                 SDL_Color fg);
SDL_Texture      *csp_mock_SDL_CreateTextureFromSurface(SDL_Renderer *renderer,
                                                         SDL_Surface *surface);

/* ── Macro redirections ──────────────────────────────────────────────────── */

#define MIX_Init                         csp_mock_MIX_Init
#define MIX_CreateMixerDevice            csp_mock_MIX_CreateMixerDevice
#define MIX_CreateTrack                  csp_mock_MIX_CreateTrack
#define SDL_CreateMutex                  csp_mock_SDL_CreateMutex
#define MIX_LoadAudio                    csp_mock_MIX_LoadAudio
#define MIX_SetTrackAudio                csp_mock_MIX_SetTrackAudio
#define MIX_ResumeTrack                  csp_mock_MIX_ResumeTrack
#define SDL_CreateProperties             csp_mock_SDL_CreateProperties
#define MIX_PlayTrack                    csp_mock_MIX_PlayTrack
#define MIX_PauseTrack                   csp_mock_MIX_PauseTrack
#define MIX_StopTrack                    csp_mock_MIX_StopTrack
#define MIX_SetTrackPlaybackPosition     csp_mock_MIX_SetTrackPlaybackPosition
#define MIX_GetTrackPlaybackPosition     csp_mock_MIX_GetTrackPlaybackPosition
#define TTF_RenderText_Solid             csp_mock_TTF_RenderText_Solid
#define SDL_CreateTextureFromSurface     csp_mock_SDL_CreateTextureFromSurface

#endif /* CSPLAYER_TEST_MOCKS_H */
