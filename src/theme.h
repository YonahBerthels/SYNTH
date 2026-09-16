#ifndef THEME_H
#define THEME_H

#include <SDL2/SDL.h>

/* Window size in points. The drawable may be larger: with
   SDL_WINDOW_ALLOW_HIGHDPI it is 2x this on a Retina display, which is why
   every layout value below is applied to the renderer's output size instead. */
#define WIN_W 900
#define WIN_H 500

/* horizontal margin for page-level content */
#define PAD 20

/* Bands reserved at top and bottom. The bars own whatever is between them, so
   nothing overlaps at any window size or DPI. */
#define HEADER_H 90
#define PANEL_H 170
#define PROGRESS_H 4

extern const SDL_Color ColWhite;
extern const SDL_Color ColDim;
extern const SDL_Color ColKnobActive;
extern const SDL_Color ColKnobIdle;
extern const SDL_Color ColBgTop;
extern const SDL_Color ColBgBottom;

#endif /* THEME_H */
