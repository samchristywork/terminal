#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "terminal.h"

typedef struct {
  Display *display;
  Window window;
  GC gc;
  XFontStruct *font_info;
  int screen;
  unsigned long black, white;
  unsigned long colors[16];
  int char_width, char_height;
  int char_ascent;
} GuiContext;

void init_colors(GuiContext *gui) {
  Colormap colormap = DefaultColormap(gui->display, gui->screen);
  XColor color;

  unsigned long color_values[] = {
      0x000000, // black
      0x800000, // red
      0x008000, // green
      0x808000, // yellow
      0x000080, // blue
      0x800080, // magenta
      0x008080, // cyan
      0xc0c0c0, // white
      0x808080, // bright black
      0xff0000, // bright red
      0x00ff00, // bright green
      0xffff00, // bright yellow
      0x0000ff, // bright blue
      0xff00ff, // bright magenta
      0x00ffff, // bright cyan
      0xffffff  // bright white
  };

  for (int i = 0; i < 16; i++) {
    color.red = ((color_values[i] >> 16) & 0xff) << 8;
    color.green = ((color_values[i] >> 8) & 0xff) << 8;
    color.blue = (color_values[i] & 0xff) << 8;
    color.flags = DoRed | DoGreen | DoBlue;

    if (XAllocColor(gui->display, colormap, &color)) {
      gui->colors[i] = color.pixel;
    } else {
      gui->colors[i] = (i < 8) ? gui->black : gui->white;
    }
  }
}

int main() {
}
