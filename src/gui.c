#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

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
      bool reverse = cell.attr.reverse || is_cursor;

      if (reverse) {
        unsigned long temp = bg_color;
        bg_color = (cell.attr.fg.color != 0)
                       ? get_color_pixel(gui, cell.attr.fg)
                       : gui->white;
        XSetForeground(gui->display, gui->gc, temp);
      } else {
        XSetForeground(gui->display, gui->gc,
                       (cell.attr.fg.color != 0)
                           ? get_color_pixel(gui, cell.attr.fg)
                           : gui->white);
      }

      XSetForeground(gui->display, gui->gc, bg_color);
      XFillRectangle(gui->display, gui->window, gui->gc, pixel_x, pixel_y,
                     gui->char_width, gui->char_height);

      if (reverse) {
        XSetForeground(gui->display, gui->gc,
                       (cell.attr.fg.color != 0)
                           ? get_color_pixel(gui, cell.attr.fg)
                           : gui->white);
      } else {
        XSetForeground(gui->display, gui->gc,
                       (cell.attr.fg.color != 0)
                           ? get_color_pixel(gui, cell.attr.fg)
                           : gui->white);
      }

      if (cell.length > 0) {
        char ch = cell.data[0];
        XDrawString(gui->display, gui->window, gui->gc, pixel_x,
                    pixel_y + gui->char_ascent, &ch, 1);

        if (cell.attr.underline) {
          XDrawLine(gui->display, gui->window, gui->gc, pixel_x,
                    pixel_y + gui->char_height - 1,
                    pixel_x + gui->char_width - 1,
                    pixel_y + gui->char_height - 1);
        }
      }
    }
  }
}

void setup_sample_terminal(Terminal *terminal) {
  init_terminal(terminal, 80, 24);

  write_string(terminal, "\x1b[31mRed Text\x1b[0m Normal Text\n");
  write_string(terminal,
               "\x1b[1;34mBold Blue\x1b[0m \x1b[42mGreen Background\x1b[0m\n");
  write_string(terminal, "\x1b[4;33mUnderlined Yellow\x1b[0m\n");
  write_string(terminal, "\x1b[7mReverse Video\x1b[0m\n");
  write_string(terminal, "\x1b[38;5;196m256-color Red\x1b[0m "
                         "\x1b[48;5;46m256-color Green BG\x1b[0m\n");
  write_string(terminal, "\nTab Test:\tCol1\tCol2\tCol3\n");
  write_string(terminal,
               "Line with \x1b[31mred\x1b[0m and \x1b[34mblue\x1b[0m words.\n");
  write_string(terminal, "\nCursor will be at end of this line.");
}

void setup_sample_terminal_2(Terminal *terminal) {
  init_terminal(terminal, 80, 24);

  int pipe_fd[2];
  pid_t pid;

  if (pipe(pipe_fd) == -1) {
    perror("pipe");
    return;
  }

  pid = fork();
  if (pid == -1) {
    perror("fork");
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    return;
  }

  if (pid == 0) {
    close(pipe_fd[0]);
    dup2(pipe_fd[1], STDOUT_FILENO);
    dup2(pipe_fd[1], STDERR_FILENO);
    close(pipe_fd[1]);

    execl("/bin/ls", "ls", "-l", "--color=always", (char *)NULL);
    perror("execl");
    exit(1);
  } else {
    close(pipe_fd[1]);

    char buffer[4096];
    ssize_t bytes_read;

    while ((bytes_read = read(pipe_fd[0], buffer, sizeof(buffer) - 1)) > 0) {
      buffer[bytes_read] = '\0';
      write_string(terminal, buffer);
    }

    close(pipe_fd[0]);
    waitpid(pid, NULL, 0);
  }
}

int init_gui(GuiContext *gui) {
  gui->display = XOpenDisplay(NULL);
  if (gui->display == NULL) {
    fprintf(stderr, "Cannot open display\n");
    return 1;
  }

  gui->screen = DefaultScreen(gui->display);
  gui->black = BlackPixel(gui->display, gui->screen);
  gui->white = WhitePixel(gui->display, gui->screen);

  init_colors(gui);

  gui->window =
      XCreateSimpleWindow(gui->display, RootWindow(gui->display, gui->screen),
                          100, 100, 800, 600, 1, gui->white, gui->black);

  XSelectInput(gui->display, gui->window,
               ExposureMask | KeyPressMask | ButtonPressMask);

  XStoreName(gui->display, gui->window, "Terminal GUI");

  gui->gc = XCreateGC(gui->display, gui->window, 0, NULL);
  XSetForeground(gui->display, gui->gc, gui->white);
  XSetBackground(gui->display, gui->gc, gui->black);

  gui->font_info = XLoadQueryFont(gui->display, "fixed");
  if (!gui->font_info) {
    gui->font_info = XLoadQueryFont(gui->display, "*");
    if (!gui->font_info) {
      fprintf(stderr, "Cannot load any font\n");
      XCloseDisplay(gui->display);
      return 1;
    }
  }
  XSetFont(gui->display, gui->gc, gui->font_info->fid);

  gui->char_width = gui->font_info->max_bounds.width;
  gui->char_height = gui->font_info->ascent + gui->font_info->descent;
  gui->char_ascent = gui->font_info->ascent;

  return 0;
}

void handle_events(GuiContext *gui, Terminal *terminal, int *running,
                   XEvent *event) {
  switch (event->type) {
  case Expose:
    XClearWindow(gui->display, gui->window);
    draw_terminal(gui, terminal);
    break;
  case KeyPress:
  case ButtonPress:
    *running = 0;
    break;
  }
}

void cleanup_gui(GuiContext *gui) {
  XFreeGC(gui->display, gui->gc);
  XUnloadFont(gui->display, gui->font_info->fid);
  XDestroyWindow(gui->display, gui->window);
  XCloseDisplay(gui->display);
}

int main() {
  GuiContext gui;
  Terminal terminal;
  XEvent event;
  int running = 1;

  if (init_gui(&gui) != 0) {
    return 1;
  }

  setup_sample_terminal_2(&terminal);

  XMapWindow(gui.display, gui.window);

  while (running) {
    XNextEvent(gui.display, &event);
    handle_events(&gui, &terminal, &running, &event);
  }

  cleanup_gui(&gui);
}
