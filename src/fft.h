#ifndef FFT_H
#define FFT_H

/* In-place iterative radix-2 Cooley-Tukey FFT. n must be a power of two.
   re and im hold the real and imaginary parts on entry and are overwritten
   with the transform. */
void fft_forward(float *re, float *im, int n);

#endif /* FFT_H */
