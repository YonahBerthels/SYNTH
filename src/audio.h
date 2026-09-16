#ifndef AUDIO_H
#define AUDIO_H

#include <SDL2/SDL.h>
#include <stdbool.h>

/* Owns the decoded track, the output device, and the streaming pump that
   feeds it through the synth chain. Also keeps a short history of processed
   audio for whoever wants to analyse what is actually being heard. */

bool audio_load(const char *path);   /* decode a WAV into memory */
bool audio_start(void);              /* (re)open the device and begin */
void audio_pump(void);               /* top the queue up; call once per frame */
void audio_shutdown(void);

void audio_toggle_pause(void);
bool audio_paused(void);
bool audio_ready(void);              /* a device is open */
bool audio_finished(void);           /* reached the end, not looping */

void audio_toggle_loop(void);
bool audio_looping(void);

/* Jump to a fraction (0..1) of the track. */
void audio_seek(double fraction);

int    audio_sample_rate(void);
Uint32 audio_total_frames(void);

/* Position in the source that is actually reaching the speakers: everything
   generated, minus what is still sitting in the device queue. */
Uint64 audio_audible_frames(void);

/* Copies the n processed mono samples ending at the audible position into
   dst, zero-padding when playback has not produced that many yet. */
void audio_history(float *dst, int n);

#endif /* AUDIO_H */
