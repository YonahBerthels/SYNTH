#include "spectrum.h"
#include "audio.h"
#include "fft.h"

#include <math.h>

/* FFT window, in frames; must be a power of two. 2048 @ 44.1kHz is ~46ms of
   audio and ~21Hz per bin: fine enough for bass, fast enough to feel live. */
#define FFT_SIZE 2048
/* visible spectrum range, log-spaced across the bars */
#define FREQ_MIN 40.0
#define FREQ_MAX 16000.0
/* magnitudes below this (in dB) read as silence */
#define DB_FLOOR -70.0f
/* per-frame smoothing: bars snap up, ease down */
#define ATTACK 0.55f
#define DECAY 0.10f
/* how fast the peak markers sink */
#define PEAK_FALL 0.006f

static float bar_level[N_BARS];
static float bar_peak[N_BARS];

/* FFT bin range feeding each bar, precomputed once the sample rate is known */
static int bin_lo[N_BARS], bin_hi[N_BARS];
static float hann[FFT_SIZE];

void spectrum_init(int sample_rate) {
    /* Hann window. Without it, the window edges act as a hard cut and smear
       energy across every bin, so the bars all twitch together. */
    for (int n = 0; n < FFT_SIZE; n++) {
        hann[n] = (float)(0.5 * (1.0 - cos(2.0 * M_PI * n / (FFT_SIZE - 1))));
    }

    /* Log-spaced bar edges: pitch is logarithmic, so linear bins would give
       almost every bar to the treble and cram all the bass into the first one. */
    const double ratio = FREQ_MAX / FREQ_MIN;
    const double bins_per_hz = (double)FFT_SIZE / sample_rate;
    const int max_bin = FFT_SIZE / 2 - 1;

    for (int i = 0; i < N_BARS; i++) {
        double f_lo = FREQ_MIN * pow(ratio, (double)i / N_BARS);
        double f_hi = FREQ_MIN * pow(ratio, (double)(i + 1) / N_BARS);

        bin_lo[i] = (int)(f_lo * bins_per_hz);
        bin_hi[i] = (int)(f_hi * bins_per_hz);

        if (bin_lo[i] < 1) bin_lo[i] = 1;             /* skip DC */
        if (bin_hi[i] <= bin_lo[i]) bin_hi[i] = bin_lo[i] + 1;
        if (bin_hi[i] > max_bin) bin_hi[i] = max_bin;
        if (bin_lo[i] > max_bin) bin_lo[i] = max_bin;
    }

    for (int i = 0; i < N_BARS; i++) bar_level[i] = bar_peak[i] = 0.0f;
}

void spectrum_update(void) {
    static float re[FFT_SIZE], im[FFT_SIZE];

    audio_history(re, FFT_SIZE);

    for (int n = 0; n < FFT_SIZE; n++) {
        re[n] *= hann[n];
        im[n] = 0.0f;
    }

    fft_forward(re, im, FFT_SIZE);

    for (int i = 0; i < N_BARS; i++) {
        /* loudest bin in the band: punchier than averaging, which lets one
           quiet neighbour flatten a peak */
        float mag = 0.0f;
        for (int k = bin_lo[i]; k < bin_hi[i]; k++) {
            float m = sqrtf(re[k] * re[k] + im[k] * im[k]);
            if (m > mag) mag = m;
        }

        /* dB, because loudness is logarithmic; linear magnitude leaves
           everything but the kick drum pinned near zero. The FFT_SIZE/4
           divisor accounts for the Hann window's 0.5 coherent gain, so a
           full-scale sine lands at exactly 0 dB. */
        float db = 20.0f * log10f(mag / (FFT_SIZE / 4.0f) + 1e-9f);
        float v = (db - DB_FLOOR) / -DB_FLOOR;

        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;

        /* fast attack, slow decay: catches transients, then falls like a
           VU meter instead of flickering */
        float rate = (v > bar_level[i]) ? ATTACK : DECAY;
        bar_level[i] += (v - bar_level[i]) * rate;

        /* the cap jumps straight to a new high, then sinks at a fixed rate
           regardless of the level underneath it */
        if (bar_level[i] > bar_peak[i]) bar_peak[i] = bar_level[i];
        else bar_peak[i] -= PEAK_FALL;

        if (bar_peak[i] < 0.0f) bar_peak[i] = 0.0f;
    }
}

float spectrum_level(int i) {
    if (i < 0 || i >= N_BARS) return 0.0f;
    return bar_level[i];
}

float spectrum_peak(int i) {
    if (i < 0 || i >= N_BARS) return 0.0f;
    return bar_peak[i];
}
