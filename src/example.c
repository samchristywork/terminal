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

int main() {
  Terminal t;
  init_terminal(&t, 30, 10);

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
