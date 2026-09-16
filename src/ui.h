#ifndef UI_H
#define UI_H

#include <SDL2/SDL.h>
#include <stdbool.h>

/* Text and chrome: the header, the knob panel, the progress bar. Owns the
   fonts, and answers where the mouse landed. */

/* Fonts are sized from the drawable height, so text keeps its proportions on
   a Retina display and on a plain one. */
bool ui_init(int draw_h);
void ui_resize(int draw_h);
void ui_shutdown(void);

void ui_draw_header(SDL_Renderer *ren, const char *name, int draw_w);
void ui_draw_panel(SDL_Renderer *ren, int draw_w, int draw_h);
void ui_draw_progress(SDL_Renderer *ren, int draw_w, int draw_h);

/* Hit tests, in drawable pixels. Both return false when the point is
   elsewhere. */
bool ui_hit_progress(int x, int y, int draw_w, int draw_h, double *fraction);
/* knob is always set when the point is in the panel; fraction is set only
   when the point is low enough to be on that knob's meter. */
bool ui_hit_panel(int x, int y, int draw_w, int draw_h,
                  int *knob, bool *on_meter, float *fraction);

#endif /* UI_H */
