#include <stdint.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <nihterm/gfx.h>
#include <nihterm/vt.h>

#define FONT_REGULAR 0
#define FONT_BOLD 1
#define FONT_ITALIC 2
#define FONT_BOLD_ITALIC 3

static int load_fonts(struct graphics *graphics);

struct graphics {
  SDL_Window *window;
  SDL_Renderer *renderer;
  TTF_Font *font[4];

  size_t xdim;
  size_t ydim;

  size_t cellw;
  size_t cellh;

  struct vt *vt;

  int dirty;
};

struct graphics *create_graphics() {
  SDL_Init(SDL_INIT_VIDEO);
  TTF_Init();

  struct graphics *graphics =
      (struct graphics *)calloc(sizeof(struct graphics), 1);

  if (load_fonts(graphics)) {
    fprintf(stderr, "nihterm: failed to load fonts\n");
    free(graphics);
    return NULL;
  }

  int cellw = 0, cellh = 0;
  TTF_SizeUTF8(graphics->font[FONT_REGULAR], "X", &cellw, &cellh);
  graphics->cellw = (size_t)cellw;
  graphics->cellh = (size_t)cellh;

  graphics->xdim = graphics->cellw * 80;
  graphics->ydim = graphics->cellh * 25;

  graphics->window = SDL_CreateWindow(
      "nihterm", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      (int)graphics->xdim, (int)graphics->ydim, 0);

  graphics->renderer =
      SDL_CreateRenderer(graphics->window, -1, SDL_RENDERER_SOFTWARE);
  SDL_SetRenderDrawColor(graphics->renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
  SDL_RenderClear(graphics->renderer);
  SDL_RenderPresent(graphics->renderer);

  graphics->dirty = 1;

  return graphics;
}

static int load_fonts(struct graphics *graphics) {
  // already loaded?
  if (graphics->font[0]) {
    // TODO(miselin): unload the old fonts so we can load new ones
    return 0;
  }

  TTF_Font *font = TTF_OpenFont("fonts/CourierPrime-Regular.ttf", 12);
  if (!font) {
    return 1;
  }

  graphics->font[FONT_REGULAR] = font;

  font = TTF_OpenFont("fonts/CourierPrime-Bold.ttf", 12);
  if (!font) {
    return 1;
  }

  graphics->font[FONT_BOLD] = font;

  font = TTF_OpenFont("fonts/CourierPrime-Italic.ttf", 12);
  if (!font) {
    return 1;
  }

  graphics->font[FONT_ITALIC] = font;

  font = TTF_OpenFont("fonts/CourierPrime-BoldItalic.ttf", 12);
  if (!font) {
    return 1;
  }

  graphics->font[FONT_BOLD_ITALIC] = font;

  return 0;
}

void destroy_graphics(struct graphics *graphics) {
  SDL_DestroyRenderer(graphics->renderer);
  SDL_DestroyWindow(graphics->window);
  SDL_Quit();

  free(graphics);
}

size_t cell_width(struct graphics *graphics) { return graphics->cellw; }

size_t cell_height(struct graphics *graphics) { return graphics->cellh; }

size_t window_width(struct graphics *graphics) { return graphics->xdim; }

size_t window_height(struct graphics *graphics) { return graphics->ydim; }

int process_queue(struct graphics *graphics) {
  (void)graphics;

  // render any pending updates from the VT
  vt_render(graphics->vt);

  if (graphics->dirty) {
    SDL_RenderPresent(graphics->renderer);
    graphics->dirty = 0;
  }

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_QUIT:
      return 1;
    case SDL_TEXTINPUT:
      fprintf(stderr, "textinput: '%s'\n", event.text.text);
      vt_input(graphics->vt, event.text.text, strlen(event.text.text));
      break;
    case SDL_KEYUP:
      switch (event.key.keysym.sym) {
      case SDLK_RETURN:
      case SDLK_RETURN2:
        vt_input(graphics->vt, "\n", 1);
        break;
      }
    }
  }

  return 0;
}

void link_vt(struct graphics *graphics, struct vt *vt) { graphics->vt = vt; }

void char_at(struct graphics *graphics, int x, int y, char c, int bold,
             int italic) {
  char buf[2] = {c, '\0'};

  int font = FONT_REGULAR;
  if (bold && italic) {
    font = FONT_BOLD_ITALIC;
  } else if (bold) {
    font = FONT_BOLD;
  } else if (italic) {
    font = FONT_ITALIC;
  }

  SDL_Color text_color = {255, 255, 255, 0};
  SDL_Surface *text =
      TTF_RenderText_Solid(graphics->font[font], buf, text_color);
  SDL_Texture *texture = SDL_CreateTextureFromSurface(graphics->renderer, text);
  SDL_FreeSurface(text);

  SDL_Rect target = {(int)(x * (int)graphics->cellw),
                     (int)(y * (int)graphics->cellh), (int)(graphics->cellw),
                     (int)(graphics->cellh)};

  SDL_RenderCopy(graphics->renderer, texture, NULL, &target);
  SDL_DestroyTexture(texture);

  graphics->dirty = 1;
}

void graphics_clear(struct graphics *graphics, int x, int y, int w, int h) {
  SDL_Rect target = {(int)(x * (int)graphics->cellw),
                     (int)(y * (int)graphics->cellh),
                     (int)(w * (int)graphics->cellw),
                     (int)(h * (int)graphics->cellh)};

  SDL_SetRenderDrawColor(graphics->renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(graphics->renderer, &target);
}

