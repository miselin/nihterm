#ifndef _NIHTERM_VT_H
#define _NIHTERM_VT_H

#include <stdint.h>

#include <nihterm/gfx.h>

struct vt;

#ifdef __cplusplus
extern "C" {
#endif

struct vt *vt_create(int pty, int rows, int cols);
void vt_destroy(struct vt *vt);

void vt_set_graphics(struct vt *vt, struct graphics *graphics);

// Process a string of bytes for rendering.
int vt_process(struct vt *vt, const char *string, size_t length);

// Pass on input to the pty, potentially processing it if needed.
ssize_t vt_input(struct vt *vt, const char *string, size_t length);

void vt_render(struct vt *vt);

// Fill the given buffer with the current state of the screen.
// Useful for testing.
void vt_fill(struct vt *vt, char **buffer);

#ifdef __cplusplus
} // extern "C"
#endif

#endif  // _NIHTERM_VT_H
