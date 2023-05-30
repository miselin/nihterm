#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nihterm/gfx.h>
#include <nihterm/vt.h>

#define print_error(...) fprintf(stderr, "nihterm: " __VA_ARGS__);

struct damage {
  int x;
  int y;
  int w;
  int h;
  struct damage *next;
};

struct cellattr {
  int bold;
  int underline;
  int blink;
  int reverse;
};

struct cell {
  char c;
  struct cellattr attr;
};

struct vt {
  int pty;

  int rows;
  int cols;

  int cx;
  int cy;

  int margin_top;
  int margin_bottom;

  struct cell *screen;
  struct graphics *graphics;

  int in_sequence;
  char sequence[16];
  int seqidx;

  struct damage *damage;

  // VT100 status/attributes/modes
  int sgr;

  // modes
  struct {
    int kam;
    int irm;
    int srm;
    int lnm;
    int decckm;
    int decanm;
    int deccolm;
    int decsclm;
    int decscnm;
    int decom;
    int decawm;
    int decarm;
    int decpff;
    int decpex;
  } mode;

  struct cellattr current_attr;
};

static int offset(struct vt *vt, int x, int y) { return (y * vt->cols) + x; }

static void set_char_at(struct vt *vt, int x, int y, char c);

static void cursor_fwd(struct vt *vt, int count);
static void cursor_back(struct vt *vt, int count);
static void cursor_home(struct vt *vt);
static void cursor_down(struct vt *vt, int count);
static void cursor_up(struct vt *vt, int count);
static void cursor_to(struct vt *vt, int x, int y);
static void cursor_sol(struct vt *vt);

static void process_char(struct vt *vt, char c);
static void do_sequence(struct vt *vt);

static void mark_damage(struct vt *vt, int x, int y, int w, int h);

static void erase_line(struct vt *vt);
static void erase_line_cursor(struct vt *vt, int before);
static void erase_screen(struct vt *vt);
static void erase_screen_cursor(struct vt *vt, int before);

static void scroll_up(struct vt *vt);
static void scroll_down(struct vt *vt);

// sequence handling
static void handle_bracket_seq(struct vt *vt);
static void handle_reports_seq(struct vt *vt);
static void handle_modes(struct vt *vt, int set);
static void handle_erases(struct vt *vt, int line, int n);
static void handle_pound_seq(struct vt *vt);

static void get_params(const char *sequence, int *params, int *num_params);

struct vt *vt_create(int pty, int rows, int cols) {
  struct vt *vt = (struct vt *)calloc(sizeof(struct vt), 1);
  vt->pty = pty;
  vt->rows = rows;
  vt->cols = cols;
  vt->margin_top = 0;
  vt->margin_bottom = rows - 1;
  vt->screen =
      (struct cell *)calloc(sizeof(struct cell), (size_t)(rows * cols));
  return vt;
}

void vt_destroy(struct vt *vt) {
  free(vt->screen);
  free(vt);
}

void vt_set_graphics(struct vt *vt, struct graphics *graphics) {
  vt->graphics = graphics;
  link_vt(graphics, vt);
}

int vt_process(struct vt *vt, const char *string, size_t length) {
  for (size_t i = 0; i < length && string[i]; i++) {
    process_char(vt, string[i]);
  }

  return 0;
}

ssize_t vt_input(struct vt *vt, const char *string, size_t length) {
  while (1) {
    ssize_t rc = write(vt->pty, string, length);
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }

      fprintf(stderr, "nihterm: vt_input failed: %s\n", strerror(errno));
      return -1;
    } else {
      return rc;
    }
  }
}

void vt_render(struct vt *vt) {
  struct damage *damage = vt->damage;
  while (damage) {
    graphics_clear(vt->graphics, damage->x, damage->y, damage->w, damage->h);

    for (int y = damage->y; y < (damage->y + damage->h); ++y) {
      for (int x = damage->x; x < (damage->x + damage->w); ++x) {
        char_at(vt->graphics, x, y, vt->screen[offset(vt, x, y)].c, 0, 0);
      }
    }

    struct damage *tmp = damage;
    damage = damage->next;
    free(tmp);
  }

  vt->damage = NULL;
}

static void process_char(struct vt *vt, char c) {
  if (vt->in_sequence) {
    vt->sequence[vt->seqidx++] = c;

    if (isdigit(c) || c == '[' || c == ';' || c == '?' || c == '#') {
      return;
    } else {
      do_sequence(vt);
    }
  } else if (c == '\033') {
    vt->in_sequence = 1;
  } else if (isprint(c)) {
    set_char_at(vt, vt->cx, vt->cy, c);
    mark_damage(vt, vt->cx, vt->cy, 1, 1);
    cursor_fwd(vt, 1);
  } else {
    switch (c) {
    case '\010':
      cursor_back(vt, 1);
      break;
    case '\n':
      cursor_down(vt, 1);
      break;
    case '\r':
      cursor_sol(vt);
      break;
    case '\t':
      cursor_fwd(vt, 8);
      break;
    default:
      fprintf(stderr, "nihterm: unknown character: %c/%d\n", c, c);
    }
  }
}

static void do_sequence(struct vt *vt) {
  vt->sequence[vt->seqidx] = '\0';

  fprintf(stderr, "handling sequence %s\n", vt->sequence);

  switch (vt->sequence[0]) {
  case '[':
    handle_bracket_seq(vt);
    break;
  case '#':
    handle_pound_seq(vt);
    break;
  case 'E':
    // NEL: Next Line
    cursor_down(vt, 1);
    cursor_sol(vt);
    break;
  case '\005':
    // ENQ: Enquiry
    write(vt->pty, "\033[?1;2c", 7);
    break;
  default:
    fprintf(stderr, "nihterm: unhandled sequence: %s\n", vt->sequence);
  }

  vt->seqidx = 0;
  vt->in_sequence = 0;
}

static void cursor_fwd(struct vt *vt, int num) {
  vt->cx += num;
  if (vt->cx >= vt->cols) {
    if (vt->mode.decawm) {
      cursor_down(vt, 1);
      cursor_sol(vt);
    } else {
      vt->cx = vt->cols - 1;
    }
  }
}

static void cursor_back(struct vt *vt, int num) {
  if (vt->cx >= num) {
    vt->cx--;
  } else {
    vt->cx = 0;
  }
}

static void cursor_sol(struct vt *vt) { vt->cx = 0; }

static void cursor_home(struct vt *vt) {
  vt->cx = 0;
  vt->cy = vt->margin_top;
}

static void cursor_down(struct vt *vt, int num) {
  vt->cy += num;

  if (vt->cy >= vt->margin_bottom) {
    int lines = vt->cy - vt->margin_bottom + 1;
    for (int line = 0; line < lines; ++line) {
      scroll_up(vt);
    }

    vt->cy = vt->margin_bottom - 1;
  }
}

static void cursor_up(struct vt *vt, int num) {
  vt->cy -= num;

  if (vt->cy <= vt->margin_top) {
    int rows = vt->margin_top - vt->cy + 1;

    for (int i = 0; i < rows; ++i) {
      scroll_down(vt);
    }

    vt->cy = vt->margin_top;
  }
}

static void cursor_to(struct vt *vt, int x, int y) {
  vt->cx = x;
  vt->cy = y;
}

static void mark_damage(struct vt *vt, int x, int y, int w, int h) {
  // TODO(miselin): merge regions if they overlap appropriately
  struct damage *damage = (struct damage *)calloc(sizeof(struct damage), 1);
  damage->x = x;
  damage->y = y;
  damage->w = w;
  damage->h = h;
  damage->next = vt->damage;
  vt->damage = damage;
}

static void get_params(const char *sequence, int *params, int *num_params) {
  int i = 0;
  int num = 0;
  int has_digit = 0;
  while (sequence[i]) {
    if (isdigit(sequence[i])) {
      num = num * 10 + (sequence[i] - '0');
      has_digit = 1;
    } else if (sequence[i] == ';') {
      params[(*num_params)++] = num;
      num = 0;
      has_digit = 0;
    } else {
      if (has_digit) {
        params[(*num_params)++] = num;
      }
      return;
    }

    ++i;
  }

  params[(*num_params)++] = num;
}

static void handle_bracket_seq(struct vt *vt) {
  int params[6] = {0};
  int num_params = 0;

  get_params(vt->sequence + 1, params, &num_params);

  char last = vt->sequence[vt->seqidx - 1];
  switch (last) {
  case 'r':
    fprintf(stderr, "DECSTBM: num=%d %d %d [%s]\n", num_params, params[0],
            params[1], vt->sequence);
    if (num_params == 0) {
      vt->margin_top = 0;
      vt->margin_bottom = vt->rows - 1;
    } else if (num_params != 2) {
      print_error("DECSTBM: expected 0 or 2 parameters: %s\n", vt->sequence);
    } else {
      vt->margin_top = params[0] - 1;
      vt->margin_bottom = params[1] - 1;
    }

    cursor_home(vt);
    break;
  case 'J':
  case 'K':
    handle_erases(vt, last == 'J' ? 0 : 1, num_params == 1 ? params[0] : 0);
    break;
  case 'l':
  case 'h':
    handle_modes(vt, last == 'l' ? 0 : 1);
    break;
  case 'c':
  case 'Z':
    // Identify Terminal
    // report VT102
    write(vt->pty, "\033[?6c", 5);
    break;
  case 'n':
    handle_reports_seq(vt);
    break;
  case 'A':
    if (num_params != 1) {
      print_error("CUU: expected 1 parameter\n");
    } else {
      cursor_up(vt, params[0]);
    }
    break;
  case 'B':
    if (num_params != 1) {
      print_error("CUD: expected 1 parameter\n");
    } else {
      cursor_down(vt, params[0]);
    }
    break;
  case 'C':
    if (num_params != 1) {
      print_error("CUF: expected 1 parameter\n");
    } else {
      cursor_fwd(vt, params[0]);
    }
    break;
  case 'D':
    if (num_params != 1) {
      print_error("CUB: expected 1 parameter\n");
    } else {
      cursor_back(vt, params[0]);
    }
    break;
  case 'H':
    // fall through
  case 'f':
    if (vt->seqidx == 2) {
      // CUP/HVP: Home
      cursor_home(vt);
    } else if (num_params == 2) {
      // CUP/HVP: position
      // line ; column
      cursor_to(vt, params[1] - 1, params[0] - 1);
    } else {
      print_error("CUP/HVP: expected 0 or 2 parameters\n");
    }
    break;
  case 'm':
    if (num_params == 0 || params[0] == 0) {
      vt->current_attr.bold = 0;
      vt->current_attr.blink = 0;
      vt->current_attr.reverse = 0;
      vt->current_attr.underline = 0;
    } else if (params[0] == 1) {
      vt->current_attr.bold = 1;
    } else if (params[0] == 4) {
      vt->current_attr.underline = 1;
    } else if (params[0] == 5) {
      vt->current_attr.blink = 1;
    } else if (params[0] == 7) {
      vt->current_attr.reverse = 1;
    }
    break;
  case 'P':
    // DCH: Delete Character
    memmove(&vt->screen[offset(vt, vt->cx, vt->cy)],
            &vt->screen[offset(vt, vt->cx + 1, vt->cy)], vt->cols - vt->cx - 1);
    break;
  default:
    fprintf(stderr, "unhandled bracket sequence %c\n", last);
  }
}

static void handle_reports_seq(struct vt *vt) {
  int param = -1;
  char question = vt->sequence[1];
  if (question == '?') {
    int rc = sscanf(vt->sequence, "[?%d", &param);
    if (rc != 1) {
      fprintf(stderr, "nihterm: failed to parse sequence: %s\n", vt->sequence);
      return;
    }

    switch (param) {
    case 15:
      // Device Status Report (Printer)
      // report no printer
      write(vt->pty, "\033[?13n", 6);
      break;
    default:
      fprintf(stderr, "nihterm: unknown DSR request: %s\n", vt->sequence);
    }
  } else {
    int rc = sscanf(vt->sequence, "[%d", &param);
    if (rc != 1) {
      fprintf(stderr, "nihterm: failed to parse sequence: %s\n", vt->sequence);
      return;
    }

    switch (param) {
    case 5:
      // Device Status Report (VT102)
      // report OK
      write(vt->pty, "\033[0n", 4);
      break;
    case 6:
      // Device Status Report (cursor position)
      dprintf(vt->pty, "\033[%d;%dR", vt->cy + 1, vt->cx + 1);
      break;
    }
  }
}

static void handle_modes(struct vt *vt, int set) {
  if (vt->sequence[1] == '?') {
    int param = 0;
    int count = sscanf(vt->sequence, "[?%d", &param);
    if (count != 1) {
      print_error("failed to parse DEC mode sequence '%s'\n", vt->sequence);
    }

    fprintf(stderr, "DEC mode %d: %d\n", set, param);

    switch (param) {
    case 1:
      // DECCKM (set = Application, reset = Cursor)
      vt->mode.decckm = set;
      break;
    case 2:
      // DECANM (set = ANSI, reset = VT52)
      vt->mode.decanm = set;
      break;
    case 3:
      // DECCOLM (set = 132, reset = 80)
      vt->mode.deccolm = set;
      break;
    case 4:
      // DECSCLM (set = Smooth, reset = Jump)
      vt->mode.decsclm = set;
      break;
    case 5:
      // DECSCNM (set = Reverse, reset = Normal)
      vt->mode.decscnm = set;
      break;
    case 6:
      // DECOM (set = Relative, reset = Absolute)
      vt->mode.decom = set;
      break;
    case 7:
      // DECAWM (set = Wrap, reset = No Wrap)
      vt->mode.decawm = set;
      break;
    case 8:
      // DECARM (set = On, reset = Off)
      vt->mode.decarm = set;
      break;
    case 18:
      // DECPFF (set = On, reset = Off)
      vt->mode.decpff = set;
      break;
    case 19:
      // DECPEX (set = On, reset = Off)
      vt->mode.decpex = set;
      break;
    default:
      print_error("unknown DEC mode %d\n", param);
    }
    return;
  }

  int param = 0;
  int count = sscanf(vt->sequence, "[%d", &param);
  if (count != 1) {
    print_error("failed to parse mode sequence '%s'\n", vt->sequence);
    return;
  }

  fprintf(stderr, "mode %d: %d\n", set, param);

  switch (param) {
  case 2:
    // KAM (set = Locked, reset = Unlocked)
    vt->mode.kam = set;
    break;
  case 4:
    // IRM (set = Insert, reset = Replace)
    vt->mode.irm = set;
    break;
  case 12:
    // SRM (set = Local, reset = Remote)
    vt->mode.srm = set;
    break;
  case 20:
    // LNM (set = Newline, reset = Linefeed)
    vt->mode.lnm = set;
    break;
  default:
    print_error("unknown mode for set/reset: %d", param);
  }
}

static void handle_erases(struct vt *vt, int line, int n) {
  switch (n) {
  case 0:
    // cursor to end of line/screen
    if (line == 1) {
      erase_line_cursor(vt, 0);
    } else {
      erase_screen_cursor(vt, 0);
    }
    break;
  case 1:
    // cursor to start of line/screen
    if (line == 1) {
      erase_line_cursor(vt, 1);
    } else {
      erase_screen_cursor(vt, 1);
    }
    break;
  case 2:
    // clear entire line/screen
    if (line == 1) {
      erase_line(vt);
    } else {
      erase_screen(vt);
    }
    break;
  }
}

static void erase_line(struct vt *vt) {
  for (int x = 0; x < vt->cols; ++x) {
    set_char_at(vt, x, vt->cy, ' ');
  }

  mark_damage(vt, 0, vt->cy, vt->cols, 1);
}

static void erase_screen(struct vt *vt) {
  for (int y = 0; y < vt->rows; ++y) {
    for (int x = 0; x < vt->cols; ++x) {
      set_char_at(vt, x, y, ' ');
    }
  }

  mark_damage(vt, 0, 0, vt->cols, vt->rows);
}

static void erase_line_cursor(struct vt *vt, int before) {
  int sx = before ? 0 : vt->cx;
  int ex = before ? vt->cx : vt->cols;
  for (int x = sx; x < ex; ++x) {
    set_char_at(vt, x, vt->cy, ' ');
  }

  mark_damage(vt, sx, vt->cy, ex - sx, 1);
}

static void erase_screen_cursor(struct vt *vt, int before) {
  int sy = before ? 0 : vt->cy;
  int ey = before ? vt->cy : vt->rows;

  for (int y = sy; y < ey; ++y) {
    for (int x = 0; x < vt->cols; ++x) {
      set_char_at(vt, x, y, ' ');
    }
  }

  mark_damage(vt, 0, sy, vt->cols, ey - sy);
}

static void set_char_at(struct vt *vt, int x, int y, char c) {
  if (x >= vt->cols || y >= vt->rows) {
    print_error("set_char_at out of bounds (%d, %d)\n", x, y);
    return;
  }

  // fprintf(stderr, "set_char_at %p %d %d [%d]\n", (void *)vt, x, y, vt->cols);
  struct cell *cell = &vt->screen[offset(vt, x, y)];
  cell->c = c;
  cell->attr = vt->current_attr;
}

static void scroll_up(struct vt *vt) {
  fprintf(stderr, "scroll_up: %d %d [%d..%d]\n", vt->rows, vt->cy,
          vt->margin_top, vt->margin_bottom);

  int rows = vt->margin_bottom - vt->margin_top;

  memmove(vt->screen + (vt->margin_top * vt->cols),
          vt->screen + ((vt->margin_top + 1) * vt->cols),
          (size_t)((vt->cols * rows) * (int)sizeof(struct cell)));

  for (int x = 0; x < vt->cols; ++x) {
    set_char_at(vt, x, vt->margin_bottom, ' ');
  }

  mark_damage(vt, 0, vt->margin_top, vt->cols, vt->margin_bottom);
}

static void scroll_down(struct vt *vt) {
  fprintf(stderr, "scroll_down: %d %d [%d..%d]\n", vt->rows, vt->cy,
          vt->margin_top, vt->margin_bottom);

  int rows = vt->margin_bottom - vt->margin_top;

  memmove(vt->screen + ((vt->margin_top + 1) * vt->cols),
          vt->screen + (vt->margin_top * vt->cols),
          (size_t)((vt->cols * rows) * (int)sizeof(struct cell)));

  for (int x = 0; x < vt->cols; ++x) {
    set_char_at(vt, x, vt->margin_top, ' ');
  }

  mark_damage(vt, 0, vt->margin_top, vt->cols, vt->margin_bottom);
}

static void handle_pound_seq(struct vt *vt) {
  switch (vt->sequence[1]) {
  case '8':
    // DECALN
    for (int y = 0; y < vt->rows; ++y) {
      for (int x = 0; x < vt->cols; ++x) {
        set_char_at(vt, x, y, 'E');
      }
    }
    break;
  default:
    print_error("unknown pound sequence: %s\n", vt->sequence);
  }
}
