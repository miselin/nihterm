// Test program for DECAWM (Autowrap Mode).
// Mostly extracted from vttest and integrated into nihterm VT library.

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <nihterm/vt.h>

static void vt_printf(struct vt *vt, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void vt_printf(struct vt *vt, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char buffer[1024];
  memset(buffer, 0, 1024);
  int len = vsprintf(buffer, fmt, ap);
  vt_process(vt, buffer, (size_t)len);
  va_end(ap);
}

// cup but for 1-based coordinates
static void cup1(struct vt *vt, int a, int b) {
  vt_printf(vt, "\033[%d;%dH", a, b);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  int pty_parent = posix_openpt(O_RDWR | O_NOCTTY);
  if (pty_parent < 0) {
    fprintf(stderr, "posix_openpt: %s\n", strerror(errno));
    abort();
  }

  int rc = grantpt(pty_parent);
  if (rc < 0) {
    fprintf(stderr, "grantpt: %s\n", strerror(errno));
    abort();
  }
  rc = unlockpt(pty_parent);
  if (rc < 0) {
    fprintf(stderr, "unlockpt: %s\n", strerror(errno));
    abort();
  }

  const char *name = ptsname(pty_parent);
  int pty_child = open(name, O_RDWR);
  if (pty_child < 0) {
    fprintf(stderr, "open pty child: %s\n", strerror(errno));
    abort();
  }

  // set line discipline so we don't need newlines
  struct termios t;
  tcgetattr(pty_child, &t);
  cfmakeraw(&t);
  tcsetattr(pty_child, TCSANOW, &t);

  struct vt *vt = vt_create(pty_parent, 25, 80);

  // Autowrap test from vttest

  static char on_left[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static char on_right[] = "abcdefghijklmnopqrstuvwxyz";
  int width = 80;
  int height = sizeof(on_left) - 1;
  int region = 24 - 6;

  // 80-column mode
  vt_printf(vt, "\033[?3l");

  vt_printf(vt, "Test of autowrap, mixing control and print characters.\r\n");
  vt_printf(vt, "The left/right margins should have letters in order:\r\n");

  // DECSTBM
  vt_printf(vt, "\033[%d;%dr", 3, region + 3);

  // DECOM, DECAWM
  vt_printf(vt, "\033[?6h\033[?7h");

  for (int i = 0; i < height; ++i) {
    switch (i % 4) {
    case 0:
      // as-is
      cup1(vt, region + 1, 1);
      vt_printf(vt, "%c", on_left[i]);

      cup1(vt, region + 1, width);
      vt_printf(vt, "%c", on_right[i]);

      vt_printf(vt, "\n");
      break;
    case 1:
      // simple wrap
      cup1(vt, region, width);
      vt_printf(vt, "%c%c", on_right[i - 1], on_left[i]);

      // backspace at right margin
      cup1(vt, region + 1, width);
      vt_printf(vt, "%c\b %c", on_left[i], on_right[i]);

      vt_printf(vt, "\n");
      break;
    case 2:
      // tab to right margin
      cup1(vt, region + 1, width);
      vt_printf(vt, "%c\b\b\t\t%c", on_left[i], on_right[i]);

      cup1(vt, region + 1, 2);
      vt_printf(vt, "\b%c\n", on_left[i]);
      break;
    default:
      // newline at right margin
      cup1(vt, region + 1, width);
      vt_printf(vt, "\n");

      cup1(vt, region, 1);
      vt_printf(vt, "%c", on_left[i]);

      cup1(vt, region, width);
      vt_printf(vt, "%c", on_right[i]);
      break;
    }

    if (i >= 4) {
      break;
    }
  }

  // Unset DECOM
  vt_printf(vt, "\033[?6l");

  // Unset DECSTBM
  vt_printf(vt, "\033[r");

  // holdit()
  cup1(vt, 22, 1);
  vt_printf(vt, "Push <RETURN>");

  vt_render(vt);

  char *buffer = NULL;
  vt_fill(vt, &buffer);

  printf("%s", buffer);

  free(buffer);

  vt_destroy(vt);

  close(pty_child);
  close(pty_parent);
}
