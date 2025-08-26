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
  char character;
  int foreground_color;
  int background_color;
} Cell;

typedef struct Line {
  Cell* cells;
} Line;

typedef struct Cursor {
  int x;
  int y;
} Cursor;

typedef struct Terminal {
  int width;
  int height;
  Cursor cursor;
  Line* lines;
} Terminal;

int main() {
}
