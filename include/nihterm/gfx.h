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
  char cp[5];
  int cp_len;
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

// Draw the given character at the given position. If dblwide is 1, the
// character will stretch over two cells. If dblheight is non-zero, the
// character will be double-width and double-height. Setting dblheight to 1 or
// 2 will control which half of the double-height character is rendered (1 for
// top, 2 for bottom).
void char_at(struct graphics *graphics, int x, int y, struct cell *cell,
             int dblwide, int dblheight);
void graphics_clear(struct graphics *graphics, int x, int y, int w, int h);

void graphics_resize(struct graphics *graphics, int cols, int rows);

// Invert the colors of the terminal.
void graphics_invert(struct graphics *graphics, int invert);

#endif  // _NIHTERM_GFX_H
