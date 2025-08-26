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

typedef struct Cell {
  int data[6];
  int length;
  int fg;
  int bg;
  int bold;
  int underline;
  int reverse;
} Cell;

typedef struct Line {
  Cell* cells;
} Line;

typedef struct Cursor {
  int x;
  int y;
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
      screen->lines[i].cells[j].length = 0;
      screen->lines[i].cells[j].fg = NORMAL;
      screen->lines[i].cells[j].bg = NONE;
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
    screen->lines[height - 1].cells[k].length = 0;
    screen->lines[height - 1].cells[k].fg = NORMAL;
    screen->lines[height - 1].cells[k].bg = NONE;
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

int main() {
}
