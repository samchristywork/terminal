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

unsigned long get_color_pixel(GuiContext *gui, Term_Color color) {
  if (color.type == COLOR_DEFAULT && color.color >= 30 && color.color <= 37) {
    return gui->colors[color.color - 30];
  } else if (color.type == COLOR_BRIGHT && color.color >= 90 &&
             color.color <= 97) {
    return gui->colors[color.color - 90 + 8];
  } else if (color.type == COLOR_DEFAULT && color.color >= 40 &&
             color.color <= 47) {
    return gui->colors[color.color - 40];
  } else if (color.type == COLOR_BRIGHT && color.color >= 100 &&
             color.color <= 107) {
    return gui->colors[color.color - 100 + 8];
  }
  return gui->white;
}

void draw_terminal(GuiContext *gui, Terminal *terminal) {
  Term_Screen *term_screen =
      terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;

  for (int y = 0; y < terminal->height; y++) {
    for (int x = 0; x < terminal->width; x++) {
      Term_Cell cell = term_screen->lines[y].cells[x];

      int pixel_x = x * gui->char_width + 10;
      int pixel_y = y * gui->char_height + 10;

      unsigned long bg_color = gui->black;
      if (cell.attr.bg.color != 0) {
        bg_color = get_color_pixel(gui, cell.attr.bg);
      }

      bool is_cursor =
          (term_screen->cursor.x == x && term_screen->cursor.y == y);

      XSetForeground(gui->display, gui->gc,
                     (cell.attr.fg.color != 0)
                         ? get_color_pixel(gui, cell.attr.fg)
                         : gui->white);

      XSetForeground(gui->display, gui->gc, bg_color);
      XFillRectangle(gui->display, gui->window, gui->gc, pixel_x, pixel_y,
                     gui->char_width, gui->char_height);

      XSetForeground(gui->display, gui->gc,
                     (cell.attr.fg.color != 0)
                         ? get_color_pixel(gui, cell.attr.fg)
                         : gui->white);

      if (cell.length > 0) {
        char ch = cell.data[0];
        XDrawString(gui->display, gui->window, gui->gc, pixel_x,
                    pixel_y + gui->char_ascent, &ch, 1);
      }
    }
  }
}

int main() {
}
