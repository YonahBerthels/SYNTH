#ifndef VIZ_H
#define VIZ_H

#include <SDL2/SDL.h>
#include <stdbool.h>

/* The bars themselves: geometry, colour cycling, and the background they sit
   on. Reads levels from the spectrum module; draws nothing else. */

/* Creates the gradient and trail textures. Call once, after the renderer. */
bool viz_init(SDL_Renderer *ren, int draw_w, int draw_h);
void viz_shutdown(void);

/* Recomputes geometry and resizes the trail buffer for a new drawable size. */
void viz_resize(SDL_Renderer *ren, int draw_w, int draw_h);

void viz_update(void);   /* levels and hues -> rectangles */

void viz_draw_background(SDL_Renderer *ren, int draw_w, int draw_h);
void viz_draw_bars(SDL_Renderer *ren);

/* Hue sampled across the bar array, for accents elsewhere in the UI:
   item i of n. */
SDL_Color viz_accent(int i, int n);

#endif /* VIZ_H */
