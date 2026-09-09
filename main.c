#include "SDL2/SDL_error.h"
#include "SDL2/SDL_rect.h"
#include "SDL2/SDL_render.h"
#include "SDL2/SDL_stdinc.h"
#include "SDL2/SDL_surface.h"
#include "SDL2/SDL_video.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL_ttf.h>
#include <string.h>
#include "tinyfiledialogs.h"

#define WIN_W 900
#define WIN_H 500

SDL_Color White = {255, 255, 255, 255};
static const char *const filters[] = {"*.wav", "*.aiff"};

FILE* track;

// function definitions
void draw_text(SDL_Renderer*, TTF_Font*, const char*, int, int);
static const char* get_track_name(const char*);

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init() > 0) {
        fprintf(stderr, "SDL_TTF init: %s\n", SDL_GetError());
        return 1;
    }
    TTF_Font* font = TTF_OpenFont("/System/Library/Fonts/Menlo.ttc", 32);

    SDL_Window *win = SDL_CreateWindow(
        "-- SYNTH --", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W,
        WIN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);

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

    SDL_RaiseWindow(win);

    bool running = true;
    char* path;
    int file_chosen = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            running = false;
        } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            running = false;
        }

        if (!file_chosen) {
            path = tinyfd_openFileDialog("Choose sample", "", 2, filters, "Audio files", 0);
            if (path) {
                file_chosen = 1;
                track = fopen(path, "r");
                if (track == NULL) {
                    fprintf(stderr, "ERROR READING FILE: %s", path);
                    return 1;
                }
            }
        }
    }

        SDL_SetRenderDrawColor(ren, 18, 18, 20, 255);
        SDL_RenderClear(ren);

    /* draw here */
        draw_text(ren, font, get_track_name(path), 10, 10);
        SDL_RenderPresent(ren);
        draw_text(ren, font, get_track_name(path), 10, 10);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

void draw_text(SDL_Renderer* ren, TTF_Font* font, const char* s, int x, int y) {
    SDL_Color fg = {220, 220, 225, 255 };
    SDL_Surface* surf = TTF_RenderText_Blended(font, s, fg);

    if (!surf) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(ren, surf);
    SDL_Rect dist = { x, y, surf->w, surf->h };
    SDL_FreeSurface(surf);
    if (!texture) return;

    SDL_RenderCopy(ren, texture, NULL, &dist);
    SDL_DestroyTexture(texture);
}

static const char* get_track_name(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}
