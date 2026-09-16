#include "synth.h"

#include <math.h>

/* the cutoff knob is normalised 0..1 and mapped logarithmically onto this */
#define CUTOFF_MIN_HZ 30.0f
#define CUTOFF_MAX_HZ 18000.0f

/* How fast a knob's working value chases the value you set, per chunk. Knob
   changes would otherwise land as a single jump in the coefficients, which a
   fast sweep turns into audible zipper noise. */
#define KNOB_GLIDE 0.35f

enum { K_DRIVE, K_CUTOFF, K_RESO, K_CRUSH, K_TREM, K_RATE, K_VOL, N_KNOBS };

static Knob knobs[N_KNOBS] = {
    { "drive",   1.00f,  1.00f, 30.00f, 1.00f,  1.00f },
    { "cutoff",  1.00f,  0.02f,  1.00f, 0.04f,  1.00f },
    { "reso",    0.10f,  0.00f,  0.95f, 0.05f,  0.10f },
    { "crush",  16.00f,  2.00f, 16.00f, 1.00f, 16.00f },
    { "trem",    0.00f,  0.00f,  1.00f, 0.05f,  0.00f },
    { "rate",    5.50f,  0.50f, 20.00f, 0.50f,  5.50f },
    { "vol",     0.80f,  0.00f,  1.00f, 0.05f,  0.80f },
};

/* glided shadow of knobs[].value; the coefficients are built from these */
static float smooth[N_KNOBS] = {
    1.00f, 1.00f, 0.10f, 16.00f, 0.00f, 5.50f, 0.80f
};

static int sel_knob = 0;

/* TPT state-variable filter integrator state, one pair per channel */
static float svf_ic1[SYNTH_MAX_CHANNELS], svf_ic2[SYNTH_MAX_CHANNELS];
static double lfo_phase = 0.0;

/* coefficients, recomputed by synth_prepare */
static struct {
    float drive, drive_norm;
    float a1, a2, a3;
    float crush_levels;
    int crushing;
    float trem;
    float vol;
    double lfo_inc;
} co;

/* --- knob model ---------------------------------------------------------- */

int synth_knob_count(void) { return N_KNOBS; }

const Knob *synth_knob(int i) {
    if (i < 0 || i >= N_KNOBS) return &knobs[0];
    return &knobs[i];
}

int synth_selected(void) { return sel_knob; }

void synth_select(int delta) {
    sel_knob = (sel_knob + delta % N_KNOBS + N_KNOBS) % N_KNOBS;
}

void synth_select_index(int i) {
    if (i >= 0 && i < N_KNOBS) sel_knob = i;
}

void synth_adjust(int dir) {
    Knob *k = &knobs[sel_knob];

    k->value += (dir > 0) ? k->step : -k->step;

    if (k->value < k->min) k->value = k->min;
    if (k->value > k->max) k->value = k->max;
}

void synth_reset(void) {
    for (int i = 0; i < N_KNOBS; i++) knobs[i].value = knobs[i].initial;
}

float synth_knob_normalised(int i) {
    if (i < 0 || i >= N_KNOBS) return 0.0f;
    const Knob *k = &knobs[i];
    return (k->value - k->min) / (k->max - k->min);
}

void synth_set_normalised(int i, float t) {
    if (i < 0 || i >= N_KNOBS) return;

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    Knob *k = &knobs[i];
    k->value = k->min + t * (k->max - k->min);
}

/* --- processing ---------------------------------------------------------- */

void synth_clear(void) {
    for (int c = 0; c < SYNTH_MAX_CHANNELS; c++) svf_ic1[c] = svf_ic2[c] = 0.0f;
    lfo_phase = 0.0;
}

void synth_prepare(float sample_rate) {
    for (int i = 0; i < N_KNOBS; i++) {
        smooth[i] += (knobs[i].value - smooth[i]) * KNOB_GLIDE;
    }

    co.drive = smooth[K_DRIVE];
    co.drive_norm = tanhf(co.drive);
    co.trem = smooth[K_TREM];
    co.vol = smooth[K_VOL];

    float cutoff_hz = CUTOFF_MIN_HZ *
        powf(CUTOFF_MAX_HZ / CUTOFF_MIN_HZ, smooth[K_CUTOFF]);
    if (cutoff_hz > sample_rate * 0.45f) cutoff_hz = sample_rate * 0.45f;

    /* Topology-preserving state-variable filter (Zavalishin). The naive
       Chamberlin SVF is only stable below about fs/6, which would cap the
       cutoff knob around 7kHz; this form stays stable up to Nyquist. */
    float g = tanf((float)M_PI * cutoff_hz / sample_rate);
    float k = 2.0f - 2.0f * smooth[K_RESO];  /* damping; smaller = more resonant */
    if (k < 0.05f) k = 0.05f;

    co.a1 = 1.0f / (1.0f + g * (g + k));
    co.a2 = g * co.a1;
    co.a3 = g * co.a2;

    co.crush_levels = powf(2.0f, smooth[K_CRUSH] - 1.0f);
    co.crushing = (smooth[K_CRUSH] < knobs[K_CRUSH].max - 0.01f);

    co.lfo_inc = 2.0 * M_PI * smooth[K_RATE] / sample_rate;
}

void synth_process(float *frame, int channels) {
    /* one LFO value per frame, so the channels stay in phase */
    float mod = 1.0f - co.trem * (0.5f - 0.5f * (float)sin(lfo_phase));

    for (int c = 0; c < channels; c++) {
        float s = frame[c];

        /* crush -> drive -> filter -> tremolo -> level. Crushing before the
           drive keeps the quantisation steps audible instead of smoothing
           them away. */
        if (co.crushing) s = roundf(s * co.crush_levels) / co.crush_levels;

        if (co.drive > 1.0f) s = tanhf(s * co.drive) / co.drive_norm;

        float v3 = s - svf_ic2[c];
        float v1 = co.a1 * svf_ic1[c] + co.a2 * v3;
        float v2 = svf_ic2[c] + co.a2 * svf_ic1[c] + co.a3 * v3;
        svf_ic1[c] = 2.0f * v1 - svf_ic1[c];
        svf_ic2[c] = 2.0f * v2 - svf_ic2[c];
        s = v2;   /* low-pass output */

        s *= mod * co.vol;

        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;

        frame[c] = s;
    }

    lfo_phase += co.lfo_inc;
    if (lfo_phase > 2.0 * M_PI) lfo_phase = fmod(lfo_phase, 2.0 * M_PI);
}
