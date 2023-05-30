#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <nihterm/vt.h>

struct teststate {
  teststate() {
    pty_parent = posix_openpt(O_RDWR);
    grantpt(pty_parent);
    unlockpt(pty_parent);
    pty_child = open(ptsname(pty_parent), O_RDWR);
    vt = vt_create(pty_child, 25, 80);
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
