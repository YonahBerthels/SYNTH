#include "ui.h"
#include "audio.h"
#include "synth.h"
#include "theme.h"
#include "viz.h"

#include <SDL2/SDL_ttf.h>
#include <stdio.h>

#define FONT_PATH "/System/Library/Fonts/Menlo.ttc"

/* rows within the knob panel, measured from its top edge */
#define ROW_HINT 14
#define ROW_NAME 58
#define ROW_VALUE 88
#define ROW_METER 126
#define ROW_MARK 142
#define METER_H 8
/* clicks above this row select a knob; at or below it they also set its value */
#define METER_GRAB 110
/* the progress bar is 4px of paint but a fatter target for the mouse */
#define PROGRESS_GRAB 26

static TTF_Font *font_big = NULL;
static TTF_Font *font_small = NULL;
static int font_h = 0;

static void open_fonts(int draw_h) {
    if (font_big) TTF_CloseFont(font_big);
    if (font_small) TTF_CloseFont(font_small);

    font_big = TTF_OpenFont(FONT_PATH, draw_h / 23);
    font_small = TTF_OpenFont(FONT_PATH, draw_h / 42);
    font_h = draw_h;
}

bool ui_init(int draw_h) {
    open_fonts(draw_h);

    if (!font_big || !font_small) {
        fprintf(stderr, "TTF_OpenFont(%s): %s\n", FONT_PATH, TTF_GetError());
        return false;
    }
    return true;
}

void ui_resize(int draw_h) {
    /* reopening is ~1ms, but resize events arrive in floods, so only do it
       when the size actually changed */
    if (draw_h != font_h) open_fonts(draw_h);
}

void ui_shutdown(void) {
    if (font_big) TTF_CloseFont(font_big);
    if (font_small) TTF_CloseFont(font_small);
    font_big = font_small = NULL;
}

/* Returns the rendered width, so callers can lay text out end to end. */
static int text(SDL_Renderer *ren, TTF_Font *font, const char *s,
                int x, int y, SDL_Color fg) {
    if (!font) return 0;

    SDL_Surface *surf = TTF_RenderText_Blended(font, s, fg);
    if (!surf) return 0;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(ren, surf);
    SDL_Rect dst = { x, y, surf->w, surf->h };
    int w = surf->w;
    SDL_FreeSurface(surf);
    if (!texture) return 0;

    SDL_RenderCopy(ren, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
    return w;
}

static void format_time(char *out, size_t n, double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    int total = (int)seconds;
    snprintf(out, n, "%d:%02d", total / 60, total % 60);
}

static int panel_slot(int draw_w) {
    return (draw_w - 2 * PAD) / synth_knob_count();
}

/* Track name on the left, status and elapsed / total on the right. */
void ui_draw_header(SDL_Renderer *ren, const char *name, int draw_w) {
    text(ren, font_big, name, PAD, PAD, ColWhite);

    if (!audio_ready()) return;

    char elapsed[16], total[16], line[64];
    int rate = audio_sample_rate();

    format_time(elapsed, sizeof elapsed, (double)audio_audible_frames() / rate);
    format_time(total, sizeof total, (double)audio_total_frames() / rate);
    snprintf(line, sizeof line, "%s%s%s / %s",
             audio_looping() ? "LOOP  " : "",
             audio_finished() ? "END  " : "",
             elapsed, total);

    int tw = 0, th = 0;
    TTF_SizeText(font_small, line, &tw, &th);
    text(ren, font_small, line, draw_w - PAD - tw, PAD + 12, ColDim);
}

/* A row of labelled meters, one per knob, with the selected one lit. */
void ui_draw_panel(SDL_Renderer *ren, int draw_w, int draw_h) {
    const int count = synth_knob_count();
    const int slot = panel_slot(draw_w);
    const int top = draw_h - PANEL_H;
    const int meter_w = slot - 40;
    char label[64];

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    /* panel backing, so the bars' glow doesn't wash out the readout */
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 150);
    SDL_Rect backing = { 0, top, draw_w, PANEL_H };
    SDL_RenderFillRect(ren, &backing);

    SDL_SetRenderDrawColor(ren, 255, 255, 255, 22);
    SDL_Rect rule = { 0, top, draw_w, 1 };
    SDL_RenderFillRect(ren, &rule);

    text(ren, font_small,
         audio_paused()
             ? "PAUSED   [SPC] resume   [<>] knob   [^v] value   [O]pen   [L]oop"
             : "[<>] knob   [^v] value   [click] set   [O]pen   [L]oop   [F]ull   [R]eset   [SPC] pause",
         PAD, top + ROW_HINT, ColKnobIdle);

    for (int i = 0; i < count; i++) {
        const Knob *k = synth_knob(i);
        bool sel = (i == synth_selected());
        int x = PAD + i * slot;

        SDL_Color fg = sel ? ColKnobActive : ColKnobIdle;
        SDL_Color hue = viz_accent(i, count);

        snprintf(label, sizeof label, "%s", k->name);
        text(ren, font_small, label, x, top + ROW_NAME, fg);

        snprintf(label, sizeof label, "%.2f", k->value);
        text(ren, font_small, label, x, top + ROW_VALUE, sel ? ColWhite : ColDim);

        /* meter: unfilled track, then the filled portion */
        float t = synth_knob_normalised(i);

        SDL_Rect track = { x, top + ROW_METER, meter_w, METER_H };
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 30);
        SDL_RenderFillRect(ren, &track);

        SDL_Rect fill = { x, top + ROW_METER, (int)(meter_w * t), METER_H };
        if (sel) SDL_SetRenderDrawColor(ren, hue.r, hue.g, hue.b, 255);
        else SDL_SetRenderDrawColor(ren, 255, 255, 255, 80);
        SDL_RenderFillRect(ren, &fill);

        /* selection marker under the active knob */
        if (sel) {
            SDL_Rect mark = { x, top + ROW_MARK, meter_w, 2 };
            SDL_SetRenderDrawColor(ren, hue.r, hue.g, hue.b, 160);
            SDL_RenderFillRect(ren, &mark);
        }
    }
}

/* Hairline progress bar along the very bottom edge. */
void ui_draw_progress(SDL_Renderer *ren, int draw_w, int draw_h) {
    Uint32 total = audio_total_frames();
    if (total == 0) return;

    double t = (double)audio_audible_frames() / total;
    if (t > 1.0) t = 1.0;

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    SDL_Rect track = { 0, draw_h - PROGRESS_H, draw_w, PROGRESS_H };
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 25);
    SDL_RenderFillRect(ren, &track);

    SDL_Color hue = viz_accent(1, 2);
    SDL_Rect fill = { 0, draw_h - PROGRESS_H, (int)(draw_w * t), PROGRESS_H };
    SDL_SetRenderDrawColor(ren, hue.r, hue.g, hue.b, 230);
    SDL_RenderFillRect(ren, &fill);

    /* playhead, so there is something to aim at */
    SDL_Rect head = { (int)(draw_w * t) - 1, draw_h - PROGRESS_H - 4, 3,
                      PROGRESS_H + 4 };
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 220);
    SDL_RenderFillRect(ren, &head);
}

bool ui_hit_progress(int x, int y, int draw_w, int draw_h, double *fraction) {
    if (y < draw_h - PROGRESS_GRAB || draw_w <= 0) return false;

    double t = (double)x / draw_w;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    *fraction = t;
    return true;
}

bool ui_hit_panel(int x, int y, int draw_w, int draw_h,
                  int *knob, bool *on_meter, float *fraction) {
    const int count = synth_knob_count();
    const int slot = panel_slot(draw_w);
    const int top = draw_h - PANEL_H;

    if (y < top || slot <= 0) return false;
    /* the progress strip sits in front of the panel's bottom edge */
    if (y >= draw_h - PROGRESS_GRAB) return false;

    int i = (x - PAD) / slot;
    if (i < 0) i = 0;
    if (i >= count) i = count - 1;

    *knob = i;
    *on_meter = (y >= top + METER_GRAB);

    if (*on_meter) {
        int meter_w = slot - 40;
        float t = (float)(x - (PAD + i * slot)) / (meter_w > 0 ? meter_w : 1);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        *fraction = t;
    }

    return true;
}
