#include <stdlib.h>
#include <string.h>

#include "screen.h"
#include "terminal.h"

static int utf8_decode(const char *s, int len) {
  if (len <= 0) return -1;
  unsigned char c = (unsigned char)s[0];
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && len >= 2)
    return ((c & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
  if ((c & 0xF0) == 0xE0 && len >= 3)
    return ((c & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) |
           ((unsigned char)s[2] & 0x3F);
  if ((c & 0xF8) == 0xF0 && len >= 4)
    return ((c & 0x07) << 18) | (((unsigned char)s[1] & 0x3F) << 12) |
           (((unsigned char)s[2] & 0x3F) << 6) | ((unsigned char)s[3] & 0x3F);
  return -1;
}

static int is_wide_codepoint(int cp) {
  if (cp < 0x1100) return 0;
  if (cp <= 0x115F) return 1;  // Hangul Jamo
  if (cp < 0x2E80) return 0;
  if (cp <= 0x303E) return 1;  // CJK Radicals through CJK Symbols
  if (cp < 0x3041) return 0;
  if (cp <= 0x33FF) return 1;  // Hiragana, Katakana, Bopomofo, CJK Compat
  if (cp < 0x3400) return 0;
  if (cp <= 0x4DBF) return 1;  // CJK Extension A
  if (cp < 0x4E00) return 0;
  if (cp <= 0x9FFF) return 1;  // CJK Unified Ideographs
  if (cp < 0xA000) return 0;
  if (cp <= 0xA4CF) return 1;  // Yi
  if (cp < 0xA960) return 0;
  if (cp <= 0xA97F) return 1;  // Hangul Jamo Extended-A
  if (cp < 0xAC00) return 0;
  if (cp <= 0xD7AF) return 1;  // Hangul Syllables
  if (cp < 0xF900) return 0;
  if (cp <= 0xFAFF) return 1;  // CJK Compatibility Ideographs
  if (cp < 0xFE10) return 0;
  if (cp <= 0xFE6F) return 1;  // Vertical, CJK Compat, Small Forms
  if (cp < 0xFF01) return 0;
  if (cp <= 0xFF60) return 1;  // Fullwidth ASCII and punctuation
  if (cp < 0xFFE0) return 0;
  if (cp <= 0xFFE6) return 1;  // Fullwidth signs
  if (cp < 0x1B000) return 0;
  if (cp <= 0x1B0FF) return 1;  // Kana Supplement
  if (cp < 0x1F300) return 0;
  if (cp <= 0x1F9FF) return 1;  // Emoji and Misc Symbols
  if (cp < 0x20000) return 0;
  if (cp <= 0x2FFFD) return 1;  // CJK Extension B–F
  if (cp < 0x30000) return 0;
  if (cp <= 0x3FFFD) return 1;  // CJK Extension G
  return 0;
}

void init_screen(Term_Screen *screen, int width, int height, int scrollback_lines) {
  memset(&screen->cursor, 0, sizeof(Term_Cursor));
  screen->lines = (Term_Line *)malloc(height * sizeof(Term_Line));
  for (int i = 0; i < height; i++) {
    screen->lines[i].cells = (Term_Cell *)malloc(width * sizeof(Term_Cell));
    for (int j = 0; j < width; j++) {
      memset(&screen->lines[i].cells[j], 0, sizeof(Term_Cell));
    }
  }
  screen->scrollback.lines = malloc(scrollback_lines * sizeof(Term_Cell *));
  screen->scrollback.widths = malloc(scrollback_lines * sizeof(int));
  screen->scrollback.capacity = scrollback_lines;
  screen->scrollback.count = 0;
  screen->scrollback.head = 0;
  screen->scroll_offset = 0;
  screen->scroll_top = 0;
  screen->scroll_bot = height - 1;
  screen->scrolled = false;
}

void free_screen(Term_Screen *screen, int height) {
  for (int i = 0; i < height; i++) {
    free(screen->lines[i].cells);
  }
  free(screen->lines);
  for (int i = 0; i < screen->scrollback.count; i++) {
    free(screen->scrollback.lines[(screen->scrollback.head + i) %
                                  screen->scrollback.capacity]);
  }
  free(screen->scrollback.lines);
  free(screen->scrollback.widths);
}

void reset_screen(Term_Screen *screen, int width, int height) {
  memset(&screen->cursor, 0, sizeof(Term_Cursor));
  memset(&screen->saved_cursor, 0, sizeof(Term_Cursor));
  for (int i = 0; i < height; i++)
    for (int j = 0; j < width; j++)
      memset(&screen->lines[i].cells[j], 0, sizeof(Term_Cell));
  Term_Scrollback *sb = &screen->scrollback;
  for (int i = 0; i < sb->count; i++)
    free(sb->lines[(sb->head + i) % sb->capacity]);
  sb->count = 0;
  sb->head = 0;
  screen->scroll_offset = 0;
  screen->scroll_top = 0;
  screen->scroll_bot = height - 1;
  screen->cursor_hidden = false;
  screen->scrolled = false;
}

void scroll_screen(Term_Screen *screen, int width, int height) {
  (void)height;
  int top = screen->scroll_top;
  int bot = screen->scroll_bot;

  if (top == 0) {
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
    memcpy(sb->lines[idx], screen->lines[top].cells, width * sizeof(Term_Cell));
    sb->widths[idx] = width;
  }

  for (int j = top; j < bot; j++) {
    for (int k = 0; k < width; k++) {
      screen->lines[j].cells[k] = screen->lines[j + 1].cells[k];
    }
  }
  Term_Cell blank = {0};
  blank.attr.bg = screen->cursor.attr.bg;
  for (int k = 0; k < width; k++) {
    screen->lines[bot].cells[k] = blank;
  }
  screen->scrolled = true;
}

void handle_newline(Term_Screen *screen, int width, int height) {
  if (screen->cursor.y == screen->scroll_bot) {
    scroll_screen(screen, width, height);
  } else {
    screen->cursor.y++;
    if (screen->cursor.y >= height)
      screen->cursor.y = height - 1;
  }
}

void write_regular_cell(Term_Screen *screen, const char *data, int data_len,
                        int width, int height, Term_Attr attr) {
  int cp = utf8_decode(data, data_len);
  int char_display_width = (cp >= 0 && is_wide_codepoint(cp)) ? 2 : 1;

  if (screen->cursor.x >= width) {
    handle_newline(screen, width, height);
    screen->cursor.x = 0;
  }

  // Wrap if a wide char won't fit, fill the last column with a space
  if (char_display_width == 2 && screen->cursor.x + 1 >= width) {
    if (screen->cursor.y < height)
      memset(&screen->lines[screen->cursor.y].cells[screen->cursor.x], 0,
             sizeof(Term_Cell));
    handle_newline(screen, width, height);
    screen->cursor.x = 0;
  }

  if (screen->cursor.y < height) {
    Term_Cell *cell = &screen->lines[screen->cursor.y].cells[screen->cursor.x];
    if (data_len > 6)
      data_len = 6;
    memcpy(cell->data, data, data_len);
    cell->length = data_len;
    cell->attr = attr;
    cell->wide = (char_display_width == 2) ? 1 : 0;
    cell->wide_cont = 0;
    screen->cursor.x++;

    if (char_display_width == 2 && screen->cursor.x < width) {
      Term_Cell *cont = &screen->lines[screen->cursor.y].cells[screen->cursor.x];
      *cont = (Term_Cell){.attr = attr, .wide_cont = 1};
      screen->cursor.x++;
    }
  }
}

void resize_screen(Term_Screen *screen, int old_width, int old_height,
                   int new_width, int new_height) {
  screen->scroll_offset = 0;
  screen->scroll_top = 0;
  screen->scroll_bot = new_height - 1;

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
