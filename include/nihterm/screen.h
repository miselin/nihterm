#ifndef _NIHTERM_SCREEN_H
#define _NIHTERM_SCREEN_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

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
  char cp[5];
  int cp_len;
  struct cellattr attr;
};

struct row {
  struct cell cells[132];
  struct row *next;
  int dirty;

  int dbl_height;
  int dbl_side; // 0=top, 1=bottom
  int dbl_width;
};

#ifdef __cplusplus
} // extern "C"
#endif

#endif
