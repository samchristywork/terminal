#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum Color {
  NONE = -1,
  BLACK = 0,
  RED = 1,
  GREEN = 2,
  YELLOW = 3,
  BLUE = 4,
  MAGENTA = 5,
  CYAN = 6,
  WHITE = 7,
  NORMAL = 9
};

typedef struct Attr {
  int fg; // TODO: Support RGB colors
  int bg; // TODO: Support RGB colors
  int bold;
  int underline;
  int reverse;
} Attr;

typedef struct Cell {
  int data[6];
  int length;
  Attr attr;
} Cell;

typedef struct Line {
  Cell* cells;
} Line;

typedef struct Cursor {
  int x;
  int y;
  Attr attr;
} Cursor;

typedef struct Screen {
  Cursor cursor;
  Line* lines;
} Screen;

typedef struct Terminal {
  int width;
  int height;
  Screen screen;
  Screen alt_screen;
  bool using_alt_screen;
} Terminal;

typedef enum {
  TOKEN_TEXT,
  TOKEN_NEWLINE,
  TOKEN_CARRIAGE_RETURN,
  TOKEN_CURSOR_MOVE,
  TOKEN_ALT_SCREEN_ON,
  TOKEN_ALT_SCREEN_OFF,
  TOKEN_HOME,
  TOKEN_CLEAR_SCREEN,
} TokenType;

typedef struct {
  TokenType type;
  char value[256];
  int length;
} Token;

typedef struct {
  Token* tokens;
  int count;
} Tokens;

const char INVERT_COLORS[] = "\x1b[7m";
const char RESET_COLORS[] = "\x1b[0m";

void print_screen(Screen* screen, int width, int height) {
  for (int i = 0; i < width + 2; i++) {
    printf("-");
  }
  printf("\n");
  for (int i = 0; i < height; i++) {
    printf("|");
    for (int j = 0; j < width; j++) {
      Cursor c = screen->cursor;
      Cell cell = screen->lines[i].cells[j];
      if (i == c.y && j == c.x) {
        printf(INVERT_COLORS);
      }
      if (cell.length > 0) {
        for (int k = 0; k < cell.length; k++) {
          printf("%c", cell.data[k]);
        }
      } else {
        printf(" ");
      }
      if (i == c.y && j == c.x) {
        printf(RESET_COLORS);
      }
    }
    printf("|\n");
  }
  for (int i = 0; i < width + 2; i++) {
    printf("-");
  }
  printf("\n");
}

void print_terminal(Terminal* terminal) {
  if (terminal->using_alt_screen) {
    print_screen(&terminal->alt_screen, terminal->width, terminal->height);
  } else {
    print_screen(&terminal->screen, terminal->width, terminal->height);
  }
}

void init_screen(Screen* screen, int width, int height) {
  screen->cursor.x = 0;
  screen->cursor.y = 0;
  screen->lines = (Line*)malloc(height * sizeof(Line));
  for (int i = 0; i < height; i++) {
    screen->lines[i].cells = (Cell*)malloc(width * sizeof(Cell));
    for (int j = 0; j < width; j++) {
      bzero(&screen->lines[i].cells[j], sizeof(Cell));
    }
  }
}

void init_terminal(Terminal* terminal, int width, int height) {
  terminal->width = width;
  terminal->height = height;
  terminal->using_alt_screen = false;
  init_screen(&terminal->screen, width, height);
  init_screen(&terminal->alt_screen, width, height);
}

void scroll_screen(Screen* screen, int width, int height) {
  for (int j = 0; j < height - 1; j++) {
    for (int k = 0; k < width; k++) {
      screen->lines[j].cells[k] = screen->lines[j + 1].cells[k];
    }
  }
  for (int k = 0; k < width; k++) {
    bzero(&screen->lines[height - 1].cells[k], sizeof(Cell));
  }
}

void scroll_terminal(Terminal* terminal) {
  if (terminal->using_alt_screen) {
    scroll_screen(&terminal->alt_screen, terminal->width, terminal->height);
  } else {
    scroll_screen(&terminal->screen, terminal->width, terminal->height);
  }
}

void handle_newline(Screen *screen, int width, int height) {
  screen->cursor.x = 0;
  screen->cursor.y++;
  if (screen->cursor.y >= height) {
    screen->cursor.y = height - 1;
    scroll_screen(screen, width, height);
  }
}

Tokens* tokenize(const char* text, int length) {
  Tokens* tokens = (Tokens*)malloc(sizeof(Tokens));
  tokens->tokens = (Token*)malloc(128 * sizeof(Token));
  tokens->count = 0;

  int i = 0;
  while (i < length) {
    if (text[i] == '\n') {
      tokens->tokens[tokens->count].type = TOKEN_NEWLINE;
      tokens->tokens[tokens->count].length = 0;
      tokens->count++;
      i++;
    } else if (text[i] == '\r') {
      tokens->tokens[tokens->count].type = TOKEN_CARRIAGE_RETURN;
      tokens->tokens[tokens->count].length = 0;
      tokens->count++;
      i++;
    } else if (strncmp(&text[i], "\x1b[2J", 4) == 0) {
      tokens->tokens[tokens->count].type = TOKEN_CLEAR_SCREEN;
      tokens->count++;
      i += 4;
    } else if (strncmp(&text[i], "\x1b[?1049h", 8) == 0) {
      tokens->tokens[tokens->count].type = TOKEN_ALT_SCREEN_ON;
      tokens->count++;
      i += 8;
    } else if (strncmp(&text[i], "\x1b[?1049l", 8) == 0) {
      tokens->tokens[tokens->count].type = TOKEN_ALT_SCREEN_OFF;
      tokens->count++;
      i += 8;
    } else if (strncmp(&text[i], "\x1bH", 2) == 0) {
      tokens->tokens[tokens->count].type = TOKEN_HOME;
      tokens->count++;
      i += 2;
    } else if (text[i] == '\x1b' && i + 1 < length && text[i + 1] == '[') {
      int j = i + 2;
      char row[4] = {0}, col[4] = {0};
      int row_idx = 0, col_idx = 0;
      while (j < length && text[j] >= '0' && text[j] <= '9' && row_idx < 3) {
        row[row_idx++] = text[j++];
      }
      if (j < length && text[j] == ';') {
        j++;
        while (j < length && text[j] >= '0' && text[j] <= '9' && col_idx < 3) {
          col[col_idx++] = text[j++];
        }
        if (j < length && text[j] == 'H') {
          tokens->tokens[tokens->count].type = TOKEN_CURSOR_MOVE;
          tokens->tokens[tokens->count].length = 2;
          tokens->tokens[tokens->count].value[0] = atoi(row);
          tokens->tokens[tokens->count].value[1] = atoi(col);
          tokens->count++;
          i = j + 1;
        } else {
          // Invalid sequence, treat as text
          tokens->tokens[tokens->count].type = TOKEN_TEXT;
          tokens->tokens[tokens->count].value[0] = text[i];
          tokens->tokens[tokens->count].length = 1;
          tokens->count++;
          i++;
        }
      } else {
        // Invalid sequence, treat as text
        tokens->tokens[tokens->count].type = TOKEN_TEXT;
        tokens->tokens[tokens->count].value[0] = text[i];
        tokens->tokens[tokens->count].length = 1;
        tokens->count++;
        i++;
      }
    } else {
      // Regular text
      int start = i;
      while (i < length && text[i] != '\n' && !(text[i] == '\x1b' && i + 1 < length && text[i + 1] == '[')) {
        i++;
      }
      int text_length = i - start;
      tokens->tokens[tokens->count].type = TOKEN_TEXT;
      memcpy(tokens->tokens[tokens->count].value, &text[start], text_length);
      tokens->tokens[tokens->count].length = text_length;
      tokens->count++;
    }
  }
  return tokens;
}

void write_regular_char(Screen* screen, char c, int width, int height) {
  if (screen->cursor.x >= width) {
    handle_newline(screen, width, height);
  }

  if (screen->cursor.y < height) {
    Cell *cell = &screen->lines[screen->cursor.y].cells[screen->cursor.x];
    cell->data[0] = c;
    cell->length = 1;
    screen->cursor.x++;
  }
}

void write_terminal(Terminal* terminal, const char* text, int length) {
  Tokens *tokens = tokenize(text, length);
  int width = terminal->width;
  int height = terminal->height;

  for (int i = 0; i < tokens->count; i++) {
    Token token = tokens->tokens[i];
    if (token.type == TOKEN_TEXT) {
      for (int j = 0; j < token.length; j++) {
        if (terminal->using_alt_screen) {
          write_regular_char(&terminal->alt_screen, token.value[j], width, height);
        } else {
          write_regular_char(&terminal->screen, token.value[j], width, height);
        }
      }
    } else if (token.type == TOKEN_ALT_SCREEN_ON) {
      terminal->using_alt_screen = true;
    } else if (token.type == TOKEN_ALT_SCREEN_OFF) {
      terminal->using_alt_screen = false;
    } else if (token.type == TOKEN_NEWLINE) {
      if (terminal->using_alt_screen) {
        handle_newline(&terminal->alt_screen, width, height);
      } else {
        handle_newline(&terminal->screen, width, height);
      }
    } else if (token.type == TOKEN_CARRIAGE_RETURN) {
      if (terminal->using_alt_screen) {
        terminal->alt_screen.cursor.x = 0;
      } else {
        terminal->screen.cursor.x = 0;
      }
    } else if (token.type == TOKEN_HOME) {
      if (terminal->using_alt_screen) {
        terminal->alt_screen.cursor.x = 0;
        terminal->alt_screen.cursor.y = 0;
      } else {
        terminal->screen.cursor.x = 0;
        terminal->screen.cursor.y = 0;
      }
    } else if (token.type == TOKEN_CURSOR_MOVE) {
      int new_x = token.value[0] - 1;
      int new_y = token.value[1] - 1;
      if (new_x >= 0 && new_x < width) {
        if (terminal->using_alt_screen) {
          terminal->alt_screen.cursor.x = new_x;
        } else {
          terminal->screen.cursor.x = new_x;
        }
      }
      if (new_y >= 0 && new_y < height) {
        if (terminal->using_alt_screen) {
          terminal->alt_screen.cursor.y = new_y;
        } else {
          terminal->screen.cursor.y = new_y;
        }
      }
    } else if (token.type == TOKEN_CLEAR_SCREEN) {
      if (terminal->using_alt_screen) {
        for (int j = 0; j < height; j++) {
          for (int k = 0; k < width; k++) {
            bzero(&terminal->alt_screen.lines[j].cells[k], sizeof(Cell));
          }
        }
        terminal->alt_screen.cursor.x = 0;
        terminal->alt_screen.cursor.y = 0;
      } else {
        for (int j = 0; j < height; j++) {
          for (int k = 0; k < width; k++) {
            bzero(&terminal->screen.lines[j].cells[k], sizeof(Cell));
          }
        }
        terminal->screen.cursor.x = 0;
        terminal->screen.cursor.y = 0;
      }
    }
  }
}

void write_string(Terminal* terminal, const char* str) {
  write_terminal(terminal, str, strlen(str));
}

int main() {
  Terminal terminal;
  init_terminal(&terminal, 20, 10);

  // Normal text
  write_string(&terminal, "Hello, World!\n");
  print_terminal(&terminal);

  // Move cursor and write more text
  write_string(&terminal, "\x1b[10;5HThis is a test.\n");
  print_terminal(&terminal);

  // Move cursor again
  write_string(&terminal, "\x1b[1;2HFoo");
  print_terminal(&terminal);

  // Scroll
  write_string(&terminal, "\nLine 1\nLine 2\nLine 3\nLine 4\nLine 5\nLine 6\nLine 7\nLine 8\nLine 9\nLine 10\nLine 11\n");
  print_terminal(&terminal);

  // Alt screen
  write_string(&terminal, "\x1b[?1049hAlt Screen: Hello, World!");
  print_terminal(&terminal);

  // Back to normal screen
  write_string(&terminal, "\x1b[?1049l");
  print_terminal(&terminal);

  // Move cursor to home position
  write_string(&terminal, "\x1bHHome");
  print_terminal(&terminal);

  // Clear screen
  write_string(&terminal, "\x1b[2J\x1bH");
  print_terminal(&terminal);

  // Carriage return
  write_string(&terminal, "Hello,\rWorld!");
  print_terminal(&terminal);
}
