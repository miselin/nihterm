#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#include <nihterm/gfx.h>
#include <nihterm/vt.h>

struct damage {
  int x;
  int y;
  int w;
  int h;
  struct damage *next;
};

struct vt {
  int pty;

  int rows;
  int cols;

  int cx;
  int cy;

  char *screen;
  struct graphics *graphics;

  int in_sequence;
  char sequence[16];
  int seqidx;

  struct damage *damage;

  // VT100 status/attributes/modes
  int sgr;
};

static int offset(struct vt *vt, int x, int y) {
  return (y * vt->cols) + x;
}

static void cursor_fwd(struct vt *vt);
static void cursor_back(struct vt *vt);
static void cursor_home(struct vt *vt);
static void cursor_down(struct vt *vt);
static void cursor_up(struct vt *vt);

static void process_char(struct vt *vt, char c);
static void do_sequence(struct vt *vt);

static void mark_damage(struct vt *vt, int x, int y, int w, int h);

// sequence handling
static void handle_bracket_seq(struct vt *vt);

struct vt *vt_create(int pty, int rows, int cols) {
  struct vt *vt = (struct vt *) calloc(sizeof(struct vt), 1);
  vt->pty = pty;
  vt->rows = rows;
  vt->cols = cols;
  vt->screen = (char *) calloc(sizeof(char), (size_t) (rows * cols));
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
        char_at(vt->graphics, x, y, vt->screen[offset(vt, x, y)], 0, 0);
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
    vt->screen[offset(vt, vt->cx, vt->cy)] = c;
    mark_damage(vt, vt->cx, vt->cy, 1, 1);
    cursor_fwd(vt);
  } else {
    switch (c) {
      case '\010':
        cursor_back(vt);
        break;
      case '\n':
        cursor_down(vt);
        break;
      case '\r':
        cursor_home(vt);
        break;
      default:
        fprintf(stderr, "nihterm: unknown character: %c/%d\n", c, c);
    }
  }
}

static void do_sequence(struct vt *vt) {
  vt->sequence[vt->seqidx] = '\0';

  if (vt->sequence[0] == '[') {
    handle_bracket_seq(vt);
  } else {
    fprintf(stderr, "nihterm: sequence: %s\n", vt->sequence);
  }

  vt->seqidx = 0;
  vt->in_sequence = 0;
}

static void cursor_fwd(struct vt *vt) {
  vt->cx++;
  if (vt->cx >= vt->cols) {
    cursor_home(vt);
    cursor_down(vt);
  }
}

static void cursor_back(struct vt *vt) {
  if (vt->cx) {
    vt->cx++;
  } else {
    cursor_up(vt);
    vt->cx = vt->cols - 1;
  }
}

static void cursor_home(struct vt *vt) {
  vt->cx = 0;
}

static void cursor_down(struct vt *vt) {
  vt->cy++;
  if (vt->cy >= vt->rows) {
    // scroll
    // TODO: store a scrollback
    memcpy(vt->screen, vt->screen + vt->cols, (size_t) (vt->cols * (vt->rows - 1)));
    memset(vt->screen + (vt->cols * (vt->rows - 1)), 0, (size_t) vt->cols);
    vt->cy = vt->rows - 1;

    mark_damage(vt, 0, 0, vt->cols, vt->rows);
  }
}

static void cursor_up(struct vt *vt) {
  if (vt->cy) {
    vt->cy--;
  }
}

static void mark_damage(struct vt *vt, int x, int y, int w, int h) {
  // TODO(miselin): merge regions if they overlap appropriately
  struct damage *damage = (struct damage *) calloc(sizeof(struct damage), 1);
  damage->x = x;
  damage->y = y;
  damage->w = w;
  damage->h = h;
  damage->next = vt->damage;
  vt->damage = damage;
}

static void handle_bracket_seq(struct vt *vt) {
  char last = vt->sequence[vt->seqidx - 1];
  switch (last) {
    case 'c':
      // what are you
      write(vt->pty, "\033[?6c", 5);
      break;
    default:
      fprintf(stderr, "unhandled bracket sequence %c\n", last);
  }
}
