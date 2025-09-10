#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "terminal.h"

const char INVERT_COLORS[] = "\x1b[7m";
const char RESET_COLORS[] = "\x1b[0m";

void print_cursor_data(Cursor cursor) {
  printf("Cursor: x=%d, y=%d, attr={fg=%d, bg=%d, bold=%d, underline=%d, "
         "reverse=%d}\n",
         cursor.x, cursor.y, cursor.attr.fg.color, cursor.attr.bg.color,
         cursor.attr.bold, cursor.attr.underline, cursor.attr.reverse);
}

void print_cell_color(Attr attr) {
  if ((attr.fg.type == COLOR_DEFAULT || attr.fg.type == COLOR_BRIGHT) &&
      attr.fg.color != 0) {
    printf("\x1b[%dm", attr.fg.color);
  }
  if ((attr.bg.type == COLOR_DEFAULT || attr.bg.type == COLOR_BRIGHT) &&
      attr.bg.color != 0) {
    printf("\x1b[%dm", attr.bg.color);
  }
  if (attr.fg.type == COLOR_256) {
    printf("\x1b[38;5;%dm", attr.fg.color);
  }
  if (attr.bg.type == COLOR_256) {
    printf("\x1b[48;5;%dm", attr.bg.color);
  }
  if (attr.bold) {
    printf("\x1b[1m");
  }
  if (attr.underline) {
    printf("\x1b[4m");
  }
  if (attr.reverse) {
    printf("\x1b[7m");
  }
}

void print_screen(Screen *screen, int width, int height) {
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
        // TODO: This is incorrect if cell is already reverse
        printf(INVERT_COLORS);
      }
      if (cell.length > 0) {
        // TODO: handle multi-byte characters
        printf("%c", cell.data[0]);
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

void print_terminal(Terminal *terminal) {
  if (terminal->using_alt_screen) {
    print_screen(&terminal->alt_screen, terminal->width, terminal->height);
  } else {
    print_screen(&terminal->screen, terminal->width, terminal->height);
  }
}

void init_screen(Screen *screen, int width, int height) {
  bzero(&screen->cursor, sizeof(Cursor));
  screen->lines = (Line *)malloc(height * sizeof(Line));
  for (int i = 0; i < height; i++) {
    screen->lines[i].cells = (Cell *)malloc(width * sizeof(Cell));
    for (int j = 0; j < width; j++) {
      bzero(&screen->lines[i].cells[j], sizeof(Cell));
    }
  }
}

void init_terminal(Terminal *terminal, int width, int height) {
  terminal->width = width;
  terminal->height = height;
  terminal->using_alt_screen = false;
  init_screen(&terminal->screen, width, height);
  init_screen(&terminal->alt_screen, width, height);
}

void scroll_screen(Screen *screen, int width, int height) {
  for (int j = 0; j < height - 1; j++) {
    for (int k = 0; k < width; k++) {
      screen->lines[j].cells[k] = screen->lines[j + 1].cells[k];
    }
  }
  for (int k = 0; k < width; k++) {
    bzero(&screen->lines[height - 1].cells[k], sizeof(Cell));
  }
}

void scroll_terminal(Terminal *terminal) {
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

void add_token(Tokens *tokens, TokenType type, const char *value,
               int start_index, int length) {
  if (tokens->count % 128 == 0) {
    tokens->tokens = (Token *)realloc(
        tokens->tokens, (tokens->count + 128) * sizeof(Token));
  }
  Token *token = &tokens->tokens[tokens->count++];
  token->type = type;
  token->length = length;
  memcpy(token->value, &value[start_index], length);
  token->value[length] = '\0';
}

bool is_csi_code(const char *text, int length, int index, int *code_length) {
  if (index + 2 < length && text[index] == '\x1b' && text[index + 1] == '[') {
    int i = index + 2;
    while (i < length && (isdigit(text[i]) || text[i] == ';')) {
      i++;
    }
    if (i < length && (text[i] == 'm' || text[i] == 'H' || text[i] == 'f')) {
      *code_length = i - index + 1;
      return true;
    }
  }
  return false;
}

bool matches(const char *text, int length, int index, const char *pattern,
             int *pattern_length) {
  int pat_len = strlen(pattern);
  if (index + pat_len <= length && strncmp(&text[index], pattern, pat_len) == 0) {
    *pattern_length = pat_len;
    return true;
  }
  return false;
}

Tokens *tokenize(const char *text, int length) {
  Tokens *tokens = (Tokens *)malloc(sizeof(Tokens));
  tokens->tokens = (Token *)malloc(128 * sizeof(Token));
  tokens->count = 0;

  for (int i = 0; i < length; i++) {
    int len = 0;
    if (matches(text, length, i, "\n", &len)) {
      add_token(tokens, TOKEN_NEWLINE, text, i, len);
      i += len - 1;
    } else if (matches(text, length, i, "\r", &len)) {
      add_token(tokens, TOKEN_CARRIAGE_RETURN, text, i, len);
      i += len - 1;
    } else if (matches(text, length, i, "\x1b[J", &len)) {
      add_token(tokens, TOKEN_ERASE_DOWN, text, i, len);
      i += len - 1;
    } else if (matches(text, length, i, "\x1b[0J", &len)) {
      add_token(tokens, TOKEN_ERASE_DOWN, text, i, len);
      i += len - 1;
    } else if (matches(text, length, i, "\x1b[1J", &len)) {
      add_token(tokens, TOKEN_ERASE_UP, text, i, len);
      i += len - 1;
    } else if (matches(text, length, i, "\x1b[2J", &len)) {
      add_token(tokens, TOKEN_ERASE_ALL, text, i, len);
      i += len - 1;
    } else if (matches(text, length, i, "\x1b[K", &len)) {
      add_token(tokens, TOKEN_ERASE_EOL, text, i, len);
      i += len - 1;
    } else if (matches(text, length, i, "\x1b[0K", &len)) {
      add_token(tokens, TOKEN_ERASE_EOL, text, i, len);
      i += len - 1;
    } else if (matches(text, length, i, "\x1b[1K", &len)) {
      add_token(tokens, TOKEN_ERASE_SOL, text, i, len);
      i += len - 1;
    } else if (matches(text, length, i, "\x1b[2K", &len)) {
      add_token(tokens, TOKEN_ERASE_LINE, text, i, len);
      i += len - 1;
    } else if (is_csi_code(text, length, i, &len)) {
      add_token(tokens, TOKEN_CSI_CODE, text, i, len);
      i += len - 1;
    } else if (matches(text, length, i, "\x1b[?1049h", &len)) {
      add_token(tokens, TOKEN_ALT_SCREEN, text, i, len);
      i += len - 1;
    } else if (matches(text, length, i, "\x1b[?1049l", &len)) {
      add_token(tokens, TOKEN_MAIN_SCREEN, text, i, len);
      i += len - 1;
    } else if (matches(text, length, i, "\t", &len)) {
      add_token(tokens, TOKEN_TAB, text, i, len);
      i += len - 1;
    } else {
      int start = i;
      while (i < length && text[i] != '\n' && text[i] != '\r' && text[i] != '\x1b' && text[i] != '\t') {
        i++;
      }
      add_token(tokens, TOKEN_TEXT, text, start, i - start);
      i--;
    }
  }

  return tokens;
}

void write_regular_char(Screen *screen, char c, int width, int height,
                        Attr attr) {
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

void handle_field(Cursor **cursor, int value) {
  if (value == 0) {
    (*cursor)->attr.fg.color = 0;
    (*cursor)->attr.fg.type = COLOR_DEFAULT;
    (*cursor)->attr.bg.color = 0;
    (*cursor)->attr.bg.type = COLOR_DEFAULT;
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
    (*cursor)->attr.fg.type = COLOR_DEFAULT;
    (*cursor)->attr.fg.color = value;
  } else if (value >= 40 && value <= 47) {
    (*cursor)->attr.bg.type = COLOR_DEFAULT;
    (*cursor)->attr.bg.color = value;
  } else if (value >= 90 && value <= 97) {
    (*cursor)->attr.fg.type = COLOR_BRIGHT;
    (*cursor)->attr.fg.color = value;
  } else if (value >= 100 && value <= 107) {
    (*cursor)->attr.bg.type = COLOR_BRIGHT;
    (*cursor)->attr.bg.color = value;
  } else if (value == 22) {
    (*cursor)->attr.bold = 0;
  } else if (value == 24) {
    (*cursor)->attr.underline = 0;
  } else if (value == 27) {
    (*cursor)->attr.reverse = 0;
  }
}

bool starts_with(const char *str, int length, const char *prefix) {
  int prefix_len = strlen(prefix);
  if (length < prefix_len) {
    return false;
  }
  return strncmp(str, prefix, prefix_len) == 0;
}

bool ends_with(const char *str, int length, char suffix) {
  if (length < 1) {
    return false;
  }
  return str[length - 1] == suffix;
}

void modify_cursor(Cursor **cursor, Token token) {
  for (int i = 0; i < token.length; i++) {
    if (isprint(token.value[i])) {
      printf("%c", token.value[i]);
    } else {
      printf("\\x%02x", (unsigned char)token.value[i]);
    }
  }
  printf("\n");

  if (starts_with(token.value, token.length, "\x1b[38;5;")) {
    if (token.length < 8) {
      return;
    }

    char num_str[16];
    int num_len = token.length - 8;
    if (num_len >= sizeof(num_str)) {
      num_len = sizeof(num_str) - 1;
    }
    memcpy(num_str, &token.value[7], num_len);
    num_str[num_len] = '\0';
    int num = atoi(num_str);
    if (num < 0 || num > 255) {
      return;
    }

    (*cursor)->attr.fg.type = COLOR_256;
    (*cursor)->attr.fg.color = num;
  } else if (starts_with(token.value, token.length, "\x1b[48;5;")) {
    if (token.length < 8) {
      return;
    }

    char num_str[16];
    int num_len = token.length - 8;
    if (num_len >= sizeof(num_str)) {
      num_len = sizeof(num_str) - 1;
    }
    memcpy(num_str, &token.value[7], num_len);
    num_str[num_len] = '\0';
    int num = atoi(num_str);
    if (num < 0 || num > 255) {
      return;
    }

    (*cursor)->attr.bg.type = COLOR_256;
    (*cursor)->attr.bg.color = num;
  } else if (ends_with(token.value, token.length, 'm')) {
    if (token.length < 3) {
      return;
    }

    for(int i = 2; i < token.length - 1; ) {
      int j = i;
      while (j < token.length - 1 && token.value[j] != ';') {
        j++;
      }
      char num_str[16];
      int num_len = j - i;
      if (num_len >= sizeof(num_str)) {
        num_len = sizeof(num_str) - 1;
      }
      memcpy(num_str, &token.value[i], num_len);
      num_str[num_len] = '\0';
      int num = atoi(num_str);
      handle_field(cursor, num);
      i = j + 1;
    }
  } else if (ends_with(token.value, token.length, 'H') ||
             ends_with(token.value, token.length, 'f')) {
    if (token.length < 3) {
      return;
    }

    int row = 1;
    int col = 1;

    char *token_copy = (char *)malloc((token.length + 1) * sizeof(char));
    memcpy(token_copy, token.value, token.length);
    token_copy[token.length] = '\0';

    char *part = strtok(token_copy + 2, ";");
    if (part != NULL) {
      row = atoi(part);
      part = strtok(NULL, ";");
      if (part != NULL) {
        col = atoi(part);
      }
    }

    free(token_copy);

    if (row < 1) {
      row = 1;
    }
    if (col < 1) {
      col = 1;
    }

    (*cursor)->y = row - 1;
    (*cursor)->x = col - 1;
  }
}

void print_token(Token t) {
  printf("%d: ", t.type);
  for (int i = 0; i < t.length; i++) {
    if (isprint(t.value[i])) {
      printf("%c", t.value[i]);
    } else {
      printf("\\x%02x", (unsigned char)t.value[i]);
    }
  }
  printf("\n");
}

void write_terminal(Terminal *terminal, const char *text, int length) {
  Tokens *tokens = tokenize(text, length);
  int width = terminal->width;
  int height = terminal->height;

#ifdef DEBUG
  for (int i = 0; i < tokens->count; i++) {
    Token token = tokens->tokens[i];
    print_token(token);
  }
#endif

  for (int i = 0; i < tokens->count; i++) {
    Token token = tokens->tokens[i];

    Screen *screen =
        terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;
    Cursor *cursor = &screen->cursor;
    if (token.type == TOKEN_TEXT) {
      for (int j = 0; j < token.length; j++) {
        write_regular_char(screen, token.value[j], width, height, cursor->attr);
      }
    } else if (token.type == TOKEN_NEWLINE) {
      handle_newline(screen, width, height);
    } else if (token.type == TOKEN_CARRIAGE_RETURN) {
      cursor->x = 0;
    } else if (token.type == TOKEN_CSI_CODE) {
      modify_cursor(&cursor, token);
    } else if (token.type == TOKEN_ERASE_EOL) {
      for (int j = cursor->x; j < width; j++) {
        bzero(&screen->lines[cursor->y].cells[j], sizeof(Cell));
      }
    } else if (token.type == TOKEN_ERASE_SOL) {
      for (int j = 0; j <= cursor->x; j++) {
        bzero(&screen->lines[cursor->y].cells[j], sizeof(Cell));
      }
    } else if (token.type == TOKEN_ERASE_LINE) {
      for (int j = 0; j < width; j++) {
        bzero(&screen->lines[cursor->y].cells[j], sizeof(Cell));
      }
    } else if (token.type == TOKEN_ERASE_DOWN) {
      for (int j = cursor->y; j < height; j++) {
        for (int k = 0; k < width; k++) {
          if (j == cursor->y && k < cursor->x) {
            continue;
          }
          bzero(&screen->lines[j].cells[k], sizeof(Cell));
        }
      }
    } else if (token.type == TOKEN_ERASE_UP) {
      for (int j = 0; j <= cursor->y; j++) {
        for (int k = 0; k < width; k++) {
          if (j == cursor->y && k > cursor->x) {
            continue;
          }
          bzero(&screen->lines[j].cells[k], sizeof(Cell));
        }
      }
    } else if (token.type == TOKEN_ERASE_ALL) {
      for (int j = 0; j < height; j++) {
        for (int k = 0; k < width; k++) {
          bzero(&screen->lines[j].cells[k], sizeof(Cell));
        }
      }
    } else if (token.type == TOKEN_ALT_SCREEN) {
      terminal->using_alt_screen = true;
    } else if (token.type == TOKEN_MAIN_SCREEN) {
      terminal->using_alt_screen = false;
    } else if (token.type == TOKEN_TAB) {
      int next_tab_stop = ((cursor->x / 8) + 1) * 8;
      if (next_tab_stop >= width) {
        handle_newline(screen, width, height);
      } else {
        cursor->x = next_tab_stop;
      }
    }
  }
}

void write_string(Terminal *terminal, const char *str) {
  write_terminal(terminal, str, strlen(str));
}
