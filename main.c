#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int red;
  int green;
  int blue;
} RGB;

typedef struct {
  int is_rgb;
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
  Cell* cells;
} Line;

typedef struct {
  int x;
  int y;
  Attr attr;
} Cursor;

typedef struct {
  Cursor cursor;
  Line* lines;
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
  TOKEN_NEWLINE,
  TOKEN_CARRIAGE_RETURN,
  TOKEN_GRAPHICS,
} TokenType;

void print_token_type(TokenType type) {
  switch (type) {
    case TOKEN_TEXT:
      printf("%-17s", "TEXT");
      break;
    case TOKEN_NEWLINE:
      printf("%-17s", "NEWLINE");
      break;
    case TOKEN_CARRIAGE_RETURN:
      printf("%-17s", "CARRIAGE_RETURN");
      break;
    case TOKEN_GRAPHICS:
      printf("%-17s", "GRAPHICS");
      break;
  }
}

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

void print_cursor_data(Cursor cursor) {
  printf("Cursor: x=%d, y=%d, attr={fg=%d, bg=%d, bold=%d, underline=%d, reverse=%d}\n",
         cursor.x, cursor.y,
         cursor.attr.fg.color, cursor.attr.bg.color,
         cursor.attr.bold, cursor.attr.underline, cursor.attr.reverse);
}

void print_cell_color(Attr attr) {
  printf("\x1b[0m");

  int style = 0;
  if (attr.bold) style = 1;
  if (attr.underline) style = 4;
  if (attr.reverse) style = 7;

  int fg = attr.fg.color;
  int bg = attr.bg.color;

  if (fg == 0) fg = 39;
  if (bg == 0) bg = 49;

  printf("\x1b[%d;%d;%dm", style, fg, bg);
}

void print_screen(Screen* screen, int width, int height) {
#ifdef DEBUG
  print_cursor_data(screen->cursor);
#endif
  for (int i = 0; i < width + 2; i++) {
    printf("-");
  }
  printf("\n");
  for (int i = 0; i < height; i++) {
    printf("|");
    for (int j = 0; j < width; j++) {
      Cursor c = screen->cursor;
      Cell cell = screen->lines[i].cells[j];
      print_cell_color(cell.attr);
      if (i == c.y && j == c.x) {
        printf(INVERT_COLORS); // TODO: This is incorrect if cell is already reverse
      }
      if (cell.length > 0) {
        printf("%c", cell.data[0]);
        //printf("%x ", cell.data[0]);
        //printf("x");
      } else {
        printf(" ");
      }
      printf(RESET_COLORS);
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
  bzero(&screen->cursor, sizeof(Cursor));
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

bool matches(const char* text, int length, int* index, const char* seq) {
  int seq_len = strlen(seq);
  if (*index + seq_len > length) {
    return false;
  }
  if (strncmp(&text[*index], seq, seq_len) == 0) {
    *index += seq_len - 1;
    return true;
  }
  return false;
}

void add_token(Tokens* tokens, TokenType type, const char* value, int length) {
  tokens->tokens[tokens->count].type = type;
  if (value != NULL && length > 0) {
    memcpy(tokens->tokens[tokens->count].value, value, length);
  }
  tokens->tokens[tokens->count].length = length;
  tokens->count++;
}

bool is_color_code(const char* text, int length, int* index) {
  if (text[*index] == '\x1b' && *index + 1 < length && text[*index + 1] == '[') {
    *index += 2;
    return true;
  }
  return false;
}

Tokens *tokenize(const char* text, int length) {
  Tokens* tokens = (Tokens*)malloc(sizeof(Tokens));
  tokens->tokens = (Token*)malloc(128 * sizeof(Token));
  tokens->count = 0;

  for (int i = 0; i < length; i++) {
    if (matches(text, length, &i, "\n")) {
      add_token(tokens, TOKEN_NEWLINE, NULL, 0);
    }else if (matches(text, length, &i, "\r")) {
      add_token(tokens, TOKEN_CARRIAGE_RETURN, NULL, 0);
    }else if (is_color_code(text, length, &i)) {
      int start = i;
      while (i < length && text[i] != 'm') {
        i++;
      }
      if (i < length) {
        i++;
      }
      add_token(tokens, TOKEN_GRAPHICS, &text[start], i - start);
      i--;
    } else {
      int start = i;
      while (i < length && text[i] != '\n' && text[i] != '\r' && text[i] != '\x1b') {
        i++;
      }
      add_token(tokens, TOKEN_TEXT, &text[start], i - start);
      i--;
    }
  }

  return tokens;
}

void write_regular_char(Screen* screen, char c, int width, int height, Attr attr) {
  if (screen->cursor.x >= width) {
    handle_newline(screen, width, height);
  }

  if (screen->cursor.y < height) {
    Cell *cell = &screen->lines[screen->cursor.y].cells[screen->cursor.x];
    cell->data[0] = c;
    cell->length = 1;
    cell->attr = attr;
    screen->cursor.x++;
  }
}

void handle_field(Cursor** cursor, int value) {
  if (value == 0) {
    (*cursor)->attr.fg.color = 0;
    (*cursor)->attr.bg.color = 0;
    (*cursor)->attr.bold = 0;
    (*cursor)->attr.underline = 0;
    (*cursor)->attr.reverse = 0;
  } else if (value == 1) {
    (*cursor)->attr.bold = 1;
  } else if (value == 4) {
    (*cursor)->attr.underline = 1;
  } else if (value == 7) {
    (*cursor)->attr.reverse = 1;
  } else if (value >= 30 && value <= 37) {
    (*cursor)->attr.fg.is_rgb = 0;
    (*cursor)->attr.fg.color = value;
  } else if (value >= 40 && value <= 47) {
    (*cursor)->attr.bg.is_rgb = 0;
    (*cursor)->attr.bg.color = value;
  }
}

void modify_cursor(Cursor** cursor, Token token) {
  int num_semicolons = 0;
  for (int i = 0; i < token.length; i++) {
    if (token.value[i] == ';') {
      num_semicolons++;
    }
  }

  for (int i = 0; i <= num_semicolons; i++) {
    char* token_copy = (char*)malloc((token.length + 1) * sizeof(char));
    memcpy(token_copy, token.value, token.length);
    token_copy[token.length] = '\0';

    char* part = strtok(token_copy, ";");
    for (int j = 0; j < i; j++) {
      part = strtok(NULL, ";");
    }

    if (part != NULL) {
      int num = atoi(part);
      handle_field(cursor, num);
    }

    free(token_copy);
  }
}

void write_terminal(Terminal* terminal, const char* text, int length) {
  Tokens *tokens = tokenize(text, length);
  int width = terminal->width;
  int height = terminal->height;

#ifdef DEBUG
  for (int i = 0; i < tokens->count; i++) {
    Token token = tokens->tokens[i];
    print_token_type(token.type);
    printf("len=%d, val=", token.length);
    for (int j = 0; j < token.length; j++) {
      printf("%x ", token.value[j]);
    }
    printf("\n");
  }
#endif

  for (int i = 0; i < tokens->count; i++) {
    Token token = tokens->tokens[i];
    if (token.type == TOKEN_TEXT) {
      for (int j = 0; j < token.length; j++) {
        if (terminal->using_alt_screen) {
          write_regular_char(&terminal->alt_screen, token.value[j], width, height, terminal->alt_screen.cursor.attr);
        } else {
          write_regular_char(&terminal->screen, token.value[j], width, height, terminal->screen.cursor.attr);
        }
      }
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
    } else if (token.type == TOKEN_GRAPHICS) {
      Cursor* cursor = terminal->using_alt_screen ? &terminal->alt_screen.cursor : &terminal->screen.cursor;
      modify_cursor(&cursor, token);
    }
  }
}

void write_string(Terminal* terminal, const char* str) {
  write_terminal(terminal, str, strlen(str));
}

void test(Terminal* terminal, const char* test_name, const char* input) {
  printf("=== %s ===\n", test_name);
  write_string(terminal, input);
  print_terminal(terminal);
  printf("\n");
}

int main() {
  Terminal t;
  init_terminal(&t, 20, 10);

  test(&t, "Normal text", "Hello, World!\n");
  test(&t, "Carriage return", "Hello,\rWorld!\n");
  test(&t, "Red text", "\x1b[31mThis is red text\n");
}
