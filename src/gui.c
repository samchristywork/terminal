#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "terminal.h"
#include "args.h"

typedef struct {
  Display *display;
  Window window;
  GC gc;
  XftFont *font;
  XftDraw *xft_draw;
  XftColor xft_colors[16];
  XftColor xft_white;
  XftColor xft_black;
  int screen;
  unsigned long black, white;
  unsigned long colors[16];
  int char_width, char_height;
  int char_ascent;
  int pipe_fd;
  int input_fd;
  pid_t child_pid;
  Pixmap backbuffer;
  int window_width, window_height;
} GuiContext;

void init_colors(GuiContext *gui) {
  Colormap colormap = DefaultColormap(gui->display, gui->screen);
  XColor color;
  XRenderColor xrender_color;
  Visual *visual = DefaultVisual(gui->display, gui->screen);

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

    xrender_color.red = color.red;
    xrender_color.green = color.green;
    xrender_color.blue = color.blue;
    xrender_color.alpha = 0xffff;
    XftColorAllocValue(gui->display, visual, colormap, &xrender_color, &gui->xft_colors[i]);
  }

  xrender_color.red = 0xffff;
  xrender_color.green = 0xffff;
  xrender_color.blue = 0xffff;
  xrender_color.alpha = 0xffff;
  XftColorAllocValue(gui->display, visual, colormap, &xrender_color, &gui->xft_white);

  xrender_color.red = 0;
  xrender_color.green = 0;
  xrender_color.blue = 0;
  xrender_color.alpha = 0xffff;
  XftColorAllocValue(gui->display, visual, colormap, &xrender_color, &gui->xft_black);
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

XftColor* get_xft_color(GuiContext *gui, Term_Color color) {
  if (color.type == COLOR_DEFAULT && color.color >= 30 && color.color <= 37) {
    return &gui->xft_colors[color.color - 30];
  } else if (color.type == COLOR_BRIGHT && color.color >= 90 &&
             color.color <= 97) {
    return &gui->xft_colors[color.color - 90 + 8];
  } else if (color.type == COLOR_DEFAULT && color.color >= 40 &&
             color.color <= 47) {
    return &gui->xft_colors[color.color - 40];
  } else if (color.type == COLOR_BRIGHT && color.color >= 100 &&
             color.color <= 107) {
    return &gui->xft_colors[color.color - 100 + 8];
  }
  return &gui->xft_white;
}

void draw_terminal(GuiContext *gui, Terminal *terminal) {
  Term_Screen *term_screen =
      terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;

  XSetForeground(gui->display, gui->gc, gui->black);
  XFillRectangle(gui->display, gui->backbuffer, gui->gc, 0, 0,
                 gui->window_width, gui->window_height);

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
      XFillRectangle(gui->display, gui->backbuffer, gui->gc, pixel_x, pixel_y,
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
        XftColor *fg_color;

        if (reverse) {
          fg_color = (cell.attr.fg.color != 0) ? get_xft_color(gui, cell.attr.fg) : &gui->xft_white;
        } else {
          fg_color = (cell.attr.fg.color != 0) ? get_xft_color(gui, cell.attr.fg) : &gui->xft_white;
        }

        XftDrawString8(gui->xft_draw, fg_color, gui->font, pixel_x,
                      pixel_y + gui->char_ascent, (FcChar8*)&ch, 1);

        if (cell.attr.underline) {
          XSetForeground(gui->display, gui->gc,
                        (cell.attr.fg.color != 0) ? get_color_pixel(gui, cell.attr.fg) : gui->white);
          XDrawLine(gui->display, gui->backbuffer, gui->gc, pixel_x,
                    pixel_y + gui->char_height - 1,
                    pixel_x + gui->char_width - 1,
                    pixel_y + gui->char_height - 1);
        }
      }
    }
  }

  XCopyArea(gui->display, gui->backbuffer, gui->window, gui->gc, 0, 0,
            gui->window_width, gui->window_height, 0, 0);
}

void init_shell(GuiContext *gui) {
  int output_pipe[2];
  int input_pipe[2];
  pid_t pid;

  if (pipe(output_pipe) == -1 || pipe(input_pipe) == -1) {
    perror("pipe");
    return;
  }

  pid = fork();
  if (pid == -1) {
    perror("fork");
    close(output_pipe[0]);
    close(output_pipe[1]);
    close(input_pipe[0]);
    close(input_pipe[1]);
    return;
  }

  if (pid == 0) {
    close(output_pipe[0]);
    close(input_pipe[1]);

    dup2(input_pipe[0], STDIN_FILENO);
    dup2(output_pipe[1], STDOUT_FILENO);
    dup2(output_pipe[1], STDERR_FILENO);

    close(input_pipe[0]);
    close(output_pipe[1]);

    execl("/bin/sh", "sh", "-i", (char *)NULL);
    perror("execl");
    exit(1);
  } else {
    close(output_pipe[1]);
    close(input_pipe[0]);

    int flags = fcntl(output_pipe[0], F_GETFL, 0);
    fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);

    gui->pipe_fd = output_pipe[0];
    gui->input_fd = input_pipe[1];
    gui->child_pid = pid;
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

int init_gui(GuiContext *gui, int font_size) {
  gui->display = XOpenDisplay(NULL);
  if (gui->display == NULL) {
    fprintf(stderr, "Cannot open display\n");
    return 1;
  }

  gui->screen = DefaultScreen(gui->display);
  gui->black = BlackPixel(gui->display, gui->screen);
  gui->white = WhitePixel(gui->display, gui->screen);

  init_colors(gui);

  gui->window_width = 800;
  gui->window_height = 600;
  gui->window =
      XCreateSimpleWindow(gui->display, RootWindow(gui->display, gui->screen),
                          100, 100, gui->window_width, gui->window_height, 1, gui->white, gui->black);

  XSelectInput(gui->display, gui->window,
               ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);

  XStoreName(gui->display, gui->window, "Terminal GUI");

  gui->gc = XCreateGC(gui->display, gui->window, 0, NULL);
  XSetForeground(gui->display, gui->gc, gui->white);
  XSetBackground(gui->display, gui->gc, gui->black);

  char font_pattern[256];
  snprintf(font_pattern, sizeof(font_pattern), "FreeMono:file=assets/FreeMono.otf:size=%d", font_size);
  gui->font = XftFontOpenName(gui->display, gui->screen, font_pattern);
  if (!gui->font) {
    snprintf(font_pattern, sizeof(font_pattern), "mono-%d", font_size);
    gui->font = XftFontOpenName(gui->display, gui->screen, font_pattern);
    if (!gui->font) {
      fprintf(stderr, "Cannot load FreeMono font or fallback\n");
      XCloseDisplay(gui->display);
      return 1;
    }
  }

  gui->char_width = gui->font->max_advance_width;
  gui->char_height = gui->font->ascent + gui->font->descent;
  gui->char_ascent = gui->font->ascent;

  gui->backbuffer = XCreatePixmap(gui->display, gui->window, gui->window_width,
                                  gui->window_height,
                                  DefaultDepth(gui->display, gui->screen));

  gui->xft_draw = XftDrawCreate(gui->display, gui->backbuffer,
                                DefaultVisual(gui->display, gui->screen),
                                DefaultColormap(gui->display, gui->screen));

  return 0;
}

void read_shell_output(GuiContext *gui, Terminal *terminal) {
  char buffer[4096];
  ssize_t bytes_read;

  while ((bytes_read = read(gui->pipe_fd, buffer, sizeof(buffer) - 1)) > 0) {
    buffer[bytes_read] = '\0';
    write_string(terminal, buffer);
  }
}

void handle_events(GuiContext *gui, Terminal *terminal, int *running,
                   XEvent *event) {
  switch (event->type) {
  case Expose:
    read_shell_output(gui, terminal);
    draw_terminal(gui, terminal);
    break;
  case ConfigureNotify: {
    int new_width = event->xconfigure.width;
    int new_height = event->xconfigure.height;

    if (new_width != gui->window_width || new_height != gui->window_height) {
      gui->window_width = new_width;
      gui->window_height = new_height;

      XFreePixmap(gui->display, gui->backbuffer);
      gui->backbuffer = XCreatePixmap(gui->display, gui->window, gui->window_width,
                                      gui->window_height,
                                      DefaultDepth(gui->display, gui->screen));

      XftDrawDestroy(gui->xft_draw);
      gui->xft_draw = XftDrawCreate(gui->display, gui->backbuffer,
                                    DefaultVisual(gui->display, gui->screen),
                                    DefaultColormap(gui->display, gui->screen));

      int term_cols = (new_width - 20) / gui->char_width;
      int term_rows = (new_height - 20) / gui->char_height;

      if (term_cols < 1) term_cols = 1;
      if (term_rows < 1) term_rows = 1;

      resize_terminal(terminal, term_cols, term_rows);
      draw_terminal(gui, terminal);
    }
    break;
  }
  case KeyPress: {
    char buffer[32];
    KeySym keysym;
    XLookupString(&event->xkey, buffer, sizeof(buffer), &keysym, NULL);

    if (keysym == XK_BackSpace) {
      buffer[0] = 0x7f;
      write(gui->input_fd, buffer, 1);
    } else if (keysym == XK_Up) {
      write(gui->input_fd, "\x1b[A", 3);
    } else if (keysym == XK_Down) {
      write(gui->input_fd, "\x1b[B", 3);
    } else if (keysym == XK_Right) {
      write(gui->input_fd, "\x1b[C", 3);
    } else if (keysym == XK_Left) {
      write(gui->input_fd, "\x1b[D", 3);
    } else {
      int len = XLookupString(&event->xkey, buffer, sizeof(buffer), NULL, NULL);
      if (len > 0) {
        write(gui->input_fd, buffer, len);
      }
    }
    break;
  }
  case ButtonPress:
    *running = 0;
    break;
  }
}

void cleanup_gui(GuiContext *gui) {
  if (gui->pipe_fd >= 0) {
    close(gui->pipe_fd);
  }
  if (gui->input_fd >= 0) {
    close(gui->input_fd);
  }
  if (gui->child_pid > 0) {
    kill(gui->child_pid, SIGTERM);
    waitpid(gui->child_pid, NULL, 0);
  }

  Colormap colormap = DefaultColormap(gui->display, gui->screen);
  Visual *visual = DefaultVisual(gui->display, gui->screen);
  for (int i = 0; i < 16; i++) {
    XftColorFree(gui->display, visual, colormap, &gui->xft_colors[i]);
  }
  XftColorFree(gui->display, visual, colormap, &gui->xft_white);
  XftColorFree(gui->display, visual, colormap, &gui->xft_black);

  XftDrawDestroy(gui->xft_draw);
  XftFontClose(gui->display, gui->font);
  XFreePixmap(gui->display, gui->backbuffer);
  XFreeGC(gui->display, gui->gc);
  XDestroyWindow(gui->display, gui->window);
  XCloseDisplay(gui->display);
}

int main(int argc, char *argv[]) {
  Args args;
  parse_args(argc, argv, &args);

  GuiContext gui;
  Terminal terminal;
  XEvent event;
  int running = 1;

  if (init_gui(&gui, args.font_size) != 0) {
    return 1;
  }

  init_terminal(&terminal, 80, 24);
  setup_sample_terminal(&terminal);
  init_shell(&gui);

  XMapWindow(gui.display, gui.window);

  while (running) {
    while (XPending(gui.display)) {
      XNextEvent(gui.display, &event);
      handle_events(&gui, &terminal, &running, &event);
    }

    int status;
    pid_t result = waitpid(gui.child_pid, &status, WNOHANG);
    if (result != 0) {
      running = 0;
      break;
    }

    read_shell_output(&gui, &terminal);
    draw_terminal(&gui, &terminal);
    XFlush(gui.display);

    usleep(10000);
  }

  cleanup_gui(&gui);
}
