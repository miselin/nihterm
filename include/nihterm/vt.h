#ifndef _NIHTERM_VT_H
#define _NIHTERM_VT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct vt;

#ifdef __cplusplus
extern "C" {
#endif

struct damage;
struct row;

struct vt_callbacks {
  // Pushed up when the terminal wishes to resize, e.g. for DECCOLM to swap to
  // 132-column mode.
  void (*on_resize)(void *user, int rows, int cols);

  // Pushed up when the terminal should be rendered in inverted colors.
  void (*invert)(void *user, int set);

  // Pushed up to report damage requiring a re-render
  void (*damage)(void *user, struct damage *chain);
};

struct vt *vt_create(int pty, int rows, int cols);
void vt_destroy(struct vt *vt);

void vt_set_callbacks(struct vt *vt, struct vt_callbacks *callbacks,
                      void *user);

// Process a string of bytes for rendering.
int vt_process(struct vt *vt, const char *string, size_t length);

// Pass on input to the pty, potentially processing it if needed.
ssize_t vt_input(struct vt *vt, const char *string, size_t length);

void vt_render(struct vt *vt);

// Fill the given buffer with the current state of the screen.
// Useful for testing.
void vt_fill(struct vt *vt, char **buffer);

struct row *vt_get_row(struct vt *vt, int row, struct row **prev);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _NIHTERM_VT_H
