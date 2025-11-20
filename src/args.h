#ifndef ARGS_H
#define ARGS_H

typedef struct {
  int font_size;
  int scrollback;
  char *log_file;
  char *font;       // NULL = use bundled FreeMono
  long fg;          // -1 = default white
  long bg;          // -1 = default black
  long palette[16]; // -1 = use built-in default for that slot
  int margin;
} Args;

void parse_args(int argc, char *argv[], Args *args);

#endif
