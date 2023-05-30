#include <fcntl.h>
#include <stdio.h>
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

    fprintf(stderr, "parent: %d, child: %d, name: %s\n", pty_parent, pty_child,
            name);

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

static ssize_t cpr(struct teststate &state, char *buf, size_t buflen) {
  vt_process(state.vt, "\033[6n", 4);
  return read_timeout(state.pty_child, buf, buflen, 2);
}

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

  // should reset to 1,1
  vt_process(state.vt, "\033[5;5H", 6);

  // ask for a cursor position report
  ssize_t rc = cpr(state, buf, 64);
  if (rc < 0 && errno == ETIMEDOUT) {
    FAIL() << "Timed out waiting for response";
  }

  EXPECT_GT(rc, 0);
  EXPECT_STREQ(buf, "\033[5;5R");
}
