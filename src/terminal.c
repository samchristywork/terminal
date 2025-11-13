#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "terminal.h"


void init_screen(Term_Screen *screen, int width, int height) {
  memset(&screen->cursor, 0, sizeof(Term_Cursor));
  screen->lines = (Term_Line *)malloc(height * sizeof(Term_Line));
  for (int i = 0; i < height; i++) {
    screen->lines[i].cells = (Term_Cell *)malloc(width * sizeof(Term_Cell));
    for (int j = 0; j < width; j++) {
      memset(&screen->lines[i].cells[j], 0, sizeof(Term_Cell));
    }
  }
  screen->scrollback.lines = malloc(SCROLLBACK_LINES * sizeof(Term_Cell *));
  screen->scrollback.widths = malloc(SCROLLBACK_LINES * sizeof(int));
  screen->scrollback.capacity = SCROLLBACK_LINES;
  screen->scrollback.count = 0;
  screen->scrollback.head = 0;
  screen->scroll_offset = 0;
}

void free_screen(Term_Screen *screen, int height) {
  for (int i = 0; i < height; i++) {
    free(screen->lines[i].cells);
  }
  free(screen->lines);
  for (int i = 0; i < screen->scrollback.count; i++) {
    free(screen->scrollback.lines[(screen->scrollback.head + i) % screen->scrollback.capacity]);
  }
  free(screen->scrollback.lines);
  free(screen->scrollback.widths);
}

void free_terminal(Terminal *terminal) {
  free_screen(&terminal->screen, terminal->height);
  free_screen(&terminal->alt_screen, terminal->height);
}

void init_terminal(Terminal *terminal, int width, int height) {
  terminal->width = width;
  terminal->height = height;
  terminal->using_alt_screen = false;
  terminal->window_title[0] = '\0';
  terminal->title_dirty = false;
  terminal->partial_len = 0;
  init_screen(&terminal->screen, width, height);
  init_screen(&terminal->alt_screen, width, height);
}

void scroll_screen(Term_Screen *screen, int width, int height) {
  Term_Scrollback *sb = &screen->scrollback;
  int idx;
  if (sb->count < sb->capacity) {
    idx = (sb->head + sb->count) % sb->capacity;
    sb->count++;
  } else {
    idx = sb->head;
    free(sb->lines[idx]);
    sb->head = (sb->head + 1) % sb->capacity;
  }
  sb->lines[idx] = malloc(width * sizeof(Term_Cell));
  memcpy(sb->lines[idx], screen->lines[0].cells, width * sizeof(Term_Cell));
  sb->widths[idx] = width;

  for (int j = 0; j < height - 1; j++) {
    for (int k = 0; k < width; k++) {
      screen->lines[j].cells[k] = screen->lines[j + 1].cells[k];
    }
  }
  for (int k = 0; k < width; k++) {
    memset(&screen->lines[height - 1].cells[k], 0, sizeof(Term_Cell));
  }
}


void handle_newline(Term_Screen *screen, int width, int height) {
  screen->cursor.y++;
  if (screen->cursor.y >= height) {
    screen->cursor.y = height - 1;
    scroll_screen(screen, width, height);
  }
}

void add_token(Term_Tokens *tokens, Term_TokenType type, const char *value,
               int start_index, int length) {
  if (tokens->count % 128 == 0) {
    Term_Token *new_tokens = (Term_Token *)realloc(
        tokens->tokens, (tokens->count + 128) * sizeof(Term_Token));
    if (!new_tokens) return;
    tokens->tokens = new_tokens;
  }
  if (length > 255) length = 255;
  Term_Token *token = &tokens->tokens[tokens->count++];
  token->type = type;
  token->length = length;
  memcpy(token->value, &value[start_index], length);
  token->value[length] = '\0';
}

bool is_csi_code(const char *text, int length, int index, int *code_length) {
  if (index + 2 < length && text[index] == '\x1b' && text[index + 1] == '[') {
    int i = index + 2;
    while (i < length && (unsigned char)text[i] >= 0x30 &&
           (unsigned char)text[i] <= 0x3f) {
      i++;
    }
    while (i < length && (unsigned char)text[i] >= 0x20 &&
           (unsigned char)text[i] <= 0x2f) {
      i++;
    }
    if (i < length && (unsigned char)text[i] >= 0x40 &&
        (unsigned char)text[i] <= 0x7e) {
      *code_length = i - index + 1;
      return true;
    }
  }
  return false;
}

bool is_osc_sequence(const char *text, int length, int index, int *seq_length) {
  if (index + 2 >= length || text[index] != '\x1b' || text[index + 1] != ']')
    return false;
  for (int i = index + 2; i < length; i++) {
    if ((unsigned char)text[i] == 0x07) {
      *seq_length = i - index + 1;
      return true;
    }
    if (i + 1 < length && text[i] == '\x1b' && text[i + 1] == '\\') {
      *seq_length = i - index + 2;
      return true;
    }
  }
  return false;
}

bool matches(const char *text, int length, int index, const char *pattern,
             int *pattern_length) {
  int pat_len = strlen(pattern);
  if (index + pat_len <= length &&
      strncmp(&text[index], pattern, pat_len) == 0) {
    *pattern_length = pat_len;
    return true;
  }
  return false;
}

Term_Tokens *tokenize(const char *text, int length) {
  Term_Tokens *tokens = (Term_Tokens *)malloc(sizeof(Term_Tokens));
  tokens->tokens = (Term_Token *)malloc(128 * sizeof(Term_Token));
  tokens->count = 0;

  for (int i = 0; i < length; i++) {
    int len = 0;
    if (matches(text, length, i, "\n", &len)) {
      add_token(tokens, TOKEN_NEWLINE, text, i, len);
    } else if (matches(text, length, i, "\r", &len)) {
      add_token(tokens, TOKEN_CARRIAGE_RETURN, text, i, len);
    } else if (matches(text, length, i, "\b", &len) ||
               (i < length && (unsigned char)text[i] == 0x7f)) {
      add_token(tokens, TOKEN_BACKSPACE, text, i, 1);
    } else if (matches(text, length, i, "\x1b[J", &len)) {
      add_token(tokens, TOKEN_ERASE_DOWN, text, i, len);
    } else if (matches(text, length, i, "\x1b[0J", &len)) {
      add_token(tokens, TOKEN_ERASE_DOWN, text, i, len);
    } else if (matches(text, length, i, "\x1b[1J", &len)) {
      add_token(tokens, TOKEN_ERASE_UP, text, i, len);
    } else if (matches(text, length, i, "\x1b[2J", &len)) {
      add_token(tokens, TOKEN_ERASE_ALL, text, i, len);
    } else if (matches(text, length, i, "\x1b[3J", &len)) {
      add_token(tokens, TOKEN_ERASE_SCROLLBACK, text, i, len);
    } else if (matches(text, length, i, "\x1b[K", &len)) {
      add_token(tokens, TOKEN_ERASE_EOL, text, i, len);
    } else if (matches(text, length, i, "\x1b[0K", &len)) {
      add_token(tokens, TOKEN_ERASE_EOL, text, i, len);
    } else if (matches(text, length, i, "\x1b[1K", &len)) {
      add_token(tokens, TOKEN_ERASE_SOL, text, i, len);
    } else if (matches(text, length, i, "\x1b[2K", &len)) {
      add_token(tokens, TOKEN_ERASE_LINE, text, i, len);
    } else if (matches(text, length, i, "\x1b[?1049h", &len)) {
      add_token(tokens, TOKEN_ALT_SCREEN, text, i, len);
    } else if (matches(text, length, i, "\x1b[?1049l", &len)) {
      add_token(tokens, TOKEN_MAIN_SCREEN, text, i, len);
    } else if (matches(text, length, i, "\x1b" "7", &len)) {
      add_token(tokens, TOKEN_CSI_CODE, text, i, len);
    } else if (matches(text, length, i, "\x1b" "8", &len)) {
      add_token(tokens, TOKEN_CSI_CODE, text, i, len);
    } else if (matches(text, length, i, "\x1b" "M", &len)) {
      add_token(tokens, TOKEN_REVERSE_INDEX, text, i, len);
    } else if (i + 1 < length && text[i] == '\x1b' &&
               (text[i + 1] == '=' || text[i + 1] == '>' ||
                text[i + 1] == 'c')) {
      len = 2; // application/normal keypad, full reset. Ignore
    } else if (i + 2 < length && text[i] == '\x1b' &&
               (text[i + 1] == '(' || text[i + 1] == ')' ||
                text[i + 1] == '*' || text[i + 1] == '+')) {
      len = 3; // character set designation (e.g., \x1b(B) — ignore
    } else if (is_osc_sequence(text, length, i, &len)) {
      add_token(tokens, TOKEN_OSC, text, i, len);
    } else if (is_csi_code(text, length, i, &len)) {
      add_token(tokens, TOKEN_CSI_CODE, text, i, len);
    } else if (matches(text, length, i, "\t", &len)) {
      add_token(tokens, TOKEN_TAB, text, i, len);
    } else if (text[i] == '\x07') {
      // Bell character - ignore
      len = 1;
    } else {
      int start = i;
      while (i < length) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 0x20 && c < 0x7f) {
          i++;
        } else if (c >= 0x80) {
          i++;  // multi-byte UTF-8 lead or continuation byte
        } else {
          break;
        }
      }
      len = i - start;
      if (len > 0) {
        for (int k = 0; k < len; k += 255)
          add_token(tokens, TOKEN_TEXT, text, start + k,
                    len - k < 255 ? len - k : 255);
      } else {
        len = 1; // skip unrecognised byte; prevent i from decrementing
      }
      i = start;
    }
    i += len - 1;
  }

  return tokens;
}

void write_regular_cell(Term_Screen *screen, const char *data, int data_len,
                        int width, int height, Term_Attr attr) {
  if (screen->cursor.x >= width) {
    handle_newline(screen, width, height);
    screen->cursor.x = 0;
  }

  if (screen->cursor.y < height) {
    Term_Cell *cell = &screen->lines[screen->cursor.y].cells[screen->cursor.x];
    if (data_len > 6) data_len = 6;
    memcpy(cell->data, data, data_len);
    cell->length = data_len;
    cell->attr = attr;
    screen->cursor.x++;
  }
}

void handle_field(Term_Cursor **cursor, int value) {
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

void modify_cursor(Term_Cursor **cursor, Term_Token token) {
  if (starts_with(token.value, token.length, "\x1b[38;5;")) {
    if (token.length < 8) {
      return;
    }

    char num_str[16];
    int num_len = token.length - 8;
    if (num_len >= (int)sizeof(num_str)) {
      num_len = (int)sizeof(num_str) - 1;
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
    if (num_len >= (int)sizeof(num_str)) {
      num_len = (int)sizeof(num_str) - 1;
    }
    memcpy(num_str, &token.value[7], num_len);
    num_str[num_len] = '\0';
    int num = atoi(num_str);
    if (num < 0 || num > 255) {
      return;
    }

    (*cursor)->attr.bg.type = COLOR_256;
    (*cursor)->attr.bg.color = num;
  } else if (starts_with(token.value, token.length, "\x1b[38;2;")) {
    char params[32];
    int params_len = token.length - 8;
    if (params_len <= 0 || params_len >= (int)sizeof(params)) return;
    memcpy(params, &token.value[7], params_len);
    params[params_len] = '\0';
    int r, g, b;
    if (sscanf(params, "%d;%d;%d", &r, &g, &b) == 3) {
      (*cursor)->attr.fg.type = COLOR_RGB;
      (*cursor)->attr.fg.rgb.red   = r < 0 ? 0 : (r > 255 ? 255 : r);
      (*cursor)->attr.fg.rgb.green = g < 0 ? 0 : (g > 255 ? 255 : g);
      (*cursor)->attr.fg.rgb.blue  = b < 0 ? 0 : (b > 255 ? 255 : b);
    }
  } else if (starts_with(token.value, token.length, "\x1b[48;2;")) {
    char params[32];
    int params_len = token.length - 8;
    if (params_len <= 0 || params_len >= (int)sizeof(params)) return;
    memcpy(params, &token.value[7], params_len);
    params[params_len] = '\0';
    int r, g, b;
    if (sscanf(params, "%d;%d;%d", &r, &g, &b) == 3) {
      (*cursor)->attr.bg.type = COLOR_RGB;
      (*cursor)->attr.bg.rgb.red   = r < 0 ? 0 : (r > 255 ? 255 : r);
      (*cursor)->attr.bg.rgb.green = g < 0 ? 0 : (g > 255 ? 255 : g);
      (*cursor)->attr.bg.rgb.blue  = b < 0 ? 0 : (b > 255 ? 255 : b);
    }
  } else if (ends_with(token.value, token.length, 'm')) {
    if (token.length < 3) {
      return;
    }

    for (int i = 2; i < token.length - 1;) {
      int j = i;
      while (j < token.length - 1 && token.value[j] != ';') {
        j++;
      }
      char num_str[16];
      int num_len = j - i;
      if (num_len >= (int)sizeof(num_str)) {
        num_len = (int)sizeof(num_str) - 1;
      }
      memcpy(num_str, &token.value[i], num_len);
      num_str[num_len] = '\0';
      int num = atoi(num_str);
      handle_field(cursor, num);
      i = j + 1;
    }
  } else if (ends_with(token.value, token.length, 'H') ||
             ends_with(token.value, token.length, 'f')) {
    const char *p = token.value + 2;
    const char *end = token.value + token.length - 1; // points at 'H' or 'f'

    int row = 0, col = 0;
    while (p < end && *p != ';') row = row * 10 + (*p++ - '0');
    if (p < end) p++; // skip ';'
    while (p < end) col = col * 10 + (*p++ - '0');

    if (row < 1) row = 1;
    if (col < 1) col = 1;

    (*cursor)->y = row - 1;
    (*cursor)->x = col - 1;
  }
}

void print_token(Term_Token t) {
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

static int csi_param(Term_Token token, int default_val) {
  if (token.length < 3) return default_val;
  char buf[32];
  int param_len = token.length - 3;
  if (param_len <= 0) return default_val;
  if (param_len >= (int)sizeof(buf)) param_len = (int)sizeof(buf) - 1;
  memcpy(buf, &token.value[2], param_len);
  buf[param_len] = '\0';
  int val = atoi(buf);
  return val <= 0 ? default_val : val;
}

static int incomplete_escape_tail(const char *buf, int len) {
  int i = len - 1;
  while (i >= 0 && (unsigned char)buf[i] != 0x1b) i--;
  if (i < 0) return 0;

  int have = len - i;
  const char *p = buf + i;

  if (have == 1) return 1;

  unsigned char next = (unsigned char)p[1];
  if (next == '[') {
    int j = 2;
    while (j < have && (unsigned char)p[j] >= 0x30 && (unsigned char)p[j] <= 0x3f) j++;
    while (j < have && (unsigned char)p[j] >= 0x20 && (unsigned char)p[j] <= 0x2f) j++;
    if (j < have && (unsigned char)p[j] >= 0x40 && (unsigned char)p[j] <= 0x7e) return 0;
    return have;
  } else if (next == ']') {
    for (int j = 2; j < have; j++) {
      if ((unsigned char)p[j] == 0x07) return 0;
      if (j + 1 < have && p[j] == '\x1b' && p[j + 1] == '\\') return 0;
    }
    return have;
  } else if (next == '(' || next == ')' || next == '*' || next == '+') {
    return (have >= 3) ? 0 : have;
  }
  return 0;
}

void write_terminal(Terminal *terminal, const char *text, int length) {
  char *combined = NULL;
  int combined_len = length;

  if (terminal->partial_len > 0) {
    combined_len = terminal->partial_len + length;
    combined = malloc(combined_len);
    if (!combined) return;
    memcpy(combined, terminal->partial_buf, terminal->partial_len);
    memcpy(combined + terminal->partial_len, text, length);
    text = combined;
    terminal->partial_len = 0;
  }

  int tail = incomplete_escape_tail(text, combined_len);
  if (tail >= combined_len) {
    int save = tail < (int)sizeof(terminal->partial_buf) ? tail : (int)sizeof(terminal->partial_buf) - 1;
    memcpy(terminal->partial_buf, text, save);
    terminal->partial_len = save;
    free(combined);
    return;
  }
  if (tail > 0) {
    int save = tail < (int)sizeof(terminal->partial_buf) ? tail : (int)sizeof(terminal->partial_buf) - 1;
    memcpy(terminal->partial_buf, text + combined_len - tail, save);
    terminal->partial_len = save;
    combined_len -= tail;
  }

  Term_Tokens *tokens = tokenize(text, combined_len);
  int width = terminal->width;
  int height = terminal->height;

  Term_Screen *active = terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;
  active->scroll_offset = 0;

#ifdef DEBUG
  for (int i = 0; i < tokens->count; i++) {
    Term_Token token = tokens->tokens[i];
    print_token(token);
  }
#endif

  for (int i = 0; i < tokens->count; i++) {
    Term_Token token = tokens->tokens[i];

    Term_Screen *screen =
        terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;
    Term_Cursor *cursor = &screen->cursor;
    if (token.type == TOKEN_TEXT) {
      int j = 0;
      while (j < token.length) {
        unsigned char c = (unsigned char)token.value[j];
        int char_len;
        if (c < 0x80)       char_len = 1;
        else if (c < 0xE0)  char_len = 2;
        else if (c < 0xF0)  char_len = 3;
        else                char_len = 4;
        if (j + char_len > token.length) char_len = token.length - j;
        write_regular_cell(screen, &token.value[j], char_len, width, height, cursor->attr);
        j += char_len;
      }
    } else if (token.type == TOKEN_NEWLINE) {
      handle_newline(screen, width, height);
    } else if (token.type == TOKEN_CARRIAGE_RETURN) {
      cursor->x = 0;
    } else if (token.type == TOKEN_CSI_CODE) {
      char final = token.value[token.length - 1];
      int n = csi_param(token, 1);
      if (final == 'A') {
        cursor->y -= n;
        if (cursor->y < 0) cursor->y = 0;
      } else if (final == 'B') {
        cursor->y += n;
        if (cursor->y >= height) cursor->y = height - 1;
      } else if (final == 'C') {
        cursor->x += n;
        if (cursor->x >= width) cursor->x = width - 1;
      } else if (final == 'D') {
        cursor->x -= n;
        if (cursor->x < 0) cursor->x = 0;
      } else if (final == 'E') {
        cursor->y += n;
        if (cursor->y >= height) cursor->y = height - 1;
        cursor->x = 0;
      } else if (final == 'F') {
        cursor->y -= n;
        if (cursor->y < 0) cursor->y = 0;
        cursor->x = 0;
      } else if (final == 'G') {
        cursor->x = n - 1;
        if (cursor->x < 0) cursor->x = 0;
        if (cursor->x >= width) cursor->x = width - 1;
      } else if (final == 'd') {
        cursor->y = n - 1;
        if (cursor->y < 0) cursor->y = 0;
        if (cursor->y >= height) cursor->y = height - 1;
      } else if (final == '7' || final == 's') {
        screen->saved_cursor = *cursor;
      } else if (final == '8' || final == 'u') {
        *cursor = screen->saved_cursor;
      } else if (final == 'L') {
        int rows = (n < height - cursor->y) ? n : height - cursor->y;
        for (int j = height - 1; j >= cursor->y + rows; j--)
          memcpy(screen->lines[j].cells, screen->lines[j - rows].cells,
                 width * sizeof(Term_Cell));
        for (int j = cursor->y; j < cursor->y + rows; j++)
          for (int k = 0; k < width; k++)
            memset(&screen->lines[j].cells[k], 0, sizeof(Term_Cell));
      } else if (final == 'M') {
        int rows = (n < height - cursor->y) ? n : height - cursor->y;
        for (int j = cursor->y; j < height - rows; j++)
          memcpy(screen->lines[j].cells, screen->lines[j + rows].cells,
                 width * sizeof(Term_Cell));
        for (int j = height - rows; j < height; j++)
          for (int k = 0; k < width; k++)
            memset(&screen->lines[j].cells[k], 0, sizeof(Term_Cell));
      } else if (final == '@') {
        int cols = (n < width - cursor->x) ? n : width - cursor->x;
        int move = width - cursor->x - cols;
        if (move > 0)
          memmove(&screen->lines[cursor->y].cells[cursor->x + cols],
                  &screen->lines[cursor->y].cells[cursor->x],
                  move * sizeof(Term_Cell));
        for (int k = cursor->x; k < cursor->x + cols; k++)
          memset(&screen->lines[cursor->y].cells[k], 0, sizeof(Term_Cell));
      } else if (final == 'P') {
        int cols = (n < width - cursor->x) ? n : width - cursor->x;
        int move = width - cursor->x - cols;
        if (move > 0)
          memmove(&screen->lines[cursor->y].cells[cursor->x],
                  &screen->lines[cursor->y].cells[cursor->x + cols],
                  move * sizeof(Term_Cell));
        for (int k = width - cols; k < width; k++)
          memset(&screen->lines[cursor->y].cells[k], 0, sizeof(Term_Cell));
      } else {
        modify_cursor(&cursor, token);
      }
    } else if (token.type == TOKEN_ERASE_EOL) {
      for (int j = cursor->x; j < width; j++) {
        memset(&screen->lines[cursor->y].cells[j], 0, sizeof(Term_Cell));
      }
    } else if (token.type == TOKEN_ERASE_SOL) {
      for (int j = 0; j <= cursor->x; j++) {
        memset(&screen->lines[cursor->y].cells[j], 0, sizeof(Term_Cell));
      }
    } else if (token.type == TOKEN_ERASE_LINE) {
      for (int j = 0; j < width; j++) {
        memset(&screen->lines[cursor->y].cells[j], 0, sizeof(Term_Cell));
      }
    } else if (token.type == TOKEN_ERASE_DOWN) {
      for (int j = cursor->y; j < height; j++) {
        for (int k = 0; k < width; k++) {
          if (j == cursor->y && k < cursor->x) {
            continue;
          }
          memset(&screen->lines[j].cells[k], 0, sizeof(Term_Cell));
        }
      }
    } else if (token.type == TOKEN_ERASE_UP) {
      for (int j = 0; j <= cursor->y; j++) {
        for (int k = 0; k < width; k++) {
          if (j == cursor->y && k > cursor->x) {
            continue;
          }
          memset(&screen->lines[j].cells[k], 0, sizeof(Term_Cell));
        }
      }
    } else if (token.type == TOKEN_ERASE_ALL) {
      for (int j = 0; j < height; j++) {
        for (int k = 0; k < width; k++) {
          memset(&screen->lines[j].cells[k], 0, sizeof(Term_Cell));
        }
      }
    } else if (token.type == TOKEN_ERASE_SCROLLBACK) {
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
    } else if (token.type == TOKEN_BACKSPACE) {
      if (cursor->x > 0) {
        cursor->x--;
      }
    } else if (token.type == TOKEN_REVERSE_INDEX) {
      if (cursor->y > 0) {
        cursor->y--;
      } else {
        for (int j = height - 1; j > 0; j--)
          memcpy(screen->lines[j].cells, screen->lines[j - 1].cells,
                 width * sizeof(Term_Cell));
        memset(screen->lines[0].cells, 0, width * sizeof(Term_Cell));
      }
    } else if (token.type == TOKEN_OSC) {
      // Parse: ESC ] cmd ; text BEL/ST
      int i = 2;
      int cmd = 0;
      while (i < token.length && token.value[i] >= '0' && token.value[i] <= '9')
        cmd = cmd * 10 + (token.value[i++] - '0');
      if (i < token.length && token.value[i] == ';') i++;
      if (cmd == 0 || cmd == 1 || cmd == 2) {
        int text_start = i;
        // stop before BEL or ESC (ST)
        int text_end = token.length - 1;
        if (text_end > text_start && token.value[text_end] == '\\')
          text_end--; // strip the '\\' of ESC-backslash ST
        int tlen = text_end - text_start;
        if (tlen < 0) tlen = 0;
        if (tlen >= (int)sizeof(terminal->window_title))
          tlen = sizeof(terminal->window_title) - 1;
        memcpy(terminal->window_title, &token.value[text_start], tlen);
        terminal->window_title[tlen] = '\0';
        terminal->title_dirty = true;
      }
    }
  }

  free(tokens->tokens);
  free(tokens);
  free(combined);
}


void resize_screen(Term_Screen *screen, int old_width, int old_height, int new_width, int new_height) {
  screen->scroll_offset = 0;

  Term_Line *new_lines = (Term_Line *)malloc(new_height * sizeof(Term_Line));

  for (int i = 0; i < new_height; i++) {
    new_lines[i].cells = (Term_Cell *)malloc(new_width * sizeof(Term_Cell));
    for (int j = 0; j < new_width; j++) {
      memset(&new_lines[i].cells[j], 0, sizeof(Term_Cell));
    }
  }

  int copy_height = (old_height < new_height) ? old_height : new_height;
  int copy_width = (old_width < new_width) ? old_width : new_width;

  for (int i = 0; i < copy_height; i++) {
    for (int j = 0; j < copy_width; j++) {
      new_lines[i].cells[j] = screen->lines[i].cells[j];
    }
  }

  for (int i = 0; i < old_height; i++) {
    free(screen->lines[i].cells);
  }
  free(screen->lines);

  screen->lines = new_lines;

  if (screen->cursor.x >= new_width) {
    screen->cursor.x = new_width - 1;
  }
  if (screen->cursor.y >= new_height) {
    screen->cursor.y = new_height - 1;
  }
}

void resize_terminal(Terminal *terminal, int new_width, int new_height) {
  if (new_width <= 0 || new_height <= 0) {
    return;
  }

  if (terminal->width == new_width && terminal->height == new_height) {
    return;
  }

  int old_width = terminal->width;
  int old_height = terminal->height;

  resize_screen(&terminal->screen, old_width, old_height, new_width, new_height);
  resize_screen(&terminal->alt_screen, old_width, old_height, new_width, new_height);

  terminal->width = new_width;
  terminal->height = new_height;
}
