#include "fft.h"

#include <math.h>

void fft_forward(float *re, float *im, int n) {
    /* bit-reversal permutation */
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;

        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }

    /* butterflies, doubling the transform length each pass */
    for (int len = 2; len <= n; len <<= 1) {
        int half = len / 2;

        for (int k = 0; k < half; k++) {
            double ang = -2.0 * M_PI * k / len;
            float wr = (float)cos(ang);
            float wi = (float)sin(ang);

            for (int i = 0; i < n; i += len) {
                float ur = re[i + k], ui = im[i + k];
                float ar = re[i + k + half], ai = im[i + k + half];
                float vr = ar * wr - ai * wi;
                float vi = ar * wi + ai * wr;

                re[i + k] = ur + vr;
                im[i + k] = ui + vi;
                re[i + k + half] = ur - vr;
                im[i + k + half] = ui - vi;
            }
        }
    }
}
