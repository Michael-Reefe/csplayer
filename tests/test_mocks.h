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
extern bool g_mock_SDL_CreateTexture_fail;
extern bool g_mock_MIX_LoadRawAudioNoCopy_fail;
extern bool g_mock_MIX_SetTrackRawCallback_fail;
extern bool g_mock_MIX_CreateAudioDecoder_fail;
extern bool g_mock_MIX_TagTrack_fail;
extern bool g_mock_MIX_PlayTag_fail;
extern bool g_mock_SDL_CreateDirectory_fail;
extern bool g_mock_SDL_CreateProcess_fail;

/* ── Sticky mode flags (NOT one-shot; the test clears them) ──────────────────
 * The flags above are one-shot failure injections.  These instead put a stub
 * into a different mode for as long as they are set, which is what lets the
 * stems job lifecycle be driven deterministically with no real child process. */

/* Return a fake process handle from SDL_CreateProcessWithProperties instead of
 * spawning anything; the wait/kill/destroy stubs recognise that handle. */
extern bool g_mock_process_fake;
/* What the fake handle's non-blocking SDL_WaitProcess reports. */
extern bool g_mock_process_exited;
extern int  g_mock_process_exitcode;
/* Make SDL_GetPathInfo report the sidecar engine and model as installed, so the
 * job path is reachable in a test tree that has neither.  Deliberately narrow:
 * only those two filenames are faked, so cache-completeness checks still see
 * the real filesystem. */
extern bool g_mock_stems_skip_tools;
/* Like the above but fakes only the engine, so the model check fails — the
 * state a user is in before downloading the weights. */
extern bool g_mock_stems_engine_only;
/* Make SDL_GetBasePath report failure, so asset-path resolution can be tested. */
extern bool g_mock_SDL_GetBasePath_null;

/*
 * Stage text as the fake child's stdout.  SDL_GetProcessOutput then hands back
 * a readable stream over it, consumed once like a real pipe, so the progress
 * drain path can be driven deterministically.  Pass NULL to clear.
 */
void csp_mock_stage_process_output(const char *text);

/* ── Test seams into audio.c (defined there under CSPLAYER_TESTING) ──────────
 * The PCM capture callbacks only ever run on SDL's audio thread, which the
 * dummy driver never drives, so these invoke them directly. */
struct audio_s;
void csp_test_feed_raw(struct audio_s *a, const float *pcm, int samples,
                        int channels);
void csp_test_feed_stem(struct audio_s *a, int idx, const float *pcm,
                         int samples, int channels);

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
SDL_Texture      *csp_mock_SDL_CreateTexture(SDL_Renderer *renderer,
                                              SDL_PixelFormat format,
                                              SDL_TextureAccess access,
                                              int w, int h);
MIX_Audio        *csp_mock_MIX_LoadRawAudioNoCopy(MIX_Mixer *mixer,
                                                   const void *data,
                                                   size_t datalen,
                                                   const SDL_AudioSpec *spec,
                                                   bool free_when_done);
bool              csp_mock_MIX_SetTrackRawCallback(MIX_Track *track,
                                                    MIX_TrackMixCallback cb,
                                                    void *userdata);
MIX_AudioDecoder *csp_mock_MIX_CreateAudioDecoder(const char *path,
                                                   SDL_PropertiesID props);
bool              csp_mock_MIX_TagTrack(MIX_Track *track, const char *tag);
bool              csp_mock_MIX_PlayTag(MIX_Mixer *mixer, const char *tag,
                                        SDL_PropertiesID options);
bool              csp_mock_SDL_CreateDirectory(const char *path);
bool              csp_mock_SDL_GetPathInfo(const char *path, SDL_PathInfo *info);
SDL_Process      *csp_mock_SDL_CreateProcessWithProperties(SDL_PropertiesID props);
SDL_IOStream     *csp_mock_SDL_GetProcessOutput(SDL_Process *process);
const char       *csp_mock_SDL_GetBasePath(void);
bool              csp_mock_SDL_WaitProcess(SDL_Process *process, bool block,
                                            int *exitcode);
bool              csp_mock_SDL_KillProcess(SDL_Process *process, bool force);
void              csp_mock_SDL_DestroyProcess(SDL_Process *process);

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
#define SDL_CreateTexture                csp_mock_SDL_CreateTexture
#define MIX_LoadRawAudioNoCopy           csp_mock_MIX_LoadRawAudioNoCopy
#define MIX_SetTrackRawCallback          csp_mock_MIX_SetTrackRawCallback
#define MIX_CreateAudioDecoder           csp_mock_MIX_CreateAudioDecoder
#define MIX_TagTrack                     csp_mock_MIX_TagTrack
#define MIX_PlayTag                      csp_mock_MIX_PlayTag
#define SDL_CreateDirectory              csp_mock_SDL_CreateDirectory
#define SDL_GetPathInfo                  csp_mock_SDL_GetPathInfo
#define SDL_CreateProcessWithProperties  csp_mock_SDL_CreateProcessWithProperties
#define SDL_GetProcessOutput             csp_mock_SDL_GetProcessOutput
#define SDL_GetBasePath                  csp_mock_SDL_GetBasePath
#define SDL_WaitProcess                  csp_mock_SDL_WaitProcess
#define SDL_KillProcess                  csp_mock_SDL_KillProcess
#define SDL_DestroyProcess               csp_mock_SDL_DestroyProcess

#endif /* CSPLAYER_TEST_MOCKS_H */
