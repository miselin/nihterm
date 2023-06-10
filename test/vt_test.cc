#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <nihterm/vt.h>

struct teststate {
  teststate() {
    pty_parent = posix_openpt(O_RDWR | O_NOCTTY);
    if (pty_parent < 0) {
      std::cerr << "posix_openpt: " << strerror(errno) << std::endl;
      abort();
    }

    int rc = grantpt(pty_parent);
    if (rc < 0) {
      std::cerr << "grantpt: " << strerror(errno) << std::endl;
      abort();
    }
    rc = unlockpt(pty_parent);
    if (rc < 0) {
      std::cerr << "unlockpt: " << strerror(errno) << std::endl;
      abort();
    }

    const char *name = ptsname(pty_parent);
    pty_child = open(name, O_RDWR);
    if (pty_child < 0) {
      std::cerr << "open pty child: " << strerror(errno) << std::endl;
      abort();
    }

    // set line discipline so we don't need newlines
    struct termios t;
    tcgetattr(pty_child, &t);
    cfmakeraw(&t);
    tcsetattr(pty_child, TCSANOW, &t);

    vt = vt_create(pty_parent, 25, 80);
  }

  ~teststate() {
    vt_destroy(vt);
    close(pty_child);
    close(pty_parent);
  }

  struct vt *vt;
  int pty_parent;
  int pty_child;
};

const char *read_testdata(const char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    return nullptr;
  }
  char *buffer = new char[0x10000];
  size_t length = fread(buffer, 1, 0x10000, fp);
  fclose(fp);
  buffer[length] = '\0';
  return buffer;
}

static ssize_t read_timeout(int fd, char *buf, size_t buflen, time_t seconds) {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(fd, &fds);

  struct timeval tv;
  tv.tv_sec = seconds;
  tv.tv_usec = 0;

  int rc = select(fd + 1, &fds, nullptr, nullptr, &tv);
  if (rc > 0) {
    return read(fd, buf, buflen);
  } else if (rc == 0) {
    errno = ETIMEDOUT;
    return -1;
  } else {
    return rc;
  }
}

static void vt_printf(struct teststate &state, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char buffer[1024];
  memset(buffer, 0, 1024);
  int len = vsprintf(buffer, fmt, ap);
  vt_process(state.vt, buffer, static_cast<size_t>(len));
  va_end(ap);
}

static ssize_t cpr(struct teststate &state, char *buf, size_t buflen) {
  vt_process(state.vt, "\033[6n", 4);
  return read_timeout(state.pty_child, buf, buflen, 2);
}

// cup but for 1-based coordinates
static void cup(struct teststate &state, int a, int b) {
  vt_printf(state, "\033[%d;%dH", a, b);
}

static void cub(struct teststate &state, int n) {
  vt_printf(state, "\033[%dD", n);
}

static void cuf(struct teststate &state, int n) {
  vt_printf(state, "\033[%dC", n);
}

static void cuu(struct teststate &state, int n) {
  vt_printf(state, "\033[%dA", n);
}

static void cud(struct teststate &state, int n) {
  vt_printf(state, "\033[%dB", n);
}

static void decaln(struct teststate &state) { vt_printf(state, "\033#8"); }

static void ed(struct teststate &state, int n) {
  vt_printf(state, "\033[%dJ", n);
}

static void el(struct teststate &state, int n) {
  vt_printf(state, "\033[%dK", n);
}

static void hvp(struct teststate &state, int a, int b) {
  vt_printf(state, "\033[%d;%df", a, b);
}

static void ind(struct teststate &state) { vt_printf(state, "\033D"); }

static void ri(struct teststate &state) { vt_printf(state, "\033M"); }

static void nel(struct teststate &state) { vt_printf(state, "\033E"); }

TEST(VTTest, BasicOutput) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/basic.dat");
  EXPECT_NE(testdata, nullptr);

  // write some content that will be rendered by the VT
  vt_process(state.vt, "Hello, world!\n", 14);

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, Overwrite) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/basic.dat");
  EXPECT_NE(testdata, nullptr);

  vt_process(state.vt, "EEEEEEEEEEEEE\r", 14);
  vt_process(state.vt, "Hello, world!\n", 14);

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, VT100_DECALN) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/alignment.dat");
  EXPECT_NE(testdata, nullptr);

  vt_process(state.vt, "\033#8", 3);
  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, VT102_DCH) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/basic.dat");
  EXPECT_NE(testdata, nullptr);

  const char *teststr = "EHello, typo world!\033[11D\033[5P\r\033[P\n";

  vt_process(state.vt, teststr, strlen(teststr));

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, VT102_DL) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/basic.dat");
  EXPECT_NE(testdata, nullptr);

  const char *teststr = "Second line!\r\nHello, world!\033[H\033[M";

  vt_process(state.vt, teststr, strlen(teststr));

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, VT102_IL) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/twolines.dat");
  EXPECT_NE(testdata, nullptr);

  const char *teststr = "Second line!\r\033[LHello, world!";

  vt_process(state.vt, teststr, strlen(teststr));

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, ScrollUp) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/alphabet.dat");
  EXPECT_NE(testdata, nullptr);

  for (char c = 'A'; c <= 'Z'; c++) {
    char buf[4] = {c, '\r', '\n', '\0'};
    vt_process(state.vt, buf, c == 'Z' ? 1 : 3);
  }

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, ScrollDown) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/line_inserted.dat");
  EXPECT_NE(testdata, nullptr);

  const char *teststr = "Hello, world!\033M";

  vt_process(state.vt, teststr, strlen(teststr));

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, ScrollDown_Alphabet) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/alphabet_alt.dat");
  EXPECT_NE(testdata, nullptr);

  for (char c = 'A'; c <= 'X'; c++) {
    char buf[4] = {c, '\r', '\n', '\0'};
    vt_process(state.vt, buf, c == 'X' ? 1 : 3);
  }

  const char *teststr = "\033[H\033M";
  vt_process(state.vt, teststr, strlen(teststr));

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, VT100_ED_All) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/empty.dat");
  EXPECT_NE(testdata, nullptr);

  const char *teststr = "\033#8\033[2J";
  vt_process(state.vt, teststr, strlen(teststr));

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, VT100_ED_End) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/alignment_half.dat");
  EXPECT_NE(testdata, nullptr);

  const char *teststr = "\033#8\033[13;1H\033[0J";
  vt_process(state.vt, teststr, strlen(teststr));

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, VT100_ED_Start) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/alignment_half_alt.dat");
  EXPECT_NE(testdata, nullptr);

  // move to end of 12th line, erase from there to beginning of screen
  // ED includes cursor position
  const char *teststr = "\033#8\033[12;80H\033[1J";
  vt_process(state.vt, teststr, strlen(teststr));

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, VT100_EL_All) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/empty.dat");
  EXPECT_NE(testdata, nullptr);

  for (int i = 0; i < 80; ++i) {
    vt_process(state.vt, "*", 1);
  }

  const char *teststr = "\033[1;1H\033[2K";
  vt_process(state.vt, teststr, strlen(teststr));

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, VT100_EL_End) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/line_right.dat");
  EXPECT_NE(testdata, nullptr);

  // write a full row of *'s
  for (int i = 0; i < 80; ++i) {
    vt_process(state.vt, "*", 1);
  }

  // move cursor to x=40, then delete to start of line (includes cursor)
  const char *teststr = "\033[1;40H\033[1K";
  vt_process(state.vt, teststr, strlen(teststr));

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, VT100_EL_Start) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/line_left.dat");
  EXPECT_NE(testdata, nullptr);

  for (int i = 0; i < 80; ++i) {
    vt_process(state.vt, "*", 1);
  }

  const char *teststr = "\033[1;40H\033[0K";
  vt_process(state.vt, teststr, strlen(teststr));

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, VT102_IRM) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/basic.dat");
  EXPECT_NE(testdata, nullptr);

  const char *teststr = "world!\033[H\033[4hHello, ";

  vt_process(state.vt, teststr, strlen(teststr));

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

TEST(VTTest, VT100_CPR) {
  struct teststate state;

  char buf[64] = {0};

  // ask for a cursor position report
  ssize_t rc = cpr(state, buf, 64);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }

  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[1;1R");
}

TEST(VTTest, VT100_CUP_home) {
  struct teststate state;

  char buf[64] = {0};

  // should reset to 1,1
  vt_process(state.vt, "\033[H", 3);

  // ask for a cursor position report
  ssize_t rc = cpr(state, buf, 64);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }

  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[1;1R");
}

TEST(VTTest, VT100_CUP_partial) {
  struct teststate state;

  char buf[64] = {0};

  // should reset to 1,1
  vt_process(state.vt, "\033[5H", 4);

  // ask for a cursor position report
  ssize_t rc = cpr(state, buf, 64);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }

  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[5;1R");
}

TEST(VTTest, VT100_CUP) {
  struct teststate state;

  char buf[64] = {0};

  vt_process(state.vt, "\033[5;5H", 6);

  // ask for a cursor position report
  ssize_t rc = cpr(state, buf, 64);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }

  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[5;5R");
}

TEST(VTTest, VT100_CUB) {
  struct teststate state;

  char buf[64] = {0};

  const char *teststr = "\033[5;5H\033[D";
  vt_process(state.vt, teststr, strlen(teststr));

  // ask for a cursor position report
  ssize_t rc = cpr(state, buf, 64);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }

  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[5;4R");
}

TEST(VTTest, VT100_CUD) {
  struct teststate state;

  char buf[64] = {0};

  const char *teststr = "\033[5;5H\033[B";
  vt_process(state.vt, teststr, strlen(teststr));

  // ask for a cursor position report
  ssize_t rc = cpr(state, buf, 64);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }

  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[6;5R");
}

TEST(VTTest, VT100_CUF) {
  struct teststate state;

  char buf[64] = {0};

  const char *teststr = "\033[5;5H\033[C";
  vt_process(state.vt, teststr, strlen(teststr));

  // ask for a cursor position report
  ssize_t rc = cpr(state, buf, 64);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }

  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[5;6R");
}

TEST(VTTest, VT100_CUU) {
  struct teststate state;

  char buf[64] = {0};

  const char *teststr = "\033[5;5H\033[A";
  vt_process(state.vt, teststr, strlen(teststr));

  // ask for a cursor position report
  ssize_t rc = cpr(state, buf, 64);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }

  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[4;5R");
}

TEST(VTTest, VT100_NEL) {
  struct teststate state;

  char buf[64] = {0};

  const char *teststr = "\033[5;5H\033E";
  vt_process(state.vt, teststr, strlen(teststr));

  // ask for a cursor position report
  ssize_t rc = cpr(state, buf, 64);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }

  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[6;1R");
}

TEST(VTTest, VT100_RI) {
  struct teststate state;

  char buf[64] = {0};

  const char *teststr = "\033[5;5H\033M";
  vt_process(state.vt, teststr, strlen(teststr));

  // ask for a cursor position report
  ssize_t rc = cpr(state, buf, 64);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }

  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[4;5R");
}

TEST(VTTest, VT100_IND) {
  struct teststate state;

  char buf[64] = {0};

  const char *teststr = "\033[5;5H\033D";
  vt_process(state.vt, teststr, strlen(teststr));

  // ask for a cursor position report
  ssize_t rc = cpr(state, buf, 64);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }

  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[6;5R");
}

TEST(VTTest, VT100_ENQ) {
  struct teststate state;

  char buf[64] = {0};

  const char *teststr = "\005";
  vt_process(state.vt, teststr, strlen(teststr));

  ssize_t rc = read_timeout(state.pty_child, buf, 64, 2);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }
  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[?1;2c");
}

TEST(VTTest, VT100_DA) {
  struct teststate state;

  char buf[64] = {0};

  const char *teststr = "\033[c";
  vt_process(state.vt, teststr, strlen(teststr));

  ssize_t rc = read_timeout(state.pty_child, buf, 64, 2);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }
  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[?1;6c");
}

TEST(VTTest, VT100_DECID) {
  struct teststate state;

  char buf[64] = {0};

  const char *teststr = "\033Z";
  vt_process(state.vt, teststr, strlen(teststr));

  ssize_t rc = read_timeout(state.pty_child, buf, 64, 2);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }
  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[?1;6c");
}

TEST(VTTest, AutoWrap) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/vttest_autowrap.dat");
  EXPECT_NE(testdata, nullptr);

  // Autowrap test from vttest

  static char on_left[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static char on_right[] = "abcdefghijklmnopqrstuvwxyz";
  int width = 80;
  int height = sizeof(on_left) - 1;
  int region = 24 - 6;

  // 80-column mode
  vt_printf(state, "\033[?3l");

  vt_printf(state,
            "Test of autowrap, mixing control and print characters.\r\n");
  vt_printf(state, "The left/right margins should have letters in order:\r\n");

  // DECSTBM
  vt_printf(state, "\033[%d;%dr", 3, region + 3);

  // DECOM, DECAWM
  vt_printf(state, "\033[?6h\033[?7h");

  for (int i = 0; i < height; ++i) {
    switch (i % 4) {
    case 0:
      // as-is
      cup(state, region + 1, 1);
      vt_printf(state, "%c", on_left[i]);

      cup(state, region + 1, width);
      vt_printf(state, "%c", on_right[i]);

      vt_printf(state, "\n");
      break;
    case 1:
      // simple wrap
      cup(state, region, width);
      vt_printf(state, "%c%c", on_right[i - 1], on_left[i]);

      // backspace at right margin
      cup(state, region + 1, width);
      vt_printf(state, "%c\b %c", on_left[i], on_right[i]);

      vt_printf(state, "\n");
      break;
    case 2:
      // tab to right margin
      cup(state, region + 1, width);
      vt_printf(state, "%c\b\b\t\t%c", on_left[i], on_right[i]);

      cup(state, region + 1, 2);
      vt_printf(state, "\b%c\n", on_left[i]);
      break;
    default:
      // newline at right margin
      cup(state, region + 1, width);
      vt_printf(state, "\n");

      cup(state, region, 1);
      vt_printf(state, "%c", on_left[i]);

      cup(state, region, width);
      vt_printf(state, "%c", on_right[i]);
      break;
    }
  }

  // Unset DECOM
  vt_printf(state, "\033[?6l");

  // Unset DECSTBM
  vt_printf(state, "\033[r");

  // holdit()
  cup(state, 22, 1);
  vt_printf(state, "Push <RETURN>");

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}

// vttest's cursor movements test
TEST(VTTest, CursorMovementsBox) {
  struct teststate state;

  const char *testdata = read_testdata("test/testdata/vttest_cursormoves.dat");
  EXPECT_NE(testdata, nullptr);

  // test assumes DECAWM
  vt_printf(state, "\033[?7h");

  /* Compute left/right columns for a 60-column box centered in 'width' */
  int width = 80;
  int max_lines = 24;
  int inner_l = (80 - 60) / 2;
  int inner_r = 61 + inner_l;
  int hlfxtra = (80 - 80) / 2;
  int row = 0;
  int col = 0;

  decaln(state);
  cup(state, 9, inner_l);
  ed(state, 1);
  cup(state, 18, 60 + hlfxtra);
  ed(state, 0);
  el(state, 1);
  cup(state, 9, inner_r);
  el(state, 0);
  for (row = 10; row <= 16; row++) {
    cup(state, row, inner_l);
    el(state, 1);
    cup(state, row, inner_r);
    el(state, 0);
  }
  cup(state, 17, 30);
  el(state, 2);
  for (col = 1; col <= width; col++) {
    hvp(state, max_lines, col);
    vt_printf(state, "*");
    hvp(state, 1, col);
    vt_printf(state, "*");
  }
  cup(state, 2, 2);
  for (row = 2; row <= max_lines - 1; row++) {
    vt_printf(state, "+");
    cub(state, 1);
    ind(state);
  }
  cup(state, max_lines - 1, width - 1);
  for (row = max_lines - 1; row >= 2; row--) {
    vt_printf(state, "+");
    cub(state, 1);
    ri(state);
  }
  cup(state, 2, 1);
  for (row = 2; row <= max_lines - 1; row++) {
    vt_printf(state, "*");
    cup(state, row, width);
    vt_printf(state, "*");
    cub(state, 10);
    if (row < 10)
      nel(state);
    else
      vt_printf(state,
                "\r\n"); // XXX: vttest runs in canonical mode, we don't
                         // have a line discipline. this would be an NL->CRNL
  }
  cup(state, 2, 10);
  cub(state, 42 + hlfxtra);
  cuf(state, 2);
  for (col = 3; col <= width - 2; col++) {
    vt_printf(state, "+");
    cuf(state, 0);
    cub(state, 2);
    cuf(state, 1);
  }
  cup(state, max_lines - 1, inner_r - 1);
  cuf(state, 42 + hlfxtra);
  cub(state, 2);
  for (col = width - 2; col >= 3; col--) {
    vt_printf(state, "+");
    cub(state, 1);
    cuf(state, 1);
    cub(state, 0);
    vt_printf(state, "%c", 8);
  }
  cup(state, 1, 1);
  cuu(state, 10);
  cuu(state, 1);
  cuu(state, 0);
  cup(state, max_lines, width);
  cud(state, 10);
  cud(state, 1);
  cud(state, 0);

  cup(state, 10, 2 + inner_l);
  for (row = 10; row <= 15; row++) {
    for (col = 2 + inner_l; col <= inner_r - 2; col++)
      vt_printf(state, " ");
    cud(state, 1);
    cub(state, 58);
  }
  cuu(state, 5);
  cuf(state, 1);
  vt_printf(state, "The screen should be cleared,  and have an unbroken bor-");
  cup(state, 12, inner_l + 3);
  vt_printf(state, "der of *'s and +'s around the edge,   and exactly in the");
  cup(state, 13, inner_l + 3);
  vt_printf(state, "middle  there should be a frame of E's around this  text");
  cup(state, 14, inner_l + 3);
  vt_printf(state, "with  one (1) free position around it.    ");

  // holdit()
  vt_printf(state, "Push <RETURN>");

  vt_render(state.vt);

  char *buffer = nullptr;
  vt_fill(state.vt, &buffer);

  EXPECT_NE(buffer, nullptr);
  EXPECT_STREQ(buffer, testdata);

  free(buffer);
  delete[] testdata;
}
