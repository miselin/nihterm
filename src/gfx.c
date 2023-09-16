#include <stdint.h>
#include <string.h>

#include <SDL2/SDL.h>

#include <nihterm/gfx.h>
#include <nihterm/vt.h>

#include <cairo/cairo.h>
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>

#define FONT_REGULAR 0
#define FONT_DOUBLE 1

static int load_fonts(struct graphics *graphics);

struct graphics {
  SDL_Window *window;
  SDL_Surface *surface;
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
    (size_t)(pango_font_metrics_get_approximate_digit_width(metrics) / PANGO_SCALE);
  graphics->cellh = (size_t)((pango_font_metrics_get_ascent(metrics) +
                     pango_font_metrics_get_descent(metrics)) /
                             PANGO_SCALE);

  pango_font_metrics_unref(metrics);

  graphics->xdim = graphics->cellw * 80;
  graphics->ydim = graphics->cellh * 25;

  graphics->window = SDL_CreateWindow(
      "nihterm", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      (int)graphics->xdim, (int)graphics->ydim, 0);

  graphics->surface = SDL_GetWindowSurface(graphics->window);
  SDL_FillRect(graphics->surface, NULL, 0);
  SDL_UpdateWindowSurface(graphics->window);

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

  font_desc = pango_font_description_from_string("Courier Prime 24");
  if (!font_desc) {
    return 1;
  }

  graphics->font[FONT_DOUBLE] = font_desc;

  return 0;
}

void destroy_graphics(struct graphics *graphics) {
  SDL_DestroyWindow(graphics->window);
  SDL_Quit();

  pango_cairo_font_map_set_default(NULL);

  pango_font_description_free(graphics->font[FONT_REGULAR]);
  pango_font_description_free(graphics->font[FONT_DOUBLE]);

  FcFini();

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
      //case SDL_TEXTINPUT:
      //fprintf(stderr, "textinput: '%s'\n", event.text.text);
      //vt_input(graphics->vt, event.text.text, strlen(event.text.text));
      //break;
    case SDL_KEYUP:
      // fprintf(stderr, "sym %d/%c; mod %x\n", event.key.keysym.sym, event.key.keysym.sym, event.key.keysym.mod);
      switch (event.key.keysym.sym) {
      case SDLK_RETURN:
      case SDLK_RETURN2:
        vt_input(graphics->vt, "\r", 1);
        break;
      case SDLK_BACKSPACE:
        vt_input(graphics->vt, "\b", 1);
        break;
      case SDLK_LEFT:
        // TODO(miselin): there's mode setting at play here
        vt_input(graphics->vt, "\033[D", 3);
        break;
      case SDLK_RIGHT:
        // TODO(miselin): there's mode setting at play here
        vt_input(graphics->vt, "\033[C", 3);
        break;
      case SDLK_UP:
        // TODO(miselin): there's mode setting at play here
        vt_input(graphics->vt, "\033[A", 3);
        break;
      case SDLK_DOWN:
        // TODO(miselin): there's mode setting at play here
        vt_input(graphics->vt, "\033[B", 3);
        break;
      default:
        if (event.key.keysym.sym >= 0x20 && event.key.keysym.sym <= 0x7F) {
          char buf[4] = {(char)event.key.keysym.sym, 0, 0, 0};
          size_t buf_len = 1;

          // ctrl-<key>
          if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL)) {
            if (buf[0] == ' ') {
              buf[0] = '\0';
            } else if (buf[0] == '`') {
              buf[0] = '\036';
            } else if (buf[0] == '?') {
              buf[0] = '\036';
            } else if (toupper(buf[0]) >= 'A' && toupper(buf[0]) <= ']') {
              // A = \001, B = \002, etc
              buf[0] = (char) (toupper(buf[0]) - '@');
            }
          } else if (event.key.keysym.mod & KMOD_SHIFT) {
            if (event.key.keysym.mod & KMOD_CAPS) {
              buf[0] = (char)tolower(buf[0]);
            }

            switch (buf[0]) {
            case '0':
              buf[0] = ')';
              break;
            case '1':
              buf[0] = '!';
              break;
            case '2':
              buf[0] = '@';
              break;
            case '3':
              buf[0] = '#';
              break;
            case '4':
              buf[0] = '$';
              break;
            case '5':
              buf[0] = '%';
              break;
            case '6':
              buf[0] = '^';
              break;
            case '7':
              buf[0] = '&';
              break;
            case '8':
              buf[0] = '*';
              break;
            case '9':
              buf[0] = '(';
              break;
            case '-':
              buf[0] = '_';
              break;
            case '=':
              buf[0] = '+';
              break;
            case '`':
              buf[0] = '~';
              break;
            case '[':
              buf[0] = '{';
              break;
            case ']':
              buf[0] = '}';
              break;
            case ';':
              buf[0] = ':';
              break;
            case '\'':
              buf[0] = '"';
              break;
            case '.':
              buf[0] = '>';
              break;
            case ',':
              buf[0] = '<';
              break;
            case '/':
              buf[0] = '?';
              break;
            default:
              buf[0] = (char)toupper(buf[0]);
            }
          } else if (event.key.keysym.mod & KMOD_CAPS) {
            buf[0] = (char)toupper(buf[0]);
          }

          vt_input(graphics->vt, buf, buf_len);
        }
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
    SDL_UpdateWindowSurface(graphics->window);
    graphics->dirty = 0;
  }

  return 0;
}

void link_vt(struct graphics *graphics, struct vt *vt) { graphics->vt = vt; }

void char_at(struct graphics *graphics, int x, int y, struct cell *cell,
             int dblwide, int dblheight) {
  chars_at(graphics, x, y, cell, 1, dblwide, dblheight);
}

void chars_at(struct graphics *graphics, int x, int y, struct cell *cells, int count, int dblwide, int dblheight) {
  int font_type = FONT_REGULAR;

  int cellw = (int) graphics->cellw;
  int cellh = (int) graphics->cellh;

  int srcw = cellw;
  int srch = cellh;

  if (dblheight) {
    srch *= 2;
    srcw *= 2;

    // destination is widened and heightened
    cellw *= 2;
    cellh *= 2;

    font_type = FONT_DOUBLE;
  } else if (dblwide) {
    // destination is only widened
    cellw *= 2;

    // double-wide source
    srch *= 2;
    srcw *= 2;

    // we use the double font but it'll get scaled down to half vertical size
    // this should look better than the small font scaled _up_ though
    font_type = FONT_DOUBLE;
  }
  
  SDL_Surface *surface =
    SDL_CreateRGBSurface(0, srcw * count, srch, 32, 0, 0, 0, 0);

  SDL_LockSurface(surface);

  void *pixels = surface->pixels;
  int pitch = surface->pitch;

  cairo_surface_t *cairo_surface = cairo_image_surface_create_for_data(pixels, CAIRO_FORMAT_ARGB32, srcw * count, srch, pitch);

  cairo_t *cr = cairo_create(cairo_surface);

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  if (graphics->inverted) {
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
  } else {
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
  }

  cairo_rectangle(cr, count * cellw, 0, cellw, cellh);
  cairo_fill(cr);
    
  for (int i = 0; i < count; ++i) {
    PangoLayout *layout = pango_cairo_create_layout(cr);

    struct cell *cell = cells + i;

    PangoAttrList *attrs = pango_attr_list_new();
    if (cell->attr.bold) {
      PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
      pango_attr_list_insert(attrs, attr);
    }
    if (cell->attr.underline) {
      PangoAttribute *attr = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
      pango_attr_list_insert(attrs, attr);
    }

    pango_layout_set_attributes(layout, attrs);
    pango_layout_set_font_description(layout, graphics->font[font_type]);
    pango_layout_set_text(layout, cell->cp, cell->cp_len);
    pango_attr_list_unref(attrs);

    // fix bg color for reversed cell
    if (cell->attr.reverse) {
      cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
      if (graphics->inverted) {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
      } else {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
      }

      cairo_rectangle(cr, count * cellw, 0, cellw, cellh);
      cairo_fill(cr);
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if (cell->attr.reverse ^ graphics->inverted) {
      cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    } else {
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    }

    cairo_move_to(cr, i * srcw, 0);

    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
  }

  cairo_destroy(cr);

  cairo_surface_destroy(cairo_surface);

  SDL_UnlockSurface(surface);

  SDL_Rect target = {(int)(x * cellw), (int)(y * (int)graphics->cellh), count * cellw,
                     (int)graphics->cellh};

  SDL_Rect source = {0, dblheight == 2 ? (int)graphics->cellh : 0, count * cellw,
                     dblwide ? srch : (int)graphics->cellh};

  SDL_BlitSurface(surface, &source, graphics->surface, &target);
  SDL_FreeSurface(surface);

  graphics->dirty = 1;
}

void graphics_clear(struct graphics *graphics, int x, int y, int w, int h) {
  SDL_Rect target = {(int)(x * (int)graphics->cellw),
                     (int)(y * (int)graphics->cellh),
                     (int)(w * (int)graphics->cellw),
                     (int)(h * (int)graphics->cellh)};

  SDL_FillRect(graphics->surface, &target, graphics->inverted ? 0xFFFFFF : 0);
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

  // resize invalidates the existing surface
  graphics->surface = SDL_GetWindowSurface(graphics->window);
}

void graphics_invert(struct graphics *graphics, int invert) {
  graphics->inverted = invert;
  graphics->dirty = 1;
}
