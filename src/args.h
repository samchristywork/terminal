#ifndef ARGS_H
#define ARGS_H

typedef struct {
  int font_size;
} Args;

void parse_args(int argc, char *argv[], Args *args);

#endif
