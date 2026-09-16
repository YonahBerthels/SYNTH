#include "viz.h"
#include "spectrum.h"
#include "theme.h"

#include <math.h>

/* peak-to-peak height of a full-scale bar, as a percentage of the band left
   between the header and the knob panel */
#define BAR_STD_HEIGHT_PCT 85
/* gap between neighbouring bars; the rest of each slot is bar */
#define BAR_GAP 4

/* neon bloom: each bar is redrawn larger and dimmer with additive blending */
#define GLOW_LAYERS 3
#define GLOW_STEP 5
#define GLOW_ALPHA 120
/* peak markers ride the loudest recent level and sink back slowly */
#define PEAK_THICK 3

/* How much of the previous frame survives, per frame. 232/255 decays to a
   tenth in about twenty frames, so bars leave a wake roughly a third of a
   second long. Lower is snappier, higher smears. */
#define TRAIL_FADE 232

/* vertical shading inside each bar: full brightness on the centre line,
   falling to this at the tips */
#define BAR_TIP_SHADE 170
#define GRAD_H 128

/* the lowest bars drive the background pulse */
#define BASS_BARS 8
#define BASS_GLIDE 0.20f
#define BG_PULSE 0.45

/* seconds for the hue to travel a full turn of the colour wheel */
#define HUE_CYCLE_SECONDS 6.0
/* degrees of hue spread across the bar array, so it reads as a rainbow
   sweeping sideways rather than every bar flashing the same colour */
#define HUE_SPREAD 300.0
#define HUE_SATURATION 0.85
#define HUE_VALUE 1.00

typedef struct {
    SDL_Rect rect;
    SDL_Color color;
} Bar;

static Bar bars[N_BARS];

/* set by viz_resize from the renderer's real output size */
static int bar_centre;      /* bars are mirrored about this line */
static int bar_max_height;  /* peak-to-peak height of a full-scale bar */

/* 1 x GRAD_H column of greys, stretched into each bar and tinted with the
   bar's colour. Gives the bars an interior falloff instead of a flat fill. */
static SDL_Texture *gradient = NULL;

/* Bars are drawn into this instead of straight to the screen. It is faded
   rather than cleared each frame, which is what leaves the trails. */
static SDL_Texture *trail = NULL;
static bool trail_ok = false;

static float bass_pulse = 0.0f;

/* HSV rather than lerping RGB directly: sweeping hue keeps every colour at the
   same brightness, where interpolating raw RGB channels dips through muddy
   greys between the primaries. h in [0,360), s and v in [0,1]. */
static SDL_Color hsv_to_rgb(double h, double s, double v) {
    double c = v * s;
    double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
    double m = v - c;
    double r, g, b;

    if      (h <  60.0) { r = c; g = x; b = 0; }
    else if (h < 120.0) { r = x; g = c; b = 0; }
    else if (h < 180.0) { r = 0; g = c; b = x; }
    else if (h < 240.0) { r = 0; g = x; b = c; }
    else if (h < 300.0) { r = x; g = 0; b = c; }
    else                { r = c; g = 0; b = x; }

    SDL_Color out = {
        (Uint8)((r + m) * 255.0),
        (Uint8)((g + m) * 255.0),
        (Uint8)((b + m) * 255.0),
        255
    };
    return out;
}

static void make_gradient(SDL_Renderer *ren) {
    Uint32 px[GRAD_H];

    for (int y = 0; y < GRAD_H; y++) {
        /* 0 on the centre line, 1 at either tip */
        double t = fabs((double)y / (GRAD_H - 1) - 0.5) * 2.0;
        Uint8 v = (Uint8)(255 - (255 - BAR_TIP_SHADE) * t);
        px[y] = ((Uint32)v << 24) | ((Uint32)v << 16) | ((Uint32)v << 8) | 0xFF;
    }

    gradient = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                 SDL_TEXTUREACCESS_STATIC, 1, GRAD_H);
    if (!gradient) return;

    SDL_UpdateTexture(gradient, NULL, px, (int)sizeof(Uint32));
    SDL_SetTextureBlendMode(gradient, SDL_BLENDMODE_BLEND);
}

static void make_trail(SDL_Renderer *ren, int draw_w, int draw_h) {
    if (trail) {
        SDL_DestroyTexture(trail);
        trail = NULL;
    }

    trail_ok = SDL_RenderTargetSupported(ren) == SDL_TRUE;
    if (!trail_ok) return;

    trail = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                              SDL_TEXTUREACCESS_TARGET, draw_w, draw_h);
    if (!trail) {
        trail_ok = false;
        return;
    }

    /* additive, so the wake reads as light over the background */
    SDL_SetTextureBlendMode(trail, SDL_BLENDMODE_ADD);

    /* a fresh target holds garbage until something clears it */
    SDL_SetRenderTarget(ren, trail);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
    SDL_RenderClear(ren);
    SDL_SetRenderTarget(ren, NULL);
}

bool viz_init(SDL_Renderer *ren, int draw_w, int draw_h) {
    make_gradient(ren);
    viz_resize(ren, draw_w, draw_h);
    return gradient != NULL;
}

void viz_shutdown(void) {
    if (gradient) SDL_DestroyTexture(gradient);
    if (trail) SDL_DestroyTexture(trail);
    gradient = trail = NULL;
}

void viz_resize(SDL_Renderer *ren, int draw_w, int draw_h) {
    const int span = draw_w - 2 * PAD;

    /* The bars own the band between header and panel, so nothing overlaps at
       any window size or DPI. */
    const int top = HEADER_H;
    const int bottom = draw_h - PANEL_H;
    const int avail = (bottom > top) ? bottom - top : draw_h;
    const int std_h = (avail * BAR_STD_HEIGHT_PCT) / 100;

    bar_centre = top + avail / 2;
    bar_max_height = std_h;

    for (int i = 0; i < N_BARS; i++) {
        /* slot edges are derived from the full span, so integer rounding is
           absorbed bar-to-bar instead of piling up as dead space */
        int left = (i * span) / N_BARS;
        int right = ((i + 1) * span) / N_BARS;
        int w = right - left - BAR_GAP;

        /* at high N_BARS the gap can eat the whole slot; degrade to hairlines
           rather than to nothing */
        if (w < 1) w = 1;

        bars[i].rect.x = PAD + left;
        bars[i].rect.y = bar_centre - std_h / 2;
        bars[i].rect.w = w;
        bars[i].rect.h = std_h;
        bars[i].color = ColWhite;
    }

    make_trail(ren, draw_w, draw_h);
}

void viz_update(void) {
    double t = SDL_GetTicks() / 1000.0;
    double base = fmod(t / HUE_CYCLE_SECONDS, 1.0) * 360.0;
    float bass = 0.0f;

    for (int i = 0; i < N_BARS; i++) {
        int h = (int)(spectrum_level(i) * bar_max_height);
        if (h < 2) h = 2;   /* keep a visible resting line */

        bars[i].rect.h = h;
        bars[i].rect.y = bar_centre - h / 2;

        /* offsetting the hue per bar makes the rainbow slide across the array
           instead of every bar flashing the same colour */
        double hue = fmod(base + ((double)i / N_BARS) * HUE_SPREAD, 360.0);
        bars[i].color = hsv_to_rgb(hue, HUE_SATURATION, HUE_VALUE);

        if (i < BASS_BARS) bass += spectrum_level(i);
    }

    /* smoothed so the background breathes rather than strobes */
    bass /= BASS_BARS;
    bass_pulse += (bass - bass_pulse) * BASS_GLIDE;
}

SDL_Color viz_accent(int i, int n) {
    if (n <= 0) return ColWhite;
    return bars[(i * N_BARS) / n % N_BARS].color;
}

/* Vertical gradient, drawn in bands rather than per-row: at four pixels a band
   the seams are invisible and it costs a few hundred fills. Brightness rides
   the bass, so the whole window breathes on the kick. */
void viz_draw_background(SDL_Renderer *ren, int draw_w, int draw_h) {
    const int band = 4;
    const double lift = 1.0 + BG_PULSE * bass_pulse;

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    for (int y = 0; y < draw_h; y += band) {
        double t = (double)y / draw_h;
        SDL_Rect r = { 0, y, draw_w, band };

        double cr = (ColBgTop.r + (ColBgBottom.r - ColBgTop.r) * t) * lift;
        double cg = (ColBgTop.g + (ColBgBottom.g - ColBgTop.g) * t) * lift;
        double cb = (ColBgTop.b + (ColBgBottom.b - ColBgTop.b) * t) * lift;

        SDL_SetRenderDrawColor(ren,
            (Uint8)(cr > 255.0 ? 255.0 : cr),
            (Uint8)(cg > 255.0 ? 255.0 : cg),
            (Uint8)(cb > 255.0 ? 255.0 : cb), 255);
        SDL_RenderFillRect(ren, &r);
    }

    /* the line the bars are mirrored about */
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 20);
    SDL_Rect centre = { 0, bar_centre, draw_w, 1 };
    SDL_RenderFillRect(ren, &centre);
}

/* White caps floating at each bar's recent maximum. They read as a separate
   layer from the coloured bars, so the eye can track transients. */
static void draw_peaks(SDL_Renderer *ren) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 210);

    for (int i = 0; i < N_BARS; i++) {
        int h = (int)(spectrum_peak(i) * bar_max_height);
        if (h < 4) continue;

        SDL_Rect above = { bars[i].rect.x, bar_centre - h / 2 - PEAK_THICK,
                           bars[i].rect.w, PEAK_THICK };
        SDL_Rect below = { bars[i].rect.x, bar_centre + h / 2,
                           bars[i].rect.w, PEAK_THICK };

        SDL_RenderFillRect(ren, &above);
        SDL_RenderFillRect(ren, &below);
    }
}

/* Bloom: each bar redrawn progressively larger and fainter. Additive blending
   means neighbouring halos reinforce where bars cluster, which is what sells
   it as light rather than a grey outline. */
static void draw_glow(SDL_Renderer *ren) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);

    for (int layer = GLOW_LAYERS; layer >= 1; layer--) {
        int grow = layer * GLOW_STEP;
        Uint8 alpha = (Uint8)(GLOW_ALPHA / (layer * 2));

        for (int i = 0; i < N_BARS; i++) {
            SDL_Color c = bars[i].color;
            SDL_Rect r = {
                bars[i].rect.x - grow,
                bars[i].rect.y - grow,
                bars[i].rect.w + 2 * grow,
                bars[i].rect.h + 2 * grow
            };

            SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, alpha);
            SDL_RenderFillRect(ren, &r);
        }
    }
}

/* Core, tinted from the shared grey gradient. Stretching one 1x128 column into
   every bar costs the same as a fill but gives the bar an interior falloff, so
   it reads as a shaft of light rather than a painted block. */
static void draw_cores(SDL_Renderer *ren) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    for (int i = 0; i < N_BARS; i++) {
        SDL_Color c = bars[i].color;

        if (gradient) {
            SDL_SetTextureColorMod(gradient, c.r, c.g, c.b);
            SDL_RenderCopy(ren, gradient, NULL, &bars[i].rect);
        } else {
            SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
            SDL_RenderFillRect(ren, &bars[i].rect);
        }
    }
}

void viz_draw_bars(SDL_Renderer *ren) {
    if (trail_ok && trail) {
        SDL_SetRenderTarget(ren, trail);

        /* Fade rather than clear. MOD multiplies what is already there, so
           each frame's bars decay geometrically instead of vanishing. */
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_MOD);
        SDL_SetRenderDrawColor(ren, TRAIL_FADE, TRAIL_FADE, TRAIL_FADE, 255);
        SDL_Rect all = { 0, 0, 0, 0 };
        SDL_QueryTexture(trail, NULL, NULL, &all.w, &all.h);
        SDL_RenderFillRect(ren, &all);

        /* Only the cores go in the wake, and only with BLEND, which
           overwrites. Feeding the additive glow into a buffer that is merely
           faded would let a sustained bar accumulate frame over frame until
           its halo saturated to white. */
        draw_cores(ren);

        SDL_SetRenderTarget(ren, NULL);
        SDL_RenderCopy(ren, trail, NULL, NULL);
    }

    /* the live frame, drawn fresh on top of its own wake */
    draw_glow(ren);
    draw_cores(ren);
    draw_peaks(ren);
}
