#ifndef TERMINAL_H
#define TERMINAL_H

typedef struct {
  int red;
  int green;
  int blue;
} Term_RGB;

typedef enum {
  COLOR_DEFAULT,
  COLOR_BRIGHT,
  COLOR_256,
  COLOR_RGB,
} Term_ColorType;

typedef struct {
  Term_ColorType type;
  union {
    int color;
    Term_RGB rgb;
  };
} Term_Color;

typedef struct {
  Term_Color fg;
  Term_Color bg;
  int bold;
  int underline;
  int reverse;
} Term_Attr;

typedef struct {
  int data[6];
  int length;
  Term_Attr attr;
} Term_Cell;

typedef struct {
  Term_Cell *cells;
} Term_Line;

typedef struct {
  int x;
  int y;
  Term_Attr attr;
} Term_Cursor;

typedef struct {
  Term_Cursor cursor;
  Term_Line *lines;
} Term_Screen;

typedef struct {
  int width;
  int height;
  Term_Screen screen;
  Term_Screen alt_screen;
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
  TOKEN_BACKSPACE,        // \b or 0x7f
  TOKEN_UNKNOWN,
} Term_TokenType;

typedef struct {
  Term_TokenType type;
  // TODO: handle longer sequences
  char value[256];
  int length;
} Term_Token;

typedef struct {
  Term_Token *tokens;
  int count;
} Term_Tokens;

void print_terminal(Terminal *terminal);

void init_terminal(Terminal *terminal, int width, int height);

void write_string(Terminal *terminal, const char *str);

void resize_terminal(Terminal *terminal, int new_width, int new_height);

#endif
