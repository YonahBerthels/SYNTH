#ifndef SYNTH_H
#define SYNTH_H

/* The effect chain and the knobs driving it. Pure DSP: this module knows
   nothing about SDL, the audio device, or the display. */

#define SYNTH_MAX_CHANNELS 8

typedef struct {
    const char *name;
    float value;
    float min, max, step;
    float initial;
} Knob;

/* --- knob model --- */

int synth_knob_count(void);
const Knob *synth_knob(int i);
int synth_selected(void);

void synth_select(int delta);        /* move the selection, wrapping */
void synth_select_index(int i);      /* select a specific knob */
void synth_adjust(int dir);          /* nudge the selected knob by its step */
void synth_reset(void);              /* every knob back to its initial value */

/* 0..1 across the knob's range, for meters and mouse drags */
float synth_knob_normalised(int i);
void synth_set_normalised(int i, float t);

/* --- processing --- */

/* Clears filter and LFO state. Call before starting playback. */
void synth_clear(void);

/* Recomputes coefficients from the current knob values. Call once per chunk,
   before the synth_process calls for that chunk. */
void synth_prepare(float sample_rate);

/* Processes one interleaved frame in place and advances the LFO. */
void synth_process(float *frame, int channels);

#endif /* SYNTH_H */
