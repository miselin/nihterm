#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <utmp.h>
#include <time.h>

#include <nihterm/gfx.h>
#include <nihterm/vt.h>

// SIGCHLD handler
void sigchld(int sig) {
  (void) sig;

  // Reap the process
  pid_t pid = 0;
  while ((pid = waitpid(0, 0, WNOHANG)) > 0) {
    // reaped
    fprintf(stderr, "reaped %d\n", pid);
  }
  if (errno != ECHILD) {
    fprintf(stderr, "nihterm: waitpid failed: %s\n", strerror(errno));
  }
}

int main(int argc, char *argv[]) {
  (void) argc;
  (void) argv;

  setsid();

  int pty = posix_openpt(O_RDWR);
  if (pty < 0) {
    fprintf(stderr, "nihterm: could not get a pseudo-terminal: %s\n", strerror(errno));
    return 1;
  }

  const char *name = ptsname(pty);
  int rc = grantpt(pty);
  if (rc < 0) {
    fprintf(stderr, "nihterm: grantpt: %s\n", strerror(errno));
    return 1;
  }

  rc = unlockpt(pty);
  if (rc < 0) {
    fprintf(stderr, "nihterm: unlockpt: %s\n", strerror(errno));
    return 1;
  }

  printf("pty is %s\n", name);

  // spin up our SIGCHLD reaper now that we've configured the PTY
  signal(SIGCHLD, sigchld);

  pid_t child = fork();
  if (child == -1) {
    fprintf(stderr, "nihterm: fork failed: %s", strerror(errno));
    return EXIT_FAILURE;
  } else if (child == 0) {
    close(0);
    close(1);
    close(2);
    close(pty);

    setsid();

    int fd = open(name, O_RDWR);
    if (fd < 0) {
      fprintf(stderr, "nihterm: open pty '%s': %s\n", name, strerror(errno));
      exit(1);
    }

    dup2(fd, 1);
    dup2(fd, 2);

    setenv("TERM", "vt100", 1);
    setenv("LC_ALL", "en_US.UTF-8", 1);

    execl("/usr/bin/vttest", "/usr/bin/vttest", NULL);
    // execl("/bin/bash", "/bin/bash", NULL);

    fprintf(stderr, "nihterm: exit failed: %s\n", strerror(errno));
    exit(1);
  }

  struct graphics *graphics = create_graphics();
  if (!graphics) {
    fprintf(stderr, "nihterm: failed to initialize graphics\n");
    return 1;
  }

  struct vt *vt = vt_create(pty, 25, 80);
  if (!vt) {
    fprintf(stderr, "nihterm: failed to initialize vt\n");
    return 1;
  }

  vt_set_graphics(vt, graphics);

  struct winsize pty_size;
  memset(&pty_size, 0, sizeof(pty_size));
  pty_size.ws_col = 80;
  pty_size.ws_row = 25;
  pty_size.ws_xpixel = (uint16_t)window_width(graphics);
  pty_size.ws_ypixel = (uint16_t)window_height(graphics);
  ioctl(pty, TIOCSWINSZ, &pty_size);

  const size_t maxBuffSize = 32768;
  char buffer[maxBuffSize];
  while (1) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(pty, &readfds);

    // we'll block for up to 100 ms looking for PTY data
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    int ready = select(pty + 1, &readfds, NULL, NULL, &tv);
    if (ready > 0) {
      if (FD_ISSET(pty, &readfds)) {
        ssize_t len = read(pty, buffer, maxBuffSize);
        if (len < 0) {
          // not a real error
          if (errno == EINTR) {
            continue;
          }

          if (errno == EIO) {
            // child terminated, other side of the pty is closed
            break;
          }

          fprintf(stderr, "nihterm: read failed: %s\n", strerror(errno));
          exit(1);
        } else if (len == 0) {
          // EOF
          break;
        }

        buffer[len] = 0;

        vt_process(vt, buffer, (size_t) len);
      }
    }

    // regardless of what the PTY side did, we'll now handle SDL events
    if (process_queue(graphics)) {
      // TODO(miselin): do we need to send a SIGKILL if the child fails to terminate?
      fprintf(stderr, "nihterm: debug: quit requested. going down\n");
      kill(child, SIGTERM);
      waitpid(child, NULL, 0);
      break;
    }
  }

  vt_destroy(vt);
  destroy_graphics(graphics);

  return 0;
}
