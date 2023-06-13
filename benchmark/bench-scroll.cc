#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <iostream>

#include <benchmark/benchmark.h>

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

static void vt_printf(struct teststate &state, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void vt_printf(struct teststate &state, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char buffer[1024];
  memset(buffer, 0, 1024);
  int len = vsprintf(buffer, fmt, ap);
  vt_process(state.vt, buffer, static_cast<size_t>(len));
  va_end(ap);
}

static void BM_VTScrolling(benchmark::State& state) {
  struct teststate vtstate;
  for (auto _ : state) {
    vt_printf(vtstate, "abcdefghijklmnopqrstuvwxyz\n");
  }
}

BENCHMARK(BM_VTScrolling);

BENCHMARK_MAIN();
