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
  struct cell *next;
};

struct row {
  struct cell *cells;
  struct row *next;
  int dirty;
};

struct vt {
  int pty;

  int rows;
  int cols;

  int cx;
  int cy;

  int margin_top;
  int margin_bottom;

  struct row *screen;

  struct row *margin_top_row;
  struct row *margin_bottom_row;

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

  int redraw_all;
};

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
static void handle_reports_seq(struct vt *vt, int *params, int num_params);
static void handle_modes(struct vt *vt, int set);
static void handle_erases(struct vt *vt, int line, int n);
static void handle_pound_seq(struct vt *vt);

static void get_params(const char *sequence, int *params, int *num_params);

struct row *get_row(struct vt *vt, int y, struct row **prev);
struct cell *get_cell(struct row *row, int x, struct cell **prev);

static struct row *append_line(struct vt *vt, struct row *after);
static void append_cell(struct vt *vt, struct row *row);

static void delete_character(struct vt *vt);
static void delete_line(struct vt *vt);
static void insert_line(struct vt *vt);

static ssize_t write_retry(int fd, const char *buffer, size_t length);

struct vt *vt_create(int pty, int rows, int cols) {
  struct vt *vt = (struct vt *)calloc(sizeof(struct vt), 1);
  vt->pty = pty;
  vt->rows = rows;
  vt->cols = cols;
  vt->margin_top = 0;
  vt->margin_bottom = rows - 1;
  vt->screen = NULL;
  struct row *prev = NULL;
  for (int i = 0; i < rows; i++) {
    prev = append_line(vt, prev);
  }
  return vt;
}

void vt_destroy(struct vt *vt) {
  struct row *row = vt->screen;
  while (row) {
    struct cell *cell = row->cells;
    while (cell) {
      struct cell *tmp = cell;
      cell = cell->next;
      free(tmp);
    }

    struct row *tmp = row;
    row = row->next;
    free(tmp);
  }

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
  return write_retry(vt->pty, string, length);
}

void vt_render(struct vt *vt) {
  struct damage *damage = vt->damage;
  while (damage) {
    struct damage *tmp = damage;
    damage = damage->next;
    free(tmp);
  }

  vt->damage = NULL;

  if (!vt->graphics) {
    return;
  }

  struct row *row = vt->screen;
  int y = 0;
  while (row) {
    if (row->dirty || vt->redraw_all) {
      row->dirty = 0;

      graphics_clear(vt->graphics, 0, y, vt->cols, 1);

      struct cell *cell = row->cells;
      int x = 0;
      while (cell) {
        char_at(vt->graphics, x++, y, cell->c, 0, 0);
        cell = cell->next;
      }
    }

    row = row->next;
    ++y;
  }

  vt->redraw_all = 0;
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
    write_retry(vt->pty, "\033[?1;2c", 7);
    break;
  case 'D':
    // Index
    cursor_down(vt, 1);
    break;
  case 'M':
    // Reverse Index
    cursor_up(vt, 1);
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
  int params[6] = {1, 1, 1, 1, 1, 1};
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
    write_retry(vt->pty, "\033[?6c", 5);
    break;
  case 'n':
    handle_reports_seq(vt, params, num_params);
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
    } else {
      // CUP/HVP: position
      // line ; column, default value is 1
      cursor_to(vt, params[1] - 1, params[0] - 1);
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
    for (int i = 0; i < params[0]; ++i) {
      delete_character(vt);
    }
    break;
  case 'L':
    // IL: Insert Line
    for (int i = 0; i < params[0]; ++i) {
      insert_line(vt);
    }
    break;
  case 'M':
    // DL: Delete Line
    for (int i = 0; i < params[0]; ++i) {
      delete_line(vt);
    }
    break;
  default:
    fprintf(stderr, "unhandled bracket sequence %c\n", last);
  }
}

static void handle_reports_seq(struct vt *vt, int *params, int num_params) {
  (void)num_params;

  char question = vt->sequence[1];
  if (question == '?') {
    switch (params[0]) {
    case 15:
      // Device Status Report (Printer)
      // report no printer
      write_retry(vt->pty, "\033[?13n", 6);
      break;
    default:
      fprintf(stderr, "nihterm: unknown DSR request: %s\n", vt->sequence);
    }
  } else {
    switch (params[0]) {
    case 5:
      // Device Status Report (VT102)
      // report OK
      write_retry(vt->pty, "\033[0n", 4);
      break;
    case 6:
      // Device Status Report (cursor position)
      fprintf(stderr, "writing cursor pos...\n");
      int rc = dprintf(vt->pty, "\033[%d;%dR", vt->cy + 1, vt->cx + 1);
      if (rc <= 0) {
        print_error("failed to write cursor position: %s\n", strerror(errno));
      } else {
        fprintf(stderr, "wrote %d bytes to %d\n", rc, vt->pty);
      }
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

  struct row *row = get_row(vt, y, NULL);
  if (!row) {
    print_error("set_char_at failed to get row %d\n", y);
    return;
  }

  struct cell *cell = get_cell(row, x, NULL);
  if (!cell) {
    print_error("set_char_at failed to get cell at x=%d y=%d\n", x, y);
    return;
  }

  cell->c = c;
  cell->attr = vt->current_attr;

  row->dirty = 1;
}

static void scroll_up(struct vt *vt) {
  fprintf(stderr, "scroll_up: %d %d [%d..%d]\n", vt->rows, vt->cy,
          vt->margin_top, vt->margin_bottom);

  // int rows = vt->margin_bottom - vt->margin_top;

  struct row *prev = NULL;
  struct row *row = get_row(vt, vt->margin_top, &prev);

  if (prev) {
    prev->next = row->next;
  } else {
    vt->screen = row->next;
  }

  append_line(vt, NULL);

  free(row);

  vt->redraw_all = 1;

  mark_damage(vt, 0, vt->margin_top, vt->cols, vt->margin_bottom);
}

static void scroll_down(struct vt *vt) {
  fprintf(stderr, "scroll_down: %d %d [%d..%d]\n", vt->rows, vt->cy,
          vt->margin_top, vt->margin_bottom);

  // int rows = vt->margin_bottom - vt->margin_top;

  struct row *prev = NULL;
  struct row *row = get_row(vt, vt->margin_top, &prev);

  append_line(vt, prev);

  row = get_row(vt, vt->margin_bottom, &prev);

  prev->next = row->next;
  free(row);

  vt->redraw_all = 1;

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

struct row *get_row(struct vt *vt, int y, struct row **prev) {
  struct row *row = vt->screen;
  while (row && y--) {
    if (prev) {
      *prev = row;
    }

    row = row->next;
  }

  return row;
}

struct cell *get_cell(struct row *row, int x, struct cell **prev) {
  struct cell *cell = row->cells;
  while (cell && x--) {
    if (prev) {
      *prev = cell;
    }

    cell = cell->next;
  }

  return cell;
}

static struct row *append_line(struct vt *vt, struct row *after) {
  struct row *row = after;
  if (!after) {
    // append to end of screen
    row = vt->screen;
    while (row) {
      if (!row->next) {
        break;
      }

      row = row->next;
    }
  }

  struct row *new_row = calloc(1, sizeof(struct row));
  for (int i = 0; i < vt->cols; ++i) {
    append_cell(vt, new_row);
  }

  if (row) {
    new_row->next = row->next;
    row->next = new_row;
  } else {
    new_row->next = vt->screen;
    vt->screen = new_row;
  }

  return new_row;
}

static void append_cell(struct vt *vt, struct row *row) {
  struct cell *new_cell = calloc(1, sizeof(struct cell));
  new_cell->c = ' ';
  new_cell->attr = vt->current_attr;

  struct cell *cell = row->cells;
  if (!cell) {
    row->cells = new_cell;
    return;
  }

  while (cell->next) {
    cell = cell->next;
  }

  cell->next = new_cell;
}

static void delete_character(struct vt *vt) {
  struct cell *prev = NULL;
  struct row *row = get_row(vt, vt->cy, NULL);
  struct cell *cell = get_cell(row, vt->cx, &prev);
  if (!prev) {
    row->cells = cell->next;
  } else {
    prev->next = cell->next;
  }

  append_cell(vt, row);

  free(cell);

  row->dirty = 1;
}

static void delete_line(struct vt *vt) {
  struct row *prev = NULL;
  struct row *row = get_row(vt, vt->cy, &prev);
  if (!prev) {
    vt->screen = row->next;
  } else {
    prev->next = row->next;
  }

  // lazy
  vt->redraw_all = 1;

  free(row);

  append_line(vt, NULL);
}

static void insert_line(struct vt *vt) {
  struct row *row = get_row(vt, vt->cy, NULL);
  append_line(vt, row);

  // lazy
  vt->redraw_all = 1;
}

void vt_fill(struct vt *vt, char **buffer) {
  *buffer = calloc(1, (size_t)((vt->rows * (vt->cols + 1)) + 1));

  struct row *row = vt->screen;
  int y = 0;
  while (row && y < vt->rows) {
    struct cell *cell = row->cells;
    int x = 0;
    while (cell && x < vt->cols) {
      (*buffer)[(y * (vt->cols + 1)) + x] = cell->c;
      cell = cell->next;
      ++x;
    }

    (*buffer)[(y * (vt->cols + 1)) + x] = '\n';

    row = row->next;
    ++y;
  }
}

static ssize_t write_retry(int fd, const char *buffer, size_t length) {
  while (1) {
    ssize_t rc = write(fd, buffer, length);
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }

      fprintf(stderr, "nihterm: write_retry failed: %s\n", strerror(errno));
    }

    return rc;
  }
}
