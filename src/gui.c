#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pty.h>
#include <time.h>

#include "terminal.h"
#include "args.h"
#include "log.h"

#define LINE_GAP 2

typedef struct {
  Display *display;
  Window window;
  GC gc;
  XftFont *font;
  XftFont *font_bold;
  XftDraw *xft_draw;
  XftColor xft_colors[16];
  XftColor xft_white;
  XftColor xft_black;
  XftColor xft_default_fg;
  XftColor xft_default_bg;
  int screen;
  unsigned long black, white;
  unsigned long colors[16];
  unsigned long default_fg;
  unsigned long default_bg;
  int char_width, char_height;
  int char_ascent;
  int pipe_fd;
  int input_fd;
  pid_t child_pid;
  Pixmap backbuffer;
  int window_width, window_height;
  Atom atom_clipboard;
  Atom atom_utf8_string;
  Atom atom_xsel_data;
  bool selecting;
  bool has_selection;
  int sel_anchor_x, sel_anchor_y;
  int sel_cur_x, sel_cur_y;
  char *selection_text;
  int selection_len;
  bool cursor_visible;
  struct timespec last_blink;
} GuiContext;

void init_colors(GuiContext *gui, Args *args) {
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
    if (args->palette[i] != -1)
      color_values[i] = (unsigned long)args->palette[i];
  }

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

  unsigned long fg_val = (args->fg != -1) ? (unsigned long)args->fg : 0xffffff;
  unsigned long bg_val = (args->bg != -1) ? (unsigned long)args->bg : 0x000000;

  color.red   = ((fg_val >> 16) & 0xff) << 8;
  color.green = ((fg_val >>  8) & 0xff) << 8;
  color.blue  = ( fg_val        & 0xff) << 8;
  color.flags = DoRed | DoGreen | DoBlue;
  gui->default_fg = XAllocColor(gui->display, colormap, &color) ? color.pixel : gui->white;

  color.red   = ((bg_val >> 16) & 0xff) << 8;
  color.green = ((bg_val >>  8) & 0xff) << 8;
  color.blue  = ( bg_val        & 0xff) << 8;
  color.flags = DoRed | DoGreen | DoBlue;
  gui->default_bg = XAllocColor(gui->display, colormap, &color) ? color.pixel : gui->black;

  xrender_color.red   = ((fg_val >> 16) & 0xff) << 8;
  xrender_color.green = ((fg_val >>  8) & 0xff) << 8;
  xrender_color.blue  = ( fg_val        & 0xff) << 8;
  xrender_color.alpha = 0xffff;
  XftColorAllocValue(gui->display, visual, colormap, &xrender_color, &gui->xft_default_fg);

  xrender_color.red   = ((bg_val >> 16) & 0xff) << 8;
  xrender_color.green = ((bg_val >>  8) & 0xff) << 8;
  xrender_color.blue  = ( bg_val        & 0xff) << 8;
  xrender_color.alpha = 0xffff;
  XftColorAllocValue(gui->display, visual, colormap, &xrender_color, &gui->xft_default_bg);
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
  } else if (color.type == COLOR_256) {
    int idx = color.color;
    unsigned long r, g, b;

    if (idx < 16) {
      return gui->colors[idx];
    } else if (idx < 232) {
      idx -= 16;
      r = (idx / 36) * 51;
      g = ((idx / 6) % 6) * 51;
      b = (idx % 6) * 51;
      return (r << 16) | (g << 8) | b;
    } else {
      int gray = 8 + (idx - 232) * 10;
      return (gray << 16) | (gray << 8) | gray;
    }
  } else if (color.type == COLOR_RGB) {
    return ((unsigned long)color.rgb.red << 16) |
           ((unsigned long)color.rgb.green << 8) |
           (unsigned long)color.rgb.blue;
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
  } else if (color.type == COLOR_256) {
    static XftColor xft_color_cache[256];
    static bool xft_color_cached[256] = {false};
    int idx = color.color;

    if (idx >= 0 && idx < 256) {
      if (idx < 16) {
        return &gui->xft_colors[idx];
      }

      if (!xft_color_cached[idx]) {
        XRenderColor xrender_color;
        unsigned long r, g, b;

        if (idx < 232) {
          int cube_idx = idx - 16;
          r = (cube_idx / 36) * 51;
          g = ((cube_idx / 6) % 6) * 51;
          b = (cube_idx % 6) * 51;
        } else {
          int gray = 8 + (idx - 232) * 10;
          r = g = b = gray;
        }

        xrender_color.red = r << 8;
        xrender_color.green = g << 8;
        xrender_color.blue = b << 8;
        xrender_color.alpha = 0xffff;
        XftColorAllocValue(gui->display, DefaultVisual(gui->display, gui->screen),
                           DefaultColormap(gui->display, gui->screen),
                           &xrender_color, &xft_color_cache[idx]);
        xft_color_cached[idx] = true;
      }
      return &xft_color_cache[idx];
    }
  } else if (color.type == COLOR_RGB) {
    static XftColor xft_rgb_color;
    XRenderColor xrender_color;
    xrender_color.red   = (unsigned short)(color.rgb.red   << 8);
    xrender_color.green = (unsigned short)(color.rgb.green << 8);
    xrender_color.blue  = (unsigned short)(color.rgb.blue  << 8);
    xrender_color.alpha = 0xffff;
    XftColorAllocValue(gui->display, DefaultVisual(gui->display, gui->screen),
                       DefaultColormap(gui->display, gui->screen),
                       &xrender_color, &xft_rgb_color);
    return &xft_rgb_color;
  }
  return &gui->xft_white;
}

static bool cell_in_selection(GuiContext *gui, int x, int y) {
  if (!gui->has_selection) return false;
  int ax = gui->sel_anchor_x, ay = gui->sel_anchor_y;
  int bx = gui->sel_cur_x,    by = gui->sel_cur_y;
  int start_x, start_y, end_x, end_y;
  if (ay < by || (ay == by && ax <= bx)) {
    start_x = ax; start_y = ay; end_x = bx; end_y = by;
  } else {
    start_x = bx; start_y = by; end_x = ax; end_y = ay;
  }
  if (y < start_y || y > end_y) return false;
  if (y == start_y && x < start_x) return false;
  if (y == end_y && x > end_x) return false;
  return true;
}

static void build_selection_text(GuiContext *gui, Terminal *terminal) {
  free(gui->selection_text);
  gui->selection_text = NULL;
  gui->selection_len = 0;
  if (!gui->has_selection) return;

  int ax = gui->sel_anchor_x, ay = gui->sel_anchor_y;
  int bx = gui->sel_cur_x,    by = gui->sel_cur_y;
  int start_x, start_y, end_x, end_y;
  if (ay < by || (ay == by && ax <= bx)) {
    start_x = ax; start_y = ay; end_x = bx; end_y = by;
  } else {
    start_x = bx; start_y = by; end_x = ax; end_y = ay;
  }

  Term_Screen *scr = terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;
  Term_Scrollback *sb = &scr->scrollback;
  int scroll_offset = scr->scroll_offset;

  int max_len = (terminal->width + 1) * (end_y - start_y + 1) + 1;
  char *buf = malloc(max_len);
  if (!buf) return;
  int pos = 0;

  for (int y = start_y; y <= end_y; y++) {
    int x0 = (y == start_y) ? start_x : 0;
    int x1 = (y == end_y)   ? end_x   : terminal->width - 1;

    for (int x = x0; x <= x1; x++) {
      Term_Cell cell;
      int combined = sb->count - scroll_offset + y;
      if (combined < 0) {
        memset(&cell, 0, sizeof(Term_Cell));
      } else if (combined < sb->count) {
        int idx = (sb->head + combined) % sb->capacity;
        cell = (x < sb->widths[idx]) ? sb->lines[idx][x] : (Term_Cell){0};
      } else {
        cell = scr->lines[combined - sb->count].cells[x];
      }
      buf[pos++] = (cell.length > 0) ? cell.data[0] : ' ';
    }
    if (y < end_y) buf[pos++] = '\n';
  }

  buf[pos] = '\0';
  gui->selection_text = buf;
  gui->selection_len = pos;
}

void draw_terminal(GuiContext *gui, Terminal *terminal) {
  Term_Screen *term_screen =
      terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;

  XSetForeground(gui->display, gui->gc, gui->default_bg);
  XFillRectangle(gui->display, gui->backbuffer, gui->gc, 0, 0,
                 gui->window_width, gui->window_height);

  int scroll_offset = term_screen->scroll_offset;
  Term_Scrollback *sb = &term_screen->scrollback;

  for (int y = 0; y < terminal->height; y++) {
    for (int x = 0; x < terminal->width; x++) {
      Term_Cell cell;
      int combined = sb->count - scroll_offset + y;
      if (combined < 0) {
        memset(&cell, 0, sizeof(Term_Cell));
      } else if (combined < sb->count) {
        int idx = (sb->head + combined) % sb->capacity;
        cell = (x < sb->widths[idx]) ? sb->lines[idx][x] : (Term_Cell){0};
      } else {
        cell = term_screen->lines[combined - sb->count].cells[x];
      }

      int pixel_x = x * gui->char_width + 10;
      int pixel_y = y * (gui->char_height + LINE_GAP) + 10;

      unsigned long bg_color = gui->default_bg;
      if (cell.attr.bg.color != 0 || cell.attr.bg.type == COLOR_RGB) {
        bg_color = get_color_pixel(gui, cell.attr.bg);
      }

      bool is_cursor = gui->cursor_visible && (scroll_offset == 0) &&
          (term_screen->cursor.x == x && term_screen->cursor.y == y);
      bool in_selection = cell_in_selection(gui, x, y);
      bool reverse = cell.attr.reverse || is_cursor || in_selection;

      unsigned long text_color;
      if (reverse) {
        text_color = bg_color;
        bg_color = (cell.attr.fg.color != 0 || cell.attr.fg.type == COLOR_RGB)
                       ? get_color_pixel(gui, cell.attr.fg)
                       : gui->default_fg;
      } else {
        text_color = (cell.attr.fg.color != 0 || cell.attr.fg.type == COLOR_RGB)
                         ? get_color_pixel(gui, cell.attr.fg)
                         : gui->default_fg;
      }

      XSetForeground(gui->display, gui->gc, bg_color);
      XFillRectangle(gui->display, gui->backbuffer, gui->gc, pixel_x, pixel_y,
                     gui->char_width, gui->char_height);

      if (cell.length > 0) {
        XftColor *fg_color;
        XftFont *font_to_use = cell.attr.bold ? gui->font_bold : gui->font;

        if (reverse) {
          fg_color = (cell.attr.bg.color != 0 || cell.attr.bg.type == COLOR_RGB) ? get_xft_color(gui, cell.attr.bg) : &gui->xft_default_bg;
        } else {
          fg_color = (cell.attr.fg.color != 0 || cell.attr.fg.type == COLOR_RGB) ? get_xft_color(gui, cell.attr.fg) : &gui->xft_default_fg;
        }

        XftDrawStringUtf8(gui->xft_draw, fg_color, font_to_use, pixel_x,
                          pixel_y + gui->char_ascent, (FcChar8*)cell.data, cell.length);

        if (cell.attr.underline) {
          XSetForeground(gui->display, gui->gc, text_color);
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

void init_shell(GuiContext *gui, int cols, int rows) {
  int master, slave;
  pid_t pid;

  LOG_INFO_MSG("Initializing shell subprocess");

  struct winsize ws = {
    .ws_row = rows,
    .ws_col = cols,
    .ws_xpixel = 0,
    .ws_ypixel = 0,
  };

  if (openpty(&master, &slave, NULL, NULL, &ws) == -1) {
    perror("openpty");
    LOG_ERROR_MSG("Failed to open PTY");
    return;
  }

  pid = fork();
  if (pid == -1) {
    perror("fork");
    LOG_ERROR_MSG("Failed to fork shell subprocess");
    close(master);
    close(slave);
    return;
  }

  if (pid == 0) {
    close(master);

    setsid();
    ioctl(slave, TIOCSCTTY, 0);

    dup2(slave, STDIN_FILENO);
    dup2(slave, STDOUT_FILENO);
    dup2(slave, STDERR_FILENO);

    if (slave > STDERR_FILENO)
      close(slave);

    setenv("TERM", "xterm-256color", 1);
    execl("/bin/sh", "sh", NULL);
    perror("execl");
    exit(1);
  } else {
    close(slave);

    int flags = fcntl(master, F_GETFL, 0);
    fcntl(master, F_SETFL, flags | O_NONBLOCK);

    gui->pipe_fd = master;
    gui->input_fd = -1;
    gui->child_pid = pid;
    LOG_INFO_MSG("Shell subprocess started with PID %d", pid);
  }
}

int init_gui(GuiContext *gui, Args *args) {
  int font_size = args->font_size;
  LOG_INFO_MSG("Initializing GUI with font size %d", font_size);

  gui->display = XOpenDisplay(NULL);
  if (gui->display == NULL) {
    fprintf(stderr, "Cannot open display\n");
    LOG_ERROR_MSG("Cannot open X11 display");
    return 1;
  }

  gui->screen = DefaultScreen(gui->display);
  gui->black = BlackPixel(gui->display, gui->screen);
  gui->white = WhitePixel(gui->display, gui->screen);

  gui->atom_clipboard = XInternAtom(gui->display, "CLIPBOARD", False);
  gui->atom_utf8_string = XInternAtom(gui->display, "UTF8_STRING", False);
  gui->atom_xsel_data = XInternAtom(gui->display, "XSEL_DATA", False);
  gui->selecting = false;
  gui->has_selection = false;
  gui->selection_text = NULL;
  gui->selection_len = 0;

  init_colors(gui, args);

  gui->window_width = 800;
  gui->window_height = 600;
  gui->window =
      XCreateSimpleWindow(gui->display, RootWindow(gui->display, gui->screen),
                          100, 100, gui->window_width, gui->window_height, 1, gui->white, gui->black);

  XSelectInput(gui->display, gui->window,
               ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
               Button1MotionMask | StructureNotifyMask);

  XStoreName(gui->display, gui->window, "Terminal GUI");

  gui->gc = XCreateGC(gui->display, gui->window, 0, NULL);
  XSetForeground(gui->display, gui->gc, gui->white);
  XSetBackground(gui->display, gui->gc, gui->black);

  char font_pattern[1024];
  char exe_path[512];
  char exe_dir[512];
  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len != -1) {
    exe_path[len] = '\0';
    char *slash = strrchr(exe_path, '/');
    if (slash) {
      *slash = '\0';
      snprintf(exe_dir, sizeof(exe_dir), "%s", exe_path);
    } else {
      snprintf(exe_dir, sizeof(exe_dir), ".");
    }
  } else {
    snprintf(exe_dir, sizeof(exe_dir), ".");
  }

  if (args->font) {
    if (strstr(args->font, "size="))
      snprintf(font_pattern, sizeof(font_pattern), "%s", args->font);
    else
      snprintf(font_pattern, sizeof(font_pattern), "%s:size=%d", args->font, font_size);
    gui->font = XftFontOpenName(gui->display, gui->screen, font_pattern);
    if (!gui->font) {
      fprintf(stderr, "Cannot load font '%s'\n", args->font);
      LOG_ERROR_MSG("Cannot load configured font: %s", args->font);
      XCloseDisplay(gui->display);
      return 1;
    }
    gui->font_bold = gui->font;
  } else {
    snprintf(font_pattern, sizeof(font_pattern), "Iosevka Nerd Font Mono:size=%d", font_size);
    gui->font = XftFontOpenName(gui->display, gui->screen, font_pattern);
    if (!gui->font) {
      LOG_WARNING_MSG("Cannot load Iosevka font, trying FreeMono");
      char repo_dir[512];
      char *sep = strrchr(exe_dir, '/');
      if (sep && sep != exe_dir) {
        int rlen = (int)(sep - exe_dir);
        memcpy(repo_dir, exe_dir, rlen);
        repo_dir[rlen] = '\0';
      } else {
        snprintf(repo_dir, sizeof(repo_dir), "%s", exe_dir);
      }
      char bundled_font[1024];
      snprintf(bundled_font, sizeof(bundled_font), "%s/assets/FreeMono.otf", repo_dir);
      FcConfigAppFontAddFile(NULL, (const FcChar8 *)bundled_font);
      snprintf(bundled_font, sizeof(bundled_font), "%s/assets/FreeMonoBold.otf", repo_dir);
      FcConfigAppFontAddFile(NULL, (const FcChar8 *)bundled_font);
      snprintf(font_pattern, sizeof(font_pattern), "FreeMono:size=%d", font_size);
      gui->font = XftFontOpenName(gui->display, gui->screen, font_pattern);
    }
    if (!gui->font) {
      LOG_WARNING_MSG("Cannot load FreeMono font, trying monospace fallback");
      snprintf(font_pattern, sizeof(font_pattern), "monospace:size=%d", font_size);
      gui->font = XftFontOpenName(gui->display, gui->screen, font_pattern);
    }
    if (!gui->font) {
      fprintf(stderr, "Cannot load any suitable font\n");
      LOG_ERROR_MSG("Cannot load any suitable font");
      XCloseDisplay(gui->display);
      return 1;
    }

    snprintf(font_pattern, sizeof(font_pattern), "Iosevka Nerd Font Mono:weight=bold:size=%d", font_size);
    gui->font_bold = XftFontOpenName(gui->display, gui->screen, font_pattern);
    if (!gui->font_bold) {
      snprintf(font_pattern, sizeof(font_pattern), "monospace:weight=bold:size=%d", font_size);
      gui->font_bold = XftFontOpenName(gui->display, gui->screen, font_pattern);
    }
    if (!gui->font_bold)
      gui->font_bold = gui->font;
  }
  LOG_INFO_MSG("Loaded font: %s", font_pattern);

  gui->char_width = gui->font->max_advance_width;
  if (gui->font_bold && gui->font_bold != gui->font &&
      gui->font_bold->max_advance_width > gui->char_width)
    gui->char_width = gui->font_bold->max_advance_width;
  gui->char_height = gui->font->ascent + gui->font->descent;
  gui->char_ascent = gui->font->ascent;

  gui->backbuffer = XCreatePixmap(gui->display, gui->window, gui->window_width,
                                  gui->window_height,
                                  DefaultDepth(gui->display, gui->screen));

  gui->xft_draw = XftDrawCreate(gui->display, gui->backbuffer,
                                DefaultVisual(gui->display, gui->screen),
                                DefaultColormap(gui->display, gui->screen));

  gui->cursor_visible = true;
  clock_gettime(CLOCK_MONOTONIC, &gui->last_blink);

  return 0;
}

void read_shell_output(GuiContext *gui, Terminal *terminal) {
  char buffer[4096];
  ssize_t bytes_read;

  while ((bytes_read = read(gui->pipe_fd, buffer, sizeof(buffer))) > 0) {
    write_terminal(terminal, buffer, bytes_read);
  }
}

void handle_events(GuiContext *gui, Terminal *terminal, XEvent *event) {
  switch (event->type) {
  case Expose:
    read_shell_output(gui, terminal);
    draw_terminal(gui, terminal);
    break;
  case ConfigureNotify: {
    int new_width = event->xconfigure.width;
    int new_height = event->xconfigure.height;

    if (new_width != gui->window_width || new_height != gui->window_height) {
      LOG_DEBUG_MSG("Window resized to %dx%d", new_width, new_height);
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
      int term_rows = (new_height - 20) / (gui->char_height + LINE_GAP);

      if (term_cols < 1) term_cols = 1;
      if (term_rows < 1) term_rows = 1;

      LOG_DEBUG_MSG("Terminal resized to %dx%d", term_cols, term_rows);
      resize_terminal(terminal, term_cols, term_rows);

      struct winsize ws = {
        .ws_row = term_rows,
        .ws_col = term_cols,
        .ws_xpixel = 0,
        .ws_ypixel = 0,
      };
      ioctl(gui->pipe_fd, TIOCSWINSZ, &ws);
      kill(gui->child_pid, SIGWINCH);

      draw_terminal(gui, terminal);
    }
    break;
  }
  case KeyPress: {
    gui->cursor_visible = true;
    clock_gettime(CLOCK_MONOTONIC, &gui->last_blink);
    char buffer[32];
    KeySym keysym;
    XLookupString(&event->xkey, buffer, sizeof(buffer), &keysym, NULL);

    Term_Screen *scr = terminal->using_alt_screen
                           ? &terminal->alt_screen : &terminal->screen;
    int max_scroll = scr->scrollback.count;

    if (keysym == XK_Prior && (event->xkey.state & ShiftMask)) {
      scr->scroll_offset += terminal->height;
      if (scr->scroll_offset > max_scroll) scr->scroll_offset = max_scroll;
      draw_terminal(gui, terminal);
    } else if (keysym == XK_Next && (event->xkey.state & ShiftMask)) {
      scr->scroll_offset -= terminal->height;
      if (scr->scroll_offset < 0) scr->scroll_offset = 0;
      draw_terminal(gui, terminal);
    } else if (keysym == XK_c && (event->xkey.state & ControlMask) && (event->xkey.state & ShiftMask)) {
      if (gui->has_selection)
        XSetSelectionOwner(gui->display, gui->atom_clipboard, gui->window, CurrentTime);
    } else if (keysym == XK_v && (event->xkey.state & ControlMask) && (event->xkey.state & ShiftMask)) {
      XConvertSelection(gui->display, gui->atom_clipboard, gui->atom_utf8_string,
                        gui->atom_xsel_data, gui->window, CurrentTime);
    } else if (keysym == XK_BackSpace) {
      write(gui->pipe_fd, "\x7f", 1);
    } else if (keysym == XK_Return || keysym == XK_KP_Enter) {
      write(gui->pipe_fd, "\r", 1);
    } else if (keysym == XK_Tab) {
      write(gui->pipe_fd, "\t", 1);
    } else if (keysym == XK_Escape) {
      write(gui->pipe_fd, "\x1b", 1);
    } else if (keysym == XK_Up) {
      write(gui->pipe_fd, (event->xkey.state & ControlMask) ? "\x1b[1;5A" : "\x1b[A",
            (event->xkey.state & ControlMask) ? 6 : 3);
    } else if (keysym == XK_Down) {
      write(gui->pipe_fd, (event->xkey.state & ControlMask) ? "\x1b[1;5B" : "\x1b[B",
            (event->xkey.state & ControlMask) ? 6 : 3);
    } else if (keysym == XK_Right) {
      write(gui->pipe_fd, (event->xkey.state & ControlMask) ? "\x1b[1;5C" : "\x1b[C",
            (event->xkey.state & ControlMask) ? 6 : 3);
    } else if (keysym == XK_Left) {
      write(gui->pipe_fd, (event->xkey.state & ControlMask) ? "\x1b[1;5D" : "\x1b[D",
            (event->xkey.state & ControlMask) ? 6 : 3);
    } else if (keysym == XK_Home) {
      write(gui->pipe_fd, "\x1b[H", 3);
    } else if (keysym == XK_End) {
      write(gui->pipe_fd, "\x1b[F", 3);
    } else if (keysym == XK_Insert) {
      write(gui->pipe_fd, "\x1b[2~", 4);
    } else if (keysym == XK_Delete) {
      write(gui->pipe_fd, "\x1b[3~", 4);
    } else if (keysym == XK_Prior) {
      write(gui->pipe_fd, "\x1b[5~", 4);
    } else if (keysym == XK_Next) {
      write(gui->pipe_fd, "\x1b[6~", 4);
    } else if (keysym >= XK_F1 && keysym <= XK_F12) {
      const char *fkeys[] = {
        "\x1bOP",   "\x1bOQ",   "\x1bOR",   "\x1bOS",
        "\x1b[15~", "\x1b[17~", "\x1b[18~", "\x1b[19~",
        "\x1b[20~", "\x1b[21~", "\x1b[23~", "\x1b[24~",
      };
      const char *seq = fkeys[keysym - XK_F1];
      write(gui->pipe_fd, seq, strlen(seq));
    } else {
      int len = XLookupString(&event->xkey, buffer, sizeof(buffer), NULL, NULL);
      if (len > 0) {
        if (event->xkey.state & Mod1Mask) {
          char alt_buf[33];
          alt_buf[0] = '\x1b';
          memcpy(&alt_buf[1], buffer, len);
          write(gui->pipe_fd, alt_buf, len + 1);
        } else {
          write(gui->pipe_fd, buffer, len);
        }
      }
    }
    break;
  }
  case ButtonPress: {
    Term_Screen *scr = terminal->using_alt_screen
                           ? &terminal->alt_screen : &terminal->screen;
    int max_scroll = scr->scrollback.count;
    if (event->xbutton.button == Button1) {
      int cell_x = (event->xbutton.x - 10) / gui->char_width;
      int cell_y = (event->xbutton.y - 10) / (gui->char_height + LINE_GAP);
      if (cell_x < 0) cell_x = 0;
      if (cell_x >= terminal->width) cell_x = terminal->width - 1;
      if (cell_y < 0) cell_y = 0;
      if (cell_y >= terminal->height) cell_y = terminal->height - 1;
      gui->selecting = true;
      gui->has_selection = false;
      gui->sel_anchor_x = cell_x;
      gui->sel_anchor_y = cell_y;
      gui->sel_cur_x = cell_x;
      gui->sel_cur_y = cell_y;
      draw_terminal(gui, terminal);
    } else if (event->xbutton.button == Button2) {
      XConvertSelection(gui->display, XA_PRIMARY, gui->atom_utf8_string,
                        gui->atom_xsel_data, gui->window, CurrentTime);
    } else if (event->xbutton.button == Button4) {
      scr->scroll_offset += 3;
      if (scr->scroll_offset > max_scroll) scr->scroll_offset = max_scroll;
      draw_terminal(gui, terminal);
    } else if (event->xbutton.button == Button5) {
      scr->scroll_offset -= 3;
      if (scr->scroll_offset < 0) scr->scroll_offset = 0;
      draw_terminal(gui, terminal);
    }
    break;
  }
  case ButtonRelease: {
    if (event->xbutton.button == Button1 && gui->selecting) {
      int cell_x = (event->xbutton.x - 10) / gui->char_width;
      int cell_y = (event->xbutton.y - 10) / (gui->char_height + LINE_GAP);
      if (cell_x < 0) cell_x = 0;
      if (cell_x >= terminal->width) cell_x = terminal->width - 1;
      if (cell_y < 0) cell_y = 0;
      if (cell_y >= terminal->height) cell_y = terminal->height - 1;
      gui->sel_cur_x = cell_x;
      gui->sel_cur_y = cell_y;
      gui->selecting = false;
      gui->has_selection = (gui->sel_anchor_x != cell_x || gui->sel_anchor_y != cell_y);
      if (gui->has_selection) {
        build_selection_text(gui, terminal);
        XSetSelectionOwner(gui->display, XA_PRIMARY, gui->window, CurrentTime);
      }
      draw_terminal(gui, terminal);
    }
    break;
  }
  case MotionNotify: {
    if (gui->selecting) {
      int cell_x = (event->xmotion.x - 10) / gui->char_width;
      int cell_y = (event->xmotion.y - 10) / (gui->char_height + LINE_GAP);
      if (cell_x < 0) cell_x = 0;
      if (cell_x >= terminal->width) cell_x = terminal->width - 1;
      if (cell_y < 0) cell_y = 0;
      if (cell_y >= terminal->height) cell_y = terminal->height - 1;
      gui->sel_cur_x = cell_x;
      gui->sel_cur_y = cell_y;
      gui->has_selection = true;
      build_selection_text(gui, terminal);
      draw_terminal(gui, terminal);
    }
    break;
  }
  case SelectionRequest: {
    XSelectionRequestEvent *req = &event->xselectionrequest;
    XSelectionEvent reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = SelectionNotify;
    reply.display = req->display;
    reply.requestor = req->requestor;
    reply.selection = req->selection;
    reply.target = req->target;
    reply.property = None;
    reply.time = req->time;
    if (gui->has_selection && gui->selection_text &&
        (req->target == gui->atom_utf8_string || req->target == XA_STRING)) {
      XChangeProperty(req->display, req->requestor, req->property,
                      req->target, 8, PropModeReplace,
                      (unsigned char *)gui->selection_text, gui->selection_len);
      reply.property = req->property;
    }
    XSendEvent(req->display, req->requestor, False, 0, (XEvent *)&reply);
    break;
  }
  case SelectionNotify: {
    if (event->xselection.property == None) break;
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    XGetWindowProperty(gui->display, gui->window, gui->atom_xsel_data,
                       0, 65536, True, AnyPropertyType,
                       &actual_type, &actual_format, &nitems, &bytes_after, &data);
    if (data) {
      write(gui->pipe_fd, data, nitems);
      XFree(data);
    }
    break;
  }
  }
}

void cleanup_gui(GuiContext *gui) {
  LOG_INFO_MSG("Cleaning up GUI resources");

  if (gui->pipe_fd >= 0) {
    close(gui->pipe_fd);
  }
  if (gui->input_fd >= 0) {
    close(gui->input_fd);
  }
  if (gui->child_pid > 0) {
    LOG_INFO_MSG("Terminating shell subprocess PID %d", gui->child_pid);
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
  XftColorFree(gui->display, visual, colormap, &gui->xft_default_fg);
  XftColorFree(gui->display, visual, colormap, &gui->xft_default_bg);

  free(gui->selection_text);

  XftDrawDestroy(gui->xft_draw);
  XftFontClose(gui->display, gui->font);
  if (gui->font_bold != gui->font) {
    XftFontClose(gui->display, gui->font_bold);
  }
  XFreePixmap(gui->display, gui->backbuffer);
  XFreeGC(gui->display, gui->gc);
  XDestroyWindow(gui->display, gui->window);
  XCloseDisplay(gui->display);
}

int main(int argc, char *argv[]) {
  Args args;
  parse_args(argc, argv, &args);

  if (args.log_file != NULL) {
    log_set_file(args.log_file);
    LOG_INFO_MSG("Logging to file: %s", args.log_file);
  } else {
    log_init(stdout);
    LOG_INFO_MSG("Logging to stdout");
  }

  GuiContext gui;
  Terminal terminal;
  XEvent event;
  int running = 1;

  if (init_gui(&gui, &args) != 0) {
    log_close();
    return 1;
  }

  int term_cols = (gui.window_width - 20) / gui.char_width;
  int term_rows = (gui.window_height - 20) / (gui.char_height + LINE_GAP);
  if (term_cols < 1) term_cols = 1;
  if (term_rows < 1) term_rows = 1;
  init_terminal(&terminal, term_cols, term_rows);
  init_shell(&gui, term_cols, term_rows);

  XMapWindow(gui.display, gui.window);

  while (running) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(gui.pipe_fd, &read_fds);
    int x11_fd = ConnectionNumber(gui.display);
    FD_SET(x11_fd, &read_fds);
    int max_fd = (gui.pipe_fd > x11_fd) ? gui.pipe_fd : x11_fd;

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 50000;

    int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

    if (activity < 0) {
      perror("select");
      break;
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed_ms = (now.tv_sec - gui.last_blink.tv_sec) * 1000 +
                      (now.tv_nsec - gui.last_blink.tv_nsec) / 1000000;
    if (elapsed_ms >= 500) {
      gui.cursor_visible = !gui.cursor_visible;
      gui.last_blink = now;
      draw_terminal(&gui, &terminal);
      XFlush(gui.display);
    }

    while (XPending(gui.display)) {
      XNextEvent(gui.display, &event);
      handle_events(&gui, &terminal, &event);
    }

    int status;
    pid_t result = waitpid(gui.child_pid, &status, WNOHANG);
    if (result != 0) {
      running = 0;
      break;
    }

    if (FD_ISSET(gui.pipe_fd, &read_fds)) {
      read_shell_output(&gui, &terminal);
      if (terminal.title_dirty) {
        XStoreName(gui.display, gui.window, terminal.window_title);
        terminal.title_dirty = false;
      }
      draw_terminal(&gui, &terminal);
      XFlush(gui.display);
    }
  }

  cleanup_gui(&gui);
  free_terminal(&terminal);
  LOG_INFO_MSG("GUI application terminated");
  log_close();
}
