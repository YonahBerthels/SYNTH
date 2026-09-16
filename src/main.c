#include "audio.h"
#include "spectrum.h"
#include "synth.h"
#include "theme.h"
#include "ui.h"
#include "viz.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "tinyfiledialogs.h"

/* SDL_LoadWAV only decodes WAV, so don't offer anything else in the picker */
static const char *const filters[] = { "*.wav" };

/* Our own copy: tinyfd hands back a pointer into its own buffer, and dropped
   paths have to be SDL_free'd, so neither is safe to hold onto. */
static char track_path[1024];

static int out_w = 0, out_h = 0;
/* drawable pixels per window point: 2 on a Retina display, 1 otherwise. Mouse
   events arrive in points but everything is drawn in pixels. */
static float scale_x = 1.0f, scale_y = 1.0f;

enum { DRAG_NONE, DRAG_SEEK, DRAG_KNOB };
static int drag_mode = DRAG_NONE;
static int drag_knob = 0;

static const char *track_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void refresh_metrics(SDL_Window *win, SDL_Renderer *ren) {
    int win_w = 1, win_h = 1;

    SDL_GetRendererOutputSize(ren, &out_w, &out_h);
    SDL_GetWindowSize(win, &win_w, &win_h);

    scale_x = (win_w > 0) ? (float)out_w / win_w : 1.0f;
    scale_y = (win_h > 0) ? (float)out_h / win_h : 1.0f;

    viz_resize(ren, out_w, out_h);
    ui_resize(out_h);
}

static bool open_track(const char *path) {
    if (!audio_load(path)) return false;

    spectrum_init(audio_sample_rate());

    if (!audio_start()) return false;

    snprintf(track_path, sizeof track_path, "%s", path);
    return true;
}

static void pick_track(void) {
    const char *chosen = tinyfd_openFileDialog("Choose sample", "", 1, filters,
                                               "WAV files", 0);
    if (chosen) open_track(chosen);
}

static void handle_key(SDL_Window *win, SDL_Keycode key) {
    switch (key) {
    case SDLK_LEFT:  synth_select(-1); break;
    case SDLK_RIGHT: synth_select(+1); break;
    case SDLK_UP:    synth_adjust(+1); break;
    case SDLK_DOWN:  synth_adjust(-1); break;
    case SDLK_r:     synth_reset(); break;
    case SDLK_SPACE: audio_toggle_pause(); break;
    case SDLK_l:     audio_toggle_loop(); break;
    case SDLK_o:     pick_track(); break;
    case SDLK_f: {
        Uint32 flags = SDL_GetWindowFlags(win);
        SDL_SetWindowFullscreen(
            win, (flags & SDL_WINDOW_FULLSCREEN_DESKTOP)
                     ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        break;
    }
    }
}

/* Shared by press and drag: press decides what is being dragged, motion just
   keeps applying it. */
static void apply_pointer(int px, int py, bool pressed) {
    double fraction;
    int knob;
    bool on_meter;
    float t;

    if (pressed) {
        if (audio_ready() &&
            ui_hit_progress(px, py, out_w, out_h, &fraction)) {
            drag_mode = DRAG_SEEK;
            audio_seek(fraction);
            return;
        }

        if (audio_ready() &&
            ui_hit_panel(px, py, out_w, out_h, &knob, &on_meter, &t)) {
            synth_select_index(knob);
            if (on_meter) {
                drag_mode = DRAG_KNOB;
                drag_knob = knob;
                synth_set_normalised(knob, t);
            }
            return;
        }

        drag_mode = DRAG_NONE;
        return;
    }

    /* a drag stays with whatever it grabbed, even if the pointer wanders */
    if (drag_mode == DRAG_SEEK) {
        double w = (out_w > 0) ? out_w : 1;
        double f = px / w;
        audio_seek(f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f));
    } else if (drag_mode == DRAG_KNOB) {
        if (ui_hit_panel(px, py, out_w, out_h, &knob, &on_meter, &t)) {
            synth_set_normalised(drag_knob, t);
        }
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "-- SYNTH --", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W,
        WIN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI |
               SDL_WINDOW_RESIZABLE);

    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    /* NOT WIN_W/WIN_H: with SDL_WINDOW_ALLOW_HIGHDPI the drawable is larger
       than the window size in points (2x on a Retina display) */
    SDL_GetRendererOutputSize(ren, &out_w, &out_h);

    viz_init(ren, out_w, out_h);
    ui_init(out_h);
    refresh_metrics(win, ren);

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    SDL_RaiseWindow(win);

    bool running = true;
    bool asked_once = false;

    while (running) {
        SDL_Event e;

        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                running = false;
                break;

            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                else handle_key(win, e.key.keysym.sym);
                break;

            case SDL_WINDOWEVENT:
                /* also fires when the window moves to a display of a
                   different DPI */
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    refresh_metrics(win, ren);
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    apply_pointer((int)(e.button.x * scale_x),
                                  (int)(e.button.y * scale_y), true);
                }
                break;

            case SDL_MOUSEMOTION:
                if (drag_mode != DRAG_NONE) {
                    apply_pointer((int)(e.motion.x * scale_x),
                                  (int)(e.motion.y * scale_y), false);
                }
                break;

            case SDL_MOUSEBUTTONUP:
                drag_mode = DRAG_NONE;
                break;

            case SDL_DROPFILE:
                open_track(e.drop.file);
                SDL_free(e.drop.file);
                break;
            }
        }

        /* Outside the event loop: this used to sit inside it, so the dialog
           only opened when an event happened to be queued. */
        if (!asked_once) {
            asked_once = true;
            pick_track();
        }

        if (audio_ready()) {
            audio_pump();
            spectrum_update();
        }
        viz_update();

        viz_draw_background(ren, out_w, out_h);
        viz_draw_bars(ren);
        ui_draw_header(ren, track_path[0] ? track_name(track_path)
                                          : "drop a .wav, or press O", out_w);

        if (audio_ready()) {
            ui_draw_panel(ren, out_w, out_h);
            ui_draw_progress(ren, out_w, out_h);
        }

        SDL_RenderPresent(ren);
    }

    audio_shutdown();
    viz_shutdown();
    ui_shutdown();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
