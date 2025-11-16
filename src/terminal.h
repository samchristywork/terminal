#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdbool.h>

#define SCROLLBACK_LINES 1000

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
  char data[6];
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
  Term_Cell **lines;
  int *widths;
  int capacity;
  int count;
  int head;
} Term_Scrollback;

typedef struct {
  Term_Cursor cursor;
  Term_Cursor saved_cursor;
  Term_Line *lines;
  Term_Scrollback scrollback;
  int scroll_offset;
  int scroll_top;
  int scroll_bot;
  bool cursor_hidden;
} Term_Screen;

typedef struct {
  int width;
  int height;
  Term_Screen screen;
  Term_Screen alt_screen;
  bool using_alt_screen;
  char window_title[256];
  bool title_dirty;
  char partial_buf[64];
  int partial_len;
  bool bracketed_paste;
  int mouse_mode; // 0=off, 1=click(1000), 2=button+motion(1002), 3=any(1003)
  bool mouse_sgr; // SGR extended coordinates (1006)
  unsigned long osc_bg; // packed 0xRRGGBB set by OSC 11
  bool bg_dirty;
  unsigned long default_fg_rgb; // packed 0xRRGGBB, for OSC 10 query response
  int cursor_shape; // DECSCUSR: 0/1=blinking block, 2=steady block, 3=blinking underline, 4=steady underline, 5=blinking bar, 6=steady bar
  char response_buf[256];
  int response_len;
  char title_stack[8][256];
  int title_stack_depth;
} Terminal;

typedef enum {
  TOKEN_TEXT,
  TOKEN_NEWLINE,             // \n
  TOKEN_CARRIAGE_RETURN,     // \r
  TOKEN_CSI_CODE,            // ESC[...m ESC[..H
  TOKEN_ERASE_EOL,           // ESC[K ESC[0K
  TOKEN_ERASE_SOL,           // ESC[1K
  TOKEN_ERASE_LINE,          // ESC[2K
  TOKEN_ERASE_DOWN,          // ESC[J ESC[0J
  TOKEN_ERASE_UP,            // ESC[1J
  TOKEN_ERASE_ALL,           // ESC[2J
  TOKEN_ERASE_SCROLLBACK,    // ESC[3J
  TOKEN_ALT_SCREEN,          // ESC[?1049h
  TOKEN_MAIN_SCREEN,         // ESC[?1049l
  TOKEN_CURSOR_HIDE,         // ESC[?25l
  TOKEN_CURSOR_SHOW,         // ESC[?25h
  TOKEN_BRACKETED_PASTE_ON,  // ESC[?2004h
  TOKEN_BRACKETED_PASTE_OFF, // ESC[?2004l
  TOKEN_FULL_RESET,          // ESC c (RIS)
  TOKEN_TAB,                 // \t
  TOKEN_BACKSPACE,           // \b or 0x7f
  TOKEN_OSC,                 // ESC ] ... BEL/ST
  TOKEN_REVERSE_INDEX,       // ESC M
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

void init_terminal(Terminal *terminal, int width, int height);

void reset_terminal(Terminal *terminal);

void free_terminal(Terminal *terminal);

void write_terminal(Terminal *terminal, const char *text, int length);

void resize_terminal(Terminal *terminal, int new_width, int new_height);

#endif
