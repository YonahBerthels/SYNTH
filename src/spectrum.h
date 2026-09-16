#ifndef SPECTRUM_H
#define SPECTRUM_H

/* Turns the processed audio history into one level per bar. Knows nothing
   about how those levels get drawn. */

#define N_BARS 64

void spectrum_init(int sample_rate);
void spectrum_update(void);

float spectrum_level(int i);   /* 0..1, smoothed */
float spectrum_peak(int i);    /* 0..1, slowly falling maximum */

#endif /* SPECTRUM_H */
