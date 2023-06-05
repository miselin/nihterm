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

struct row {
  struct cell cells[132];
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

  int margin_left;
  int margin_right;

  struct row *screen;

  struct row *margin_top_row;
  struct row *margin_bottom_row;

  struct graphics *graphics;

  int in_sequence;
  char sequence[64];
  int seqidx;

  struct damage *damage;

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

  // G0/G1
  int charset;

  struct cellattr current_attr;

  int saved_x;
  int saved_y;
  int saved_charset;
  struct cellattr saved_attr;

  int redraw_all;

  char tabstops[132];
};

static void set_char_at(struct vt *vt, int x, int y, char c);
static void insert_char_at(struct vt *vt, int x, int y, char c);

static void cursor_fwd(struct vt *vt, int count);
static void cursor_back(struct vt *vt, int count);
static void cursor_home(struct vt *vt);
static void cursor_down(struct vt *vt, int count, int scroll);
static void cursor_up(struct vt *vt, int count, int scroll);
static void cursor_to(struct vt *vt, int x, int y, int scroll);
static void cursor_sol(struct vt *vt);

static void process_char(struct vt *vt, char c);
static void do_sequence(struct vt *vt);
static void do_vt52(struct vt *vt);
static void end_sequence(struct vt *vt);

static void mark_damage(struct vt *vt, int x, int y, int w, int h);

static void erase_line(struct vt *vt);
static void erase_line_cursor(struct vt *vt, int before);
static void erase_screen(struct vt *vt);
static void erase_screen_cursor(struct vt *vt, int before);

static void scroll_up(struct vt *vt);
static void scroll_down(struct vt *vt);

// sequence handling
static void handle_bracket_seq(struct vt *vt);
static void handle_paren_seq(struct vt *vt);
static void handle_reports_seq(struct vt *vt, int *params, int num_params);
static void handle_modes(struct vt *vt, int set);
static void handle_erases(struct vt *vt, int line, int n);
static void handle_pound_seq(struct vt *vt);

static void get_params(const char *sequence, int *params, int *num_params);

struct row *get_row(struct vt *vt, int y, struct row **prev);

void free_row(struct row *row);

static struct row *append_line(struct vt *vt, struct row *after);
static struct row *screen_insert_line(struct vt *vt, struct row *prev);

static void delete_character(struct vt *vt);
static void delete_line(struct vt *vt);
static void insert_line(struct vt *vt);

static int next_tabstop(struct vt *vt, int x);

static ssize_t write_retry(int fd, const char *buffer, size_t length);

struct vt *vt_create(int pty, int rows, int cols) {
  struct vt *vt = (struct vt *)calloc(sizeof(struct vt), 1);
  vt->pty = pty;
  vt->rows = rows;
  vt->cols = cols;
  vt->margin_top = 0;
  vt->margin_bottom = rows - 1;
  vt->margin_left = 0;
  vt->margin_right = cols;
  vt->screen = NULL;
  struct row *prev = NULL;
  for (int i = 0; i < rows; i++) {
    prev = append_line(vt, prev);
  }
  // set default tab stops (every 8 chars)
  for (int i = 0; i < 132; i++) {
    vt->tabstops[i] = (i % 8 == 0);
  }

  // set default modes
  vt->mode.decanm = 1;

  return vt;
}

void vt_destroy(struct vt *vt) {
  struct row *row = vt->screen;
  while (row) {
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
    // redraw_all hits a different code path
    if (vt->graphics && !vt->redraw_all) {
      graphics_clear(vt->graphics, damage->x, damage->y, damage->w, damage->h);

      for (int y = damage->y; y < (damage->y + damage->h); ++y) {
        struct row *row = get_row(vt, y, NULL);
        for (int x = damage->x; x < (damage->x + damage->w); ++x) {
          char_at(vt->graphics, x, y, &row->cells[x]);
        }
      }
    }

    struct damage *tmp = damage;
    damage = damage->next;
    free(tmp);
  }

  vt->damage = NULL;

  if (!vt->graphics) {
    return;
  } else if (!vt->redraw_all) {
    return;
  }

  graphics_clear(vt->graphics, 0, 0, vt->cols, vt->rows);

  struct row *row = vt->screen;
  int y = 0;
  while (row) {
    for (int i = 0; i < vt->cols; i++) {
      char_at(vt->graphics, i, y, &row->cells[i]);
    }

    row->dirty = 0;
    row = row->next;
    ++y;
  }

  vt->redraw_all = 0;
}

static void process_char(struct vt *vt, char c) {
  fprintf(stderr, "process: '%c' / %d\n",
          c == '\033' ? '~' : (isprint(c) ? c : '-'), c);

  // VT100 ignores NUL and DEL
  if (c == 0 || c == '\177') {
    return;
  }

  // these all take action regardless of if we're in a control sequence
  int actioned = 1;
  switch (c) {
  case '\005':
    // ENQ: Enquiry
    write_retry(vt->pty, "\033[?1;2c", 7);
    break;
  case '\010':
    cursor_back(vt, 1);
    break;
  case '\n':
  case '\013':
  case '\014':
    cursor_down(vt, 1, 1);
    break;
  case '\r':
    cursor_sol(vt);
    break;
  case '\t':
    cursor_to(vt, next_tabstop(vt, vt->cx), vt->cy, 0);
    break;
  case '\016':
    vt->charset = 1;
    break;
  case '\017':
    vt->charset = 0;
    break;
  default:
    // fprintf(stderr, "nihterm: unknown character: %c/%d\n", c, c);
    actioned = 0;
  }

  if (actioned) {
    return;
  }

  if (vt->in_sequence) {
    if (c == '\030' || c == '\032') {
      // CAN, SUB - cancel sequence
      end_sequence(vt);
      return;
    }

    if (c == '\033') {
      // ESC during sequence reests the sequence
      vt->seqidx = 0;
      return;
    }

    vt->sequence[vt->seqidx++] = c;

    if (!vt->mode.decanm) {
      do_vt52(vt);
      return;
    }

    if (vt->seqidx == 1 && !isalpha(c) && !iscntrl(c) && !isdigit(c)) {
      return;
    }

    // CSI sequences have more parameters, ESC sequences are complete here.
    if (vt->sequence[0] == '[') {
      if (isdigit(c) || c == ';' || c == '?' || c == '#') {
        return;
      }
    }

    do_sequence(vt);
  } else if (c == '\033') {
    vt->in_sequence = 1;
  } else if (isprint(c)) {
    if (vt->mode.irm) {
      insert_char_at(vt, vt->cx, vt->cy, c);
      mark_damage(vt, vt->cx, vt->cy, vt->cols - vt->cx, 1);
    } else {
      set_char_at(vt, vt->cx, vt->cy, c);
      mark_damage(vt, vt->cx, vt->cy, 1, 1);
    }
    cursor_fwd(vt, 1);
    fprintf(stderr, "cursor: %d, %d\n", vt->cx, vt->cy);
  } else {
    switch (c) {
    case '\005':
      // ENQ: Enquiry
      write_retry(vt->pty, "\033[?1;2c", 7);
      break;
    case '\010':
      cursor_back(vt, 1);
      break;
    case '\n':
    case '\013':
    case '\014':
      cursor_down(vt, 1, 1);
      break;
    case '\r':
      cursor_sol(vt);
      break;
    case '\t':
      cursor_to(vt, next_tabstop(vt, vt->cx), vt->cy, 0);
      break;
    case '\016':
      vt->charset = 1;
      break;
    case '\017':
      vt->charset = 0;
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
  case '(':
  case ')':
    handle_paren_seq(vt);
    break;
  case '#':
    handle_pound_seq(vt);
    break;
  case 'E':
    // NEL: Next Line
    cursor_down(vt, 1, 1);
    cursor_sol(vt);
    break;
  case 'D':
    // Index
    cursor_down(vt, 1, 1);
    break;
  case 'M':
    // Reverse Index
    cursor_up(vt, 1, 1);
    break;
  case 'Z':
    // DECID - Identify Terminal
    // Graphics option + Advanced video option
    write_retry(vt->pty, "\033[?1;6c", 7);
    break;
  case 'H':
    // HTS - Horizontal Tabulation Set
    vt->tabstops[vt->cx] = 1;
    break;
  case '7':
    // DECSC - Save Cursor
    vt->saved_x = vt->cx;
    vt->saved_y = vt->cy;
    vt->saved_attr = vt->current_attr;
    vt->saved_charset = vt->charset;
    break;
  case '8':
    // DECRC - Restore Cursor
    vt->cx = vt->saved_x;
    vt->cy = vt->saved_y;
    vt->current_attr = vt->saved_attr;
    vt->charset = vt->saved_charset;
    break;
  default:
    fprintf(stderr, "nihterm: unhandled sequence: %s\n", vt->sequence);
  }

  end_sequence(vt);
}

static void cursor_fwd(struct vt *vt, int num) {
  cursor_to(vt, vt->cx + num, vt->cy, 0);
}

static void cursor_back(struct vt *vt, int num) {
  cursor_to(vt, vt->cx - num, vt->cy, 0);
}

static void cursor_sol(struct vt *vt) { cursor_to(vt, 0, vt->cy, 0); }

static void cursor_home(struct vt *vt) { cursor_to(vt, 0, 0, 0); }

static void cursor_down(struct vt *vt, int num, int scroll) {
  cursor_to(vt, vt->cx, vt->cy + num, scroll);
}

static void cursor_up(struct vt *vt, int num, int scroll) {
  cursor_to(vt, vt->cx, vt->cy - num, scroll);
}

static void cursor_to(struct vt *vt, int x, int y, int scroll) {
  fprintf(
      stderr,
      "cursor_to(%d, %d) current %d, %d (decom %d, decawm %d, margin %d..%d)\n",
      x, y, vt->cx, vt->cy, vt->mode.decom, vt->mode.decawm, vt->margin_top,
      vt->margin_bottom);

  // TODO(miselin): this isn't quite right yet...
  /*
  if (vt->mode.decom) {
    // position is relative to origin in DECOM mode
    x += vt->margin_left;
    y += vt->margin_top;
  }
  */

  vt->cx = x;
  vt->cy = y;

  if (vt->mode.decom) {
    if (vt->cx < vt->margin_left) {
      vt->cx = vt->margin_left;
    } else if (vt->cx >= vt->margin_right) {
      if (vt->mode.decawm) {
        int overflow = vt->cx - vt->margin_right;
        vt->cy++;
        vt->cx = overflow;
        scroll = 1;
      } else {
        vt->cx = vt->margin_right - 1;
      }
    }

    if (vt->cy < vt->margin_top) {
      if (scroll) {
        int rows = (vt->margin_top - vt->cy) + 1;
        for (int i = 0; i < rows; ++i) {
          scroll_down(vt);
        }
      }

      vt->cy = vt->margin_top;
    } else if (vt->cy >= vt->margin_bottom) {
      if (scroll) {
        int rows = (vt->cy - vt->margin_bottom) + 1;
        for (int i = 0; i < rows; ++i) {
          scroll_up(vt);
        }
      }

      vt->cy = vt->margin_bottom - 1;
    }
  } else {
    if (vt->cx < 0) {
      vt->cx = 0;
    } else if (vt->cx >= vt->cols) {
      if (vt->mode.decawm) {
        int overflow = vt->cx - vt->cols;
        vt->cy++;
        vt->cx = overflow;
        scroll = 1;
      } else {
        vt->cx = vt->cols - 1;
      }
    }

    if (vt->cy < 0) {
      if (scroll) {
        int rows = -vt->cy;
        for (int i = 0; i < rows; ++i) {
          scroll_down(vt);
        }
      }

      vt->cy = 0;
    } else if (vt->cy >= vt->rows) {
      if (scroll) {
        int rows = (vt->cy - vt->rows) + 1;
        for (int i = 0; i < rows; ++i) {
          scroll_up(vt);
        }
      }

      vt->cy = vt->rows - 1;
    }
  }

  fprintf(stderr, "cursor_to(%d, %d) => %d, %d\n", x, y, vt->cx, vt->cy);
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

  fprintf(stderr, "params: count=%d %d %d %d %d %d %d\n", num_params, params[0],
          params[1], params[2], params[3], params[4], params[5]);

  char last = vt->sequence[vt->seqidx - 1];
  switch (last) {
  case 'g':
    // TBC - Tabulation Clear
    if (num_params == 0 || params[0] == 0) {
      vt->tabstops[vt->cx] = 0;
    } else if (params[0] == 3) {
      memset(vt->tabstops, 0, (size_t)vt->cols);
    }
    break;
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
    fprintf(stderr, "cursor: %d, %d\n", vt->cx, vt->cy);
    handle_erases(vt, last == 'J' ? 0 : 1, num_params == 1 ? params[0] : 0);
    break;
  case 'l':
  case 'h':
    handle_modes(vt, last == 'l' ? 0 : 1);
    break;
  case 'c':
    // DA - Device Attributes
    // Graphics option + Advanced video option
    write_retry(vt->pty, "\033[?1;6c", 7);
    break;
  case 'n':
    handle_reports_seq(vt, params, num_params);
    break;
  case 'A':
    cursor_up(vt, params[0], 0);
    break;
  case 'B':
    cursor_down(vt, params[0], 0);
    break;
  case 'C':
    cursor_fwd(vt, params[0]);
    break;
  case 'D':
    cursor_back(vt, params[0]);
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
      cursor_to(vt, params[1] - 1, params[0] - 1, 0);
    }
    break;
  case 'm':
    vt->current_attr.bold = 0;
    vt->current_attr.blink = 0;
    vt->current_attr.reverse = 0;
    vt->current_attr.underline = 0;
    for (int i = 0; i < num_params; ++i) {
      if (params[i] == 1) {
        vt->current_attr.bold = 1;
      } else if (params[i] == 4) {
        vt->current_attr.underline = 1;
      } else if (params[i] == 5) {
        vt->current_attr.blink = 1;
      } else if (params[i] == 7) {
        vt->current_attr.reverse = 1;
      }
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
      if (dprintf(vt->pty, "\033[%d;%dR", vt->cy + 1, vt->cx + 1) <= 0) {
        print_error("failed to write cursor position: %s\n", strerror(errno));
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
      if (vt->mode.deccolm) {
        vt->cols = 132;
      } else {
        vt->cols = 80;
      }
      erase_screen(vt);
      cursor_home(vt);
      graphics_resize(vt->graphics, vt->cols, vt->rows);
      vt->margin_right = vt->cols;
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

      // when changing DECOM, the cursor is homed
      cursor_home(vt);
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
  fprintf(stderr, "erase %d %d\n", line, n);
  switch (n) {
  case 0:
    // cursor to end of line/screen
    if (line == 1) {
      fprintf(stderr, "erase cursor to end of line\n");
      erase_line_cursor(vt, 0);
    } else {
      fprintf(stderr, "erase cursor to end of screen\n");
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
  fprintf(stderr, "erase_line_cursor: before=%d cx=%d\n", before, vt->cx);
  int sx = before ? 0 : vt->cx;
  int ex = before ? vt->cx : vt->cols;
  fprintf(stderr, "erase_line_cursor: %d %d\n", sx, ex);
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

  row->cells[x].c = c;
  row->cells[x].attr = vt->current_attr;

  row->dirty = 1;
}

static void insert_char_at(struct vt *vt, int x, int y, char c) {
  if (x >= vt->cols || y >= vt->rows) {
    print_error("insert_char_at out of bounds (%d, %d)\n", x, y);
    return;
  }

  struct row *row = get_row(vt, y, NULL);
  if (!row) {
    print_error("insert_char_at failed to get row %d\n", y);
    return;
  }

  // move characters right. last character is lost.
  memmove(&row->cells[x + 1], &row->cells[x],
          sizeof(struct cell) * (size_t)(vt->cols - x - 1));

  row->cells[x].c = c;
  row->cells[x].attr = vt->current_attr;

  row->dirty = 1;
}

static void scroll_up(struct vt *vt) {
  fprintf(stderr, "scroll_up: %d %d [%d..%d]\n", vt->rows, vt->cy,
          vt->margin_top, vt->margin_bottom);

  struct row *prev = NULL;
  struct row *row = get_row(vt, vt->margin_top, &prev);

  if (prev) {
    prev->next = row->next;
  } else {
    vt->screen = row->next;
  }

  append_line(vt, NULL);

  free_row(row);

  vt->redraw_all = 1;

  mark_damage(vt, 0, vt->margin_top, vt->cols, vt->margin_bottom);
}

static void scroll_down(struct vt *vt) {
  fprintf(stderr, "scroll_down: %d %d [%d..%d]\n", vt->rows, vt->cy,
          vt->margin_top, vt->margin_bottom);

  // int rows = vt->margin_bottom - vt->margin_top;

  struct row *prev = NULL;
  struct row *row = get_row(vt, vt->margin_top, &prev);

  screen_insert_line(vt, prev);

  row = get_row(vt, vt->margin_bottom + 1, &prev);

  prev->next = row->next;
  free_row(row);

  vt->redraw_all = 1;

  mark_damage(vt, 0, vt->margin_top, vt->cols, vt->margin_bottom);
}

static void handle_pound_seq(struct vt *vt) {
  fprintf(stderr, "pound seq: %s\n", vt->sequence);
  switch (vt->sequence[1]) {
  case '8':
    // DECALN
    {
      struct row *row = vt->screen;
      while (row) {
        for (int x = 0; x < vt->cols; ++x) {
          row->cells[x].c = 'E';
          row->cells[x].attr = vt->current_attr;
        }

        row = row->next;
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

  return screen_insert_line(vt, row);
}

static struct row *screen_insert_line(struct vt *vt, struct row *prev) {
  struct row *new_row = calloc(1, sizeof(struct row));
  for (int x = 0; x < vt->cols; ++x) {
    new_row->cells[x].c = ' ';
    new_row->cells[x].attr = vt->current_attr;
  }

  if (prev) {
    new_row->next = prev->next;
    prev->next = new_row;
  } else {
    new_row->next = vt->screen;
    vt->screen = new_row;
  }

  return new_row;
}

static void delete_character(struct vt *vt) {
  struct row *row = get_row(vt, vt->cy, NULL);

  memmove(&row->cells[vt->cx], &row->cells[vt->cx + 1],
          sizeof(struct cell) * (size_t)(vt->cols - vt->cx - 1));

  // TODO(miselin): I think this actually is meant to be the rightmost attribute
  row->cells[vt->cols - 1].c = ' ';
  row->cells[vt->cols - 1].attr = vt->current_attr;

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

  free_row(row);

  append_line(vt, NULL);
}

static void insert_line(struct vt *vt) {
  struct row *prev = NULL;
  if (vt->cy > 0) {
    prev = get_row(vt, vt->cy, NULL);
  }

  screen_insert_line(vt, prev);

  // lazy
  vt->redraw_all = 1;
}

void vt_fill(struct vt *vt, char **buffer) {
  *buffer = calloc(1, (size_t)((vt->rows * (vt->cols + 1)) + 1));

  struct row *row = vt->screen;
  int y = 0;
  while (row && y < vt->rows) {
    int x;
    for (x = 0; x < vt->cols; ++x) {
      (*buffer)[(y * (vt->cols + 1)) + x] = row->cells[x].c;
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

void free_row(struct row *row) { free(row); }

static int next_tabstop(struct vt *vt, int x) {
  for (int i = x + 1; i < vt->cols; ++i) {
    if (vt->tabstops[i]) {
      return i;
    }
  }

  return vt->cols - 1;
}

static void do_vt52(struct vt *vt) {
  fprintf(stderr, "vt52: %s\n", vt->sequence);

  switch (vt->sequence[0]) {
  case 'A':
    cursor_up(vt, 1, 0);
    break;
  case 'B':
    cursor_down(vt, 1, 0);
    break;
  case 'C':
    cursor_fwd(vt, 1);
    break;
  case 'D':
    cursor_back(vt, 1);
    break;
  case 'F':
    // Select Special Graphics character set
    break;
  case 'G':
    // Select ASCII character set
    break;
  case 'H':
    cursor_home(vt);
    break;
  case 'I':
    cursor_up(vt, 1, 1);
    break;
  case 'J':
    erase_screen_cursor(vt, 0);
    break;
  case 'K':
    erase_line_cursor(vt, 0);
    break;
  case 'Y':
    // Direct cursor address - has two params
    if (vt->seqidx < 3) {
      return;
    }

    char l = vt->sequence[1];
    char c = vt->sequence[2];
    cursor_to(vt, c - 037 - 1, l - 037 - 1, 0);
    break;
  case 'Z':
    // Identify
    write_retry(vt->pty, "\033/Z", 3);
    break;
  case '=':
    // Enter alternate keypad mode
    break;
  case '>':
    // Exit alternate keypad mode
    break;
  case '<':
    // Enter ANSI mode
    vt->mode.decanm = 1;
    break;
  default:
    print_error("unknown vt52 sequence: %s\n", vt->sequence);
  }

  end_sequence(vt);
}

static void end_sequence(struct vt *vt) {
  vt->in_sequence = 0;
  vt->seqidx = 0;
  memset(vt->sequence, 0, sizeof(vt->sequence));
}

static void handle_paren_seq(struct vt *vt) {
  if (vt->seqidx < 2) {
    return;
  }

  switch (vt->sequence[1]) {
  case 'A':
    // UK
    break;
  case 'B':
    // ASCII
    break;
  case '0':
    // special graphics
    break;
  case '1':
    // alternate standard characters
    break;
  case '2':
    // alternate special graphics
    break;
  }
}
