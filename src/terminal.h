#ifndef TERMINAL_H
#define TERMINAL_H

typedef struct {
  int red;
  int green;
  int blue;
} RGB;

typedef enum {
  COLOR_DEFAULT,
  COLOR_BRIGHT,
  COLOR_256,
  COLOR_RGB,
} ColorType;

typedef struct {
  ColorType type;
  union {
    int color;
    RGB rgb;
  };
} Color;

typedef struct {
  Color fg;
  Color bg;
  int bold;
  int underline;
  int reverse;
} Attr;

typedef struct {
  int data[6];
  int length;
  Attr attr;
} Cell;

typedef struct {
  Cell *cells;
} Line;

typedef struct {
  int x;
  int y;
  Attr attr;
} Cursor;

typedef struct {
  Cursor cursor;
  Line *lines;
} Screen;

typedef struct {
  int width;
  int height;
  Screen screen;
  Screen alt_screen;
  bool using_alt_screen;
} Terminal;

typedef enum {
  TOKEN_TEXT,
  TOKEN_NEWLINE,          // \n
  TOKEN_CARRIAGE_RETURN,  // \r
  TOKEN_CSI_CODE,         // ESC[...m ESC[..H
  TOKEN_ERASE_EOL,        // ESC[K ESC[0K
  TOKEN_ERASE_SOL,        // ESC[1K
  TOKEN_ERASE_LINE,       // ESC[2K
  TOKEN_ERASE_DOWN,       // ESC[J ESC[0J
  TOKEN_ERASE_UP,         // ESC[1J
  TOKEN_ERASE_ALL,        // ESC[2J
  TOKEN_ERASE_SCROLLBACK, // ESC[3J
  TOKEN_ALT_SCREEN,       // ESC[?1049h
  TOKEN_MAIN_SCREEN,      // ESC[?1049l
  TOKEN_TAB,              // \t
  TOKEN_UNKNOWN,
} TokenType;

typedef struct {
  TokenType type;
  // TODO: handle longer sequences
  char value[256];
  int length;
} Token;

typedef struct {
  Token *tokens;
  int count;
} Tokens;

void print_terminal(Terminal *terminal);

void init_terminal(Terminal *terminal, int width, int height);

void write_string(Terminal *terminal, const char *str);

#endif
