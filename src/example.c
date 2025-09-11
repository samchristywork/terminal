#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "terminal.h"

void write_chunk(Terminal *t, char *buf) {
  write_string(t, buf);
  print_terminal(t);
}

int setup_pipes(int stdin_pipe[2], int stdout_pipe[2]) {
  if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) {
    perror("pipe");
    return 1;
  }
  return 0;
}

int execute_child_process(int stdin_pipe[2], int stdout_pipe[2]) {
  close(stdin_pipe[1]);
  close(stdout_pipe[0]);

  dup2(stdin_pipe[0], STDIN_FILENO);
  dup2(stdout_pipe[1], STDOUT_FILENO);

  close(stdin_pipe[0]);
  close(stdout_pipe[1]);

  execl("/bin/bash", "/bin/bash", NULL);
  perror("execl");
  exit(1);
}

int handle_io(int stdin_pipe[2], int stdout_pipe[2], Terminal *t) {
  close(stdin_pipe[0]);
  close(stdout_pipe[1]);

  char buf[1024];
  ssize_t bytes_read;

  fd_set read_fds;
  int max_fd;

  signal(SIGINT, SIG_IGN);

  while (1) {
    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    FD_SET(stdout_pipe[0], &read_fds);

    max_fd = (STDIN_FILENO > stdout_pipe[0]) ? STDIN_FILENO : stdout_pipe[0];
    max_fd++;

    if (select(max_fd, &read_fds, NULL, NULL, NULL) == -1) {
      if (errno == EINTR) {
        break;
      } else {
        perror("select");
        break;
      }
    }

    if (FD_ISSET(STDIN_FILENO, &read_fds)) {
      bytes_read = read(STDIN_FILENO, buf, sizeof(buf) - 1);
      if (bytes_read > 0) {
        buf[bytes_read] = '\0';
        if (write(stdin_pipe[1], buf, bytes_read) == -1) {
          perror("write to bash stdin");
          break;
        }

        if (buf[bytes_read - 1] != '\n') {
          write(stdin_pipe[1], "\n", 1);
        }
      } else if (bytes_read == 0) {
        close(stdin_pipe[1]);
        break;
      } else {
        perror("read from stdin");
        break;
      }
    }

    if (FD_ISSET(stdout_pipe[0], &read_fds)) {
      bytes_read = read(stdout_pipe[0], buf, sizeof(buf) - 1);
      if (bytes_read > 0) {
        buf[bytes_read] = '\0';
        write_chunk(t, buf);
      } else if (bytes_read == 0) {
        break;
      } else {
        perror("read from bash stdout");
        break;
      }
    }
  }

  close(stdin_pipe[1]);
  close(stdout_pipe[0]);

  return 0;
}

int main() {
  Terminal t;
  init_terminal(&t, 80, 20);

  char buf[100];
  bzero(buf, sizeof(buf));
  while (1) {
    char *ret = fgets(buf, sizeof(buf) - 1, stdin);
    if (ret == NULL)
      break;

    write_chunk(&t, buf);
    bzero(buf, sizeof(buf));
  }
}
