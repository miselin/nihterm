#ifndef _NIHTERM_GFX_H
#define _NIHTERM_GFX_H 1

#include <stdint.h>

// private contents, part of public API
struct graphics;

// forward-declare VT (circular header dependency)
struct vt;

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

struct graphics *create_graphics();
void destroy_graphics(struct graphics *graphics);

size_t cell_width(struct graphics *graphics);
size_t cell_height(struct graphics *graphics);

size_t window_width(struct graphics *graphics);
size_t window_height(struct graphics *graphics);

int process_queue(struct graphics *graphics);

void link_vt(struct graphics *graphics, struct vt *vt);

void char_at(struct graphics *graphics, int x, int y, struct cell *cell);
void graphics_clear(struct graphics *graphics, int x, int y, int w, int h);

void graphics_resize(struct graphics *graphics, int cols, int rows);

#endif  // _NIHTERM_GFX_H
