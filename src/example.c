#include <stdio.h>
#include <string.h>

#include "terminal.h"

void write_chunk(Terminal *t, char *buf) {
  write_string(t, buf);
  print_terminal(t);
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
