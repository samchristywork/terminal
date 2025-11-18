#include <stdlib.h>
#include <string.h>

#include "screen.h"
#include "terminal.h"

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
  for (int k = 0; k < width; k++) {
    memset(&screen->lines[bot].cells[k], 0, sizeof(Term_Cell));
  }
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
  if (screen->cursor.x >= width) {
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
    screen->cursor.x++;
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
