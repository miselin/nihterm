#include <stdint.h>

#include <SDL2/SDL.h>

#include <nihterm/gfx.h>
#include <nihterm/vt.h>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#define FONT_REGULAR 0
#define FONT_BOLD 1
#define FONT_ITALIC 2
#define FONT_BOLD_ITALIC 3

static int load_fonts(struct graphics *graphics);

struct graphics {
  SDL_Window *window;
  SDL_Renderer *renderer;
  PangoFontDescription *font[4];

  size_t xdim;
  size_t ydim;

  size_t cellw;
  size_t cellh;

  struct vt *vt;

  int dirty;

  int inverted;
};

struct graphics *create_graphics() {
  SDL_Init(SDL_INIT_VIDEO);

  struct graphics *graphics =
      (struct graphics *)calloc(sizeof(struct graphics), 1);

  if (load_fonts(graphics)) {
    fprintf(stderr, "nihterm: failed to load fonts\n");
    free(graphics);
    return NULL;
  }

  PangoFontMetrics *metrics = 0;
  PangoFontMap *fontmap = pango_cairo_font_map_get_default();
  PangoContext *context = pango_font_map_create_context(fontmap);
  pango_context_set_font_description(context, graphics->font[FONT_REGULAR]);
  metrics =
      pango_context_get_metrics(context, graphics->font[FONT_REGULAR], NULL);
  g_object_unref(context);

  graphics->cellw =
      pango_font_metrics_get_approximate_digit_width(metrics) / PANGO_SCALE;
  graphics->cellh = (pango_font_metrics_get_ascent(metrics) +
                     pango_font_metrics_get_descent(metrics)) /
                    PANGO_SCALE;

  pango_font_metrics_unref(metrics);

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

  PangoFontDescription *font_desc =
      pango_font_description_from_string("Courier Prime 12");
  if (!font_desc) {
    return 1;
  }

  graphics->font[FONT_REGULAR] = font_desc;

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
  // render any pending updates from the VT
  vt_render(graphics->vt);

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
      break;
    case SDL_WINDOWEVENT:
      switch (event.window.event) {
      case SDL_WINDOWEVENT_EXPOSED:
        // requires a redraw
        graphics->dirty = 1;
        break;
      }
      break;
    }
  }

  if (graphics->dirty) {
    SDL_RenderPresent(graphics->renderer);
    graphics->dirty = 0;
  }

  return 0;
}

void link_vt(struct graphics *graphics, struct vt *vt) { graphics->vt = vt; }

void char_at(struct graphics *graphics, int x, int y, struct cell *cell) {
  PangoAttrList *attrs = pango_attr_list_new();
  if (cell->attr.bold) {
    PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    pango_attr_list_insert(attrs, attr);
  }
  if (cell->attr.underline) {
    PangoAttribute *attr = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
    pango_attr_list_insert(attrs, attr);
  }

  SDL_Texture *texture = SDL_CreateTexture(
      graphics->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
      graphics->cellw, graphics->cellh);

  void *pixels;
  int pitch;
  SDL_LockTexture(texture, NULL, &pixels, &pitch);

  cairo_surface_t *cairo_surface = cairo_image_surface_create_for_data(
      pixels, CAIRO_FORMAT_ARGB32, graphics->cellw, graphics->cellh, pitch);

  cairo_t *cr = cairo_create(cairo_surface);

  PangoLayout *layout = pango_cairo_create_layout(cr);

  pango_layout_set_attributes(layout, attrs);
  pango_layout_set_font_description(layout, graphics->font[FONT_REGULAR]);
  pango_layout_set_text(layout, cell->cp, cell->cp_len);
  pango_attr_list_unref(attrs);

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  if (cell->attr.reverse ^ graphics->inverted) {
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
  } else {
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
  }

  cairo_rectangle(cr, 0, 0, graphics->cellw, graphics->cellh);
  cairo_fill(cr);

  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  if (cell->attr.reverse ^ graphics->inverted) {
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
  } else {
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
  }

  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);

  cairo_destroy(cr);

  SDL_UnlockTexture(texture);

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

  if (graphics->inverted) {
    SDL_SetRenderDrawColor(graphics->renderer, 1, 1, 1, SDL_ALPHA_OPAQUE);
  } else {
    SDL_SetRenderDrawColor(graphics->renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
  }
  SDL_RenderFillRect(graphics->renderer, &target);
}

void graphics_resize(struct graphics *graphics, int cols, int rows) {
  size_t new_xdim = (size_t)cols * graphics->cellw;
  size_t new_ydim = (size_t)rows * graphics->cellh;
  if (new_xdim == graphics->xdim && new_ydim == graphics->ydim) {
    return;
  }

  graphics->xdim = new_xdim;
  graphics->ydim = new_ydim;

  SDL_SetWindowSize(graphics->window, (int)new_xdim, (int)new_ydim);
}

void graphics_invert(struct graphics *graphics, int invert) {
  graphics->inverted = invert;
  graphics->dirty = 1;
}
