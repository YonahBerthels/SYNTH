#include "audio.h"
#include "synth.h"

#include <stdio.h>
#include <string.h>

/* Audio is synthesised a chunk at a time and queued just ahead of the device,
   so a knob move is audible within roughly QUEUE_TARGET_FRAMES / rate. Too
   small and the queue starves into crackling; too large and knobs feel laggy. */
#define CHUNK_FRAMES 512
#define QUEUE_TARGET_FRAMES 6144

/* History of processed mono audio. Power of two; must exceed
   QUEUE_TARGET_FRAMES plus the largest analysis window anyone will ask for,
   or audio_history would read frames that have already been overwritten. */
#define RING_FRAMES 16384

/* the decoded track: raw interleaved PCM, in track_spec's format */
static SDL_AudioSpec track_spec;
static Uint8 *track_buf = NULL;
static Uint32 track_len = 0;
static Uint32 track_frames = 0;
static int frame_bytes = 0;      /* bytes per source frame, all channels */
static int out_frame_bytes = 0;  /* bytes per output frame (float, all channels) */

static SDL_AudioDeviceID audio_dev = 0;
static bool paused = false;
static bool looping = false;
static Uint32 play_head = 0;     /* next source frame to synthesise */

/* ring_write counts frames ever written; it indexes the history ring and is
   deliberately independent of play_head, which can jump on a seek or a loop */
static float ring[RING_FRAMES];
static Uint64 ring_write = 0;

/* One channel of one frame, normalised to [-1, 1]. WAV is always
   little-endian, so only the LSB variants are handled; audio_load rejects
   anything else before we get here. */
static float sample_at(Uint32 frame, int c) {
    Uint32 i = frame * (Uint32)track_spec.channels + (Uint32)c;

    switch (track_spec.format) {
    case AUDIO_U8:     return (((Uint8 *)track_buf)[i] - 128) / 128.0f;
    case AUDIO_S8:     return ((Sint8 *)track_buf)[i] / 128.0f;
    case AUDIO_S16LSB: return ((Sint16 *)track_buf)[i] / 32768.0f;
    case AUDIO_S32LSB: return ((Sint32 *)track_buf)[i] / 2147483648.0f;
    case AUDIO_F32LSB: return ((float *)track_buf)[i];
    }
    return 0.0f;
}

static void free_track(void) {
    if (track_buf) {
        SDL_FreeWAV(track_buf);
        track_buf = NULL;
    }
    track_len = 0;
    track_frames = 0;
}

/* Decodes a WAV into memory. SDL_LoadWAV walks the RIFF chunks for us, so we
   never touch the header layout ourselves. */
bool audio_load(const char *path) {
    free_track();

    if (SDL_LoadWAV(path, &track_spec, &track_buf, &track_len) == NULL) {
        fprintf(stderr, "SDL_LoadWAV(%s): %s\n", path, SDL_GetError());
        return false;
    }

    switch (track_spec.format) {
    case AUDIO_U8: case AUDIO_S8:
    case AUDIO_S16LSB: case AUDIO_S32LSB: case AUDIO_F32LSB:
        break;
    default:
        fprintf(stderr, "unsupported sample format 0x%04x in %s\n",
                track_spec.format, path);
        free_track();
        return false;
    }

    if (track_spec.channels < 1 || track_spec.channels > SYNTH_MAX_CHANNELS) {
        fprintf(stderr, "%d channels in %s, at most %d supported\n",
                track_spec.channels, path, SYNTH_MAX_CHANNELS);
        free_track();
        return false;
    }

    frame_bytes = (SDL_AUDIO_BITSIZE(track_spec.format) / 8) * track_spec.channels;
    out_frame_bytes = (int)sizeof(float) * track_spec.channels;
    track_frames = track_len / (Uint32)frame_bytes;

    if (track_frames == 0) {
        fprintf(stderr, "%s contains no audio\n", path);
        free_track();
        return false;
    }

    return true;
}

bool audio_start(void) {
    /* close any device from a previously loaded track: the new one may have a
       different rate or channel count */
    if (audio_dev) {
        SDL_CloseAudioDevice(audio_dev);
        audio_dev = 0;
    }

    SDL_AudioSpec want = track_spec;

    /* Float output: the DSP chain works in floats, so this saves converting
       back to the source format on every chunk. SDL converts to whatever the
       hardware actually wants. */
    want.format = AUDIO_F32SYS;
    want.samples = CHUNK_FRAMES;
    want.callback = NULL;   /* queue-driven, no callback thread */

    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (audio_dev == 0) {
        fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return false;
    }

    /* Nothing is queued up front: audio_pump feeds the device a little at a
       time, which is what lets the knobs affect audio that has not been
       generated yet. */
    play_head = 0;
    ring_write = 0;
    paused = false;
    memset(ring, 0, sizeof ring);
    synth_clear();

    SDL_PauseAudioDevice(audio_dev, 0);
    return true;
}

void audio_pump(void) {
    static float out[CHUNK_FRAMES * SYNTH_MAX_CHANNELS];

    if (!audio_dev || paused) return;

    const int ch = track_spec.channels;
    const Uint32 target = QUEUE_TARGET_FRAMES * (Uint32)out_frame_bytes;

    while (SDL_GetQueuedAudioSize(audio_dev) < target) {
        if (play_head >= track_frames) {
            if (!looping) return;
            /* wrap without clearing the filter: resetting its state mid-signal
               would click */
            play_head = 0;
        }

        Uint32 n = CHUNK_FRAMES;
        if (play_head + n > track_frames) n = track_frames - play_head;

        /* Coefficients are recomputed per chunk rather than per sample: a
           chunk is ~12ms, far below the point where a knob sweep would
           audibly step. */
        synth_prepare((float)track_spec.freq);

        for (Uint32 i = 0; i < n; i++) {
            float *frame = &out[i * (Uint32)ch];

            for (int c = 0; c < ch; c++) frame[c] = sample_at(play_head + i, c);

            synth_process(frame, ch);

            float mono = 0.0f;
            for (int c = 0; c < ch; c++) mono += frame[c];

            /* analysis reads this, so the bars show the processed sound */
            ring[ring_write & (RING_FRAMES - 1)] = mono / ch;
            ring_write++;
        }

        SDL_QueueAudio(audio_dev, out, n * (Uint32)out_frame_bytes);
        play_head += n;
    }
}

void audio_shutdown(void) {
    if (audio_dev) {
        SDL_CloseAudioDevice(audio_dev);
        audio_dev = 0;
    }
    free_track();
}

void audio_toggle_pause(void) {
    if (!audio_dev) return;
    paused = !paused;
    SDL_PauseAudioDevice(audio_dev, paused);
}

bool audio_paused(void) { return paused; }
bool audio_ready(void) { return audio_dev != 0; }

bool audio_finished(void) {
    return audio_dev && !looping && play_head >= track_frames &&
           SDL_GetQueuedAudioSize(audio_dev) == 0;
}

void audio_toggle_loop(void) { looping = !looping; }
bool audio_looping(void) { return looping; }

void audio_seek(double fraction) {
    if (!audio_dev || track_frames == 0) return;

    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;

    /* Drop what was already generated, or the old position would keep playing
       for the length of the queue. */
    SDL_ClearQueuedAudio(audio_dev);
    play_head = (Uint32)(fraction * track_frames);

    /* The history now describes audio that will never be heard. Zeroing it
       makes the bars fall to silence rather than show a stale spectrum for
       the length of one analysis window. */
    memset(ring, 0, sizeof ring);
    synth_clear();
}

int audio_sample_rate(void) { return track_spec.freq; }
Uint32 audio_total_frames(void) { return track_frames; }

Uint64 audio_audible_frames(void) {
    if (!audio_dev) return 0;

    Uint32 queued = SDL_GetQueuedAudioSize(audio_dev) / (Uint32)out_frame_bytes;
    return (play_head > queued) ? play_head - queued : 0;
}

void audio_history(float *dst, int n) {
    if (!audio_dev) {
        memset(dst, 0, (size_t)n * sizeof *dst);
        return;
    }

    Uint32 queued = SDL_GetQueuedAudioSize(audio_dev) / (Uint32)out_frame_bytes;
    Uint64 heard = (ring_write > queued) ? ring_write - queued : 0;
    Uint64 start = (heard > (Uint64)n) ? heard - (Uint64)n : 0;

    for (int i = 0; i < n; i++) {
        Uint64 f = start + (Uint64)i;
        dst[i] = (f < ring_write) ? ring[f & (RING_FRAMES - 1)] : 0.0f;
    }
}
