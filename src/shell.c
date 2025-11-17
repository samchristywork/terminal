#include <fcntl.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "gui.h"
#include "log.h"
#include "terminal.h"

void init_shell(GuiContext *gui, int cols, int rows) {
  int master, slave;
  pid_t pid;

  LOG_INFO_MSG("Initializing shell subprocess");

  struct winsize ws = {
      .ws_row = rows,
      .ws_col = cols,
      .ws_xpixel = 0,
      .ws_ypixel = 0,
  };

  if (openpty(&master, &slave, NULL, NULL, &ws) == -1) {
    perror("openpty");
    LOG_ERROR_MSG("Failed to open PTY");
    return;
  }

  pid = fork();
  if (pid == -1) {
    perror("fork");
    LOG_ERROR_MSG("Failed to fork shell subprocess");
    close(master);
    close(slave);
    return;
  }

  if (pid == 0) {
    close(master);

    setsid();
    ioctl(slave, TIOCSCTTY, 0);

    dup2(slave, STDIN_FILENO);
    dup2(slave, STDOUT_FILENO);
    dup2(slave, STDERR_FILENO);

    if (slave > STDERR_FILENO)
      close(slave);

    setenv("TERM", "xterm-256color", 1);
    execl("/bin/sh", "sh", NULL);
    perror("execl");
    exit(1);
  } else {
    close(slave);

    int flags = fcntl(master, F_GETFL, 0);
    fcntl(master, F_SETFL, flags | O_NONBLOCK);

    gui->pipe_fd = master;
    gui->input_fd = -1;
    gui->child_pid = pid;
    LOG_INFO_MSG("Shell subprocess started with PID %d", pid);
  }
}

void read_shell_output(GuiContext *gui, Terminal *terminal) {
  char buffer[4096];
  ssize_t bytes_read;

  while ((bytes_read = read(gui->pipe_fd, buffer, sizeof(buffer))) > 0) {
    write_terminal(terminal, buffer, bytes_read);
  }
}
