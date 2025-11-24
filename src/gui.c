#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "events.h"
#include "gui.h"
#include "log.h"
#include "render.h"
#include "shell.h"
#include "terminal.h"

int init_gui(GuiContext *gui, Args *args) {
  int font_size = args->font_size;
  LOG_INFO_MSG("Initializing GUI with font size %d", font_size);

  gui->x11.display = XOpenDisplay(NULL);
  if (gui->x11.display == NULL) {
    fprintf(stderr, "Cannot open display\n");
    LOG_ERROR_MSG("Cannot open X11 display");
    return 1;
  }

  gui->x11.screen = DefaultScreen(gui->x11.display);
  gui->surface.alpha = args->alpha;

  // Select visual: use ARGB (32-bit) when transparency is requested
  Visual *visual = DefaultVisual(gui->x11.display, gui->x11.screen);
  Colormap colormap = DefaultColormap(gui->x11.display, gui->x11.screen);
  int depth = DefaultDepth(gui->x11.display, gui->x11.screen);
  gui->x11.owns_colormap = false;

  if (args->alpha < 255) {
    XVisualInfo vinfo;
    if (XMatchVisualInfo(gui->x11.display, gui->x11.screen, 32, TrueColor, &vinfo)) {
      visual = vinfo.visual;
      colormap =
          XCreateColormap(gui->x11.display, RootWindow(gui->x11.display, gui->x11.screen),
                          visual, AllocNone);
      depth = 32;
      gui->x11.owns_colormap = true;
    }
  }
  gui->x11.visual = visual;
  gui->x11.colormap = colormap;

  gui->color.black = BlackPixel(gui->x11.display, gui->x11.screen);
  gui->color.white = WhitePixel(gui->x11.display, gui->x11.screen);

  gui->selection.atom_clipboard = XInternAtom(gui->x11.display, "CLIPBOARD", False);
  gui->selection.atom_utf8_string = XInternAtom(gui->x11.display, "UTF8_STRING", False);
  gui->selection.atom_xsel_data = XInternAtom(gui->x11.display, "XSEL_DATA", False);
  gui->selection.selecting = false;
  gui->selection.has_selection = false;
  gui->selection.selection_text = NULL;
  gui->selection.selection_len = 0;

  init_colors(gui, args);

  gui->surface.margin = args->margin;
  gui->surface.window_width = 800;
  gui->surface.window_height = 600;
  XSetWindowAttributes xwa;
  xwa.colormap = colormap;
  xwa.background_pixel = 0;
  xwa.border_pixel = 0;
  gui->x11.window = XCreateWindow(
      gui->x11.display, RootWindow(gui->x11.display, gui->x11.screen), 100, 100,
      gui->surface.window_width, gui->surface.window_height, 0, depth, InputOutput, visual,
      CWColormap | CWBackPixel | CWBorderPixel, &xwa);

  XSelectInput(gui->x11.display, gui->x11.window,
               ExposureMask | KeyPressMask | ButtonPressMask |
                   ButtonReleaseMask | Button1MotionMask | Button2MotionMask |
                   Button3MotionMask | PointerMotionMask | StructureNotifyMask);

  XStoreName(gui->x11.display, gui->x11.window,
             args->title ? args->title : "Terminal GUI");

  gui->x11.gc = XCreateGC(gui->x11.display, gui->x11.window, 0, NULL);
  XSetForeground(gui->x11.display, gui->x11.gc, gui->color.white);
  XSetBackground(gui->x11.display, gui->x11.gc, gui->color.black);

  char font_pattern[1024];
  char regular_base[256] = "";
  char bold_base[256] = "";
  char italic_base[256] = "";
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
    char *size_pos = strstr(args->font, ":size=");
    if (size_pos) {
      int base_len = (int)(size_pos - args->font);
      memcpy(regular_base, args->font, base_len);
      regular_base[base_len] = '\0';
      snprintf(font_pattern, sizeof(font_pattern), "%s", args->font);
    } else {
      snprintf(regular_base, sizeof(regular_base), "%s", args->font);
      snprintf(font_pattern, sizeof(font_pattern), "%s:size=%d", args->font,
               font_size);
    }
    gui->fonts.font = XftFontOpenName(gui->x11.display, gui->x11.screen, font_pattern);
    if (!gui->fonts.font) {
      fprintf(stderr, "Cannot load font '%s'\n", args->font);
      LOG_ERROR_MSG("Cannot load configured font: %s", args->font);
      XCloseDisplay(gui->x11.display);
      return 1;
    }
    gui->fonts.font_bold = gui->fonts.font;
  } else {
    snprintf(font_pattern, sizeof(font_pattern),
             "Iosevka Nerd Font Mono:size=%d", font_size);
    gui->fonts.font = XftFontOpenName(gui->x11.display, gui->x11.screen, font_pattern);
    if (gui->fonts.font) {
      snprintf(regular_base, sizeof(regular_base), "Iosevka Nerd Font Mono");
    } else {
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
      snprintf(bundled_font, sizeof(bundled_font), "%s/assets/FreeMono.otf",
               repo_dir);
      FcConfigAppFontAddFile(NULL, (const FcChar8 *)bundled_font);
      snprintf(bundled_font, sizeof(bundled_font), "%s/assets/FreeMonoBold.otf",
               repo_dir);
      FcConfigAppFontAddFile(NULL, (const FcChar8 *)bundled_font);
      snprintf(font_pattern, sizeof(font_pattern), "FreeMono:size=%d",
               font_size);
      gui->fonts.font = XftFontOpenName(gui->x11.display, gui->x11.screen, font_pattern);
      if (gui->fonts.font)
        snprintf(regular_base, sizeof(regular_base), "FreeMono");
    }
    if (!gui->fonts.font) {
      LOG_WARNING_MSG("Cannot load FreeMono font, trying monospace fallback");
      snprintf(font_pattern, sizeof(font_pattern), "monospace:size=%d",
               font_size);
      gui->fonts.font = XftFontOpenName(gui->x11.display, gui->x11.screen, font_pattern);
      if (gui->fonts.font)
        snprintf(regular_base, sizeof(regular_base), "monospace");
    }
    if (!gui->fonts.font) {
      fprintf(stderr, "Cannot load any suitable font\n");
      LOG_ERROR_MSG("Cannot load any suitable font");
      XCloseDisplay(gui->x11.display);
      return 1;
    }

    snprintf(font_pattern, sizeof(font_pattern),
             "Iosevka Nerd Font Mono:weight=bold:size=%d", font_size);
    gui->fonts.font_bold = XftFontOpenName(gui->x11.display, gui->x11.screen, font_pattern);
    if (gui->fonts.font_bold) {
      snprintf(bold_base, sizeof(bold_base),
               "Iosevka Nerd Font Mono:weight=bold");
    } else {
      snprintf(font_pattern, sizeof(font_pattern),
               "monospace:weight=bold:size=%d", font_size);
      gui->fonts.font_bold = XftFontOpenName(gui->x11.display, gui->x11.screen, font_pattern);
      if (gui->fonts.font_bold)
        snprintf(bold_base, sizeof(bold_base), "monospace:weight=bold");
    }
    if (!gui->fonts.font_bold)
      gui->fonts.font_bold = gui->fonts.font;
  }
  if (regular_base[0]) {
    snprintf(font_pattern, sizeof(font_pattern), "%s:slant=italic:size=%d",
             regular_base, font_size);
    gui->fonts.font_italic = XftFontOpenName(gui->x11.display, gui->x11.screen, font_pattern);
    if (gui->fonts.font_italic)
      snprintf(italic_base, sizeof(italic_base), "%s:slant=italic",
               regular_base);
  }
  if (!gui->fonts.font_italic)
    gui->fonts.font_italic = gui->fonts.font;

  LOG_INFO_MSG("Loaded font: %s", font_pattern);

  gui->fonts.font_size = font_size;
  snprintf(gui->fonts.font_base, sizeof(gui->fonts.font_base), "%s", regular_base);
  snprintf(gui->fonts.font_bold_base, sizeof(gui->fonts.font_bold_base), "%s", bold_base);
  snprintf(gui->fonts.font_italic_base, sizeof(gui->fonts.font_italic_base), "%s",
           italic_base);

  gui->fonts.char_width = gui->fonts.font->max_advance_width;
  if (gui->fonts.font_bold && gui->fonts.font_bold != gui->fonts.font &&
      gui->fonts.font_bold->max_advance_width > gui->fonts.char_width)
    gui->fonts.char_width = gui->fonts.font_bold->max_advance_width;
  gui->fonts.char_height = gui->fonts.font->ascent + gui->fonts.font->descent;
  gui->fonts.char_ascent = gui->fonts.font->ascent;

  if (args->cols > 0)
    gui->surface.window_width = args->cols * gui->fonts.char_width + 2 * gui->surface.margin;
  if (args->rows > 0)
    gui->surface.window_height = args->rows * gui->fonts.char_height + 2 * gui->surface.margin;
  if (args->cols > 0 || args->rows > 0)
    XResizeWindow(gui->x11.display, gui->x11.window, gui->surface.window_width,
                  gui->surface.window_height);

  gui->surface.backbuffer = XCreatePixmap(gui->x11.display, gui->x11.window, gui->surface.window_width,
                                  gui->surface.window_height, depth);

  gui->color.xft_draw =
      XftDrawCreate(gui->x11.display, gui->surface.backbuffer, visual, colormap);

  gui->surface.backbuffer_picture = None;
  if (args->alpha < 255) {
    XRenderPictFormat *fmt = XRenderFindVisualFormat(gui->x11.display, visual);
    if (fmt)
      gui->surface.backbuffer_picture =
          XRenderCreatePicture(gui->x11.display, gui->surface.backbuffer, fmt, 0, NULL);
  }

  gui->cursor.cursor_visible = true;
  clock_gettime(CLOCK_MONOTONIC, &gui->cursor.last_blink);
  gui->bell.bell_flash = false;
  gui->click.last_click_time.tv_sec = 0;
  gui->click.last_click_time.tv_nsec = 0;
  gui->click.last_click_x = -1;
  gui->click.last_click_y = -1;

  memset(gui->color.xft_color_cached, 0, sizeof(gui->color.xft_color_cached));
  memset(gui->color.rgb_cache_valid, 0, sizeof(gui->color.rgb_cache_valid));
  gui->color.rgb_cache_next = 0;

  return 0;
}

void change_font_size(GuiContext *gui, Terminal *terminal, int delta) {
  int new_size = gui->fonts.font_size + delta;
  if (new_size < 6 || new_size > 72)
    return;

  bool bold_separate = (gui->fonts.font_bold != gui->fonts.font);
  bool italic_separate =
      (gui->fonts.font_italic != gui->fonts.font && gui->fonts.font_italic != gui->fonts.font_bold);
  XftFontClose(gui->x11.display, gui->fonts.font);
  if (bold_separate)
    XftFontClose(gui->x11.display, gui->fonts.font_bold);
  if (italic_separate)
    XftFontClose(gui->x11.display, gui->fonts.font_italic);

  char pattern[512];
  snprintf(pattern, sizeof(pattern), "%s:size=%d", gui->fonts.font_base, new_size);
  gui->fonts.font = XftFontOpenName(gui->x11.display, gui->x11.screen, pattern);
  if (!gui->fonts.font) {
    snprintf(pattern, sizeof(pattern), "%s:size=%d", gui->fonts.font_base,
             gui->fonts.font_size);
    gui->fonts.font = XftFontOpenName(gui->x11.display, gui->x11.screen, pattern);
    if (!gui->fonts.font)
      return;
    if (gui->fonts.font_bold_base[0]) {
      snprintf(pattern, sizeof(pattern), "%s:size=%d", gui->fonts.font_bold_base,
               gui->fonts.font_size);
      gui->fonts.font_bold = XftFontOpenName(gui->x11.display, gui->x11.screen, pattern);
      if (!gui->fonts.font_bold)
        gui->fonts.font_bold = gui->fonts.font;
    } else {
      gui->fonts.font_bold = gui->fonts.font;
    }
    if (gui->fonts.font_italic_base[0]) {
      snprintf(pattern, sizeof(pattern), "%s:size=%d", gui->fonts.font_italic_base,
               gui->fonts.font_size);
      gui->fonts.font_italic = XftFontOpenName(gui->x11.display, gui->x11.screen, pattern);
      if (!gui->fonts.font_italic)
        gui->fonts.font_italic = gui->fonts.font;
    } else {
      gui->fonts.font_italic = gui->fonts.font;
    }
    return;
  }

  if (gui->fonts.font_bold_base[0]) {
    snprintf(pattern, sizeof(pattern), "%s:size=%d", gui->fonts.font_bold_base,
             new_size);
    gui->fonts.font_bold = XftFontOpenName(gui->x11.display, gui->x11.screen, pattern);
    if (!gui->fonts.font_bold)
      gui->fonts.font_bold = gui->fonts.font;
  } else {
    gui->fonts.font_bold = gui->fonts.font;
  }

  if (gui->fonts.font_italic_base[0]) {
    snprintf(pattern, sizeof(pattern), "%s:size=%d", gui->fonts.font_italic_base,
             new_size);
    gui->fonts.font_italic = XftFontOpenName(gui->x11.display, gui->x11.screen, pattern);
    if (!gui->fonts.font_italic)
      gui->fonts.font_italic = gui->fonts.font;
  } else {
    gui->fonts.font_italic = gui->fonts.font;
  }

  gui->fonts.font_size = new_size;
  gui->fonts.char_width = gui->fonts.font->max_advance_width;
  if (gui->fonts.font_bold != gui->fonts.font &&
      gui->fonts.font_bold->max_advance_width > gui->fonts.char_width)
    gui->fonts.char_width = gui->fonts.font_bold->max_advance_width;
  gui->fonts.char_height = gui->fonts.font->ascent + gui->fonts.font->descent;
  gui->fonts.char_ascent = gui->fonts.font->ascent;

  int term_cols = (gui->surface.window_width - 2 * gui->surface.margin) / gui->fonts.char_width;
  int term_rows = (gui->surface.window_height - 2 * gui->surface.margin) / gui->fonts.char_height;
  if (term_cols < 1)
    term_cols = 1;
  if (term_rows < 1)
    term_rows = 1;

  resize_terminal(terminal, term_cols, term_rows);

  struct winsize ws = {
      .ws_row = term_rows,
      .ws_col = term_cols,
      .ws_xpixel = 0,
      .ws_ypixel = 0,
  };
  ioctl(gui->process.pipe_fd, TIOCSWINSZ, &ws);
  kill(gui->process.child_pid, SIGWINCH);

  draw_terminal(gui, terminal);
}

void cleanup_gui(GuiContext *gui) {
  LOG_INFO_MSG("Cleaning up GUI resources");

  if (gui->process.pipe_fd >= 0) {
    close(gui->process.pipe_fd);
  }
  if (gui->process.input_fd >= 0) {
    close(gui->process.input_fd);
  }
  if (gui->process.child_pid > 0) {
    LOG_INFO_MSG("Terminating shell subprocess PID %d", gui->process.child_pid);
    kill(gui->process.child_pid, SIGTERM);
    waitpid(gui->process.child_pid, NULL, 0);
  }

  Visual *visual = gui->x11.visual;
  Colormap colormap = gui->x11.colormap;
  for (int i = 0; i < 16; i++) {
    XftColorFree(gui->x11.display, visual, colormap, &gui->color.xft_colors[i]);
  }
  XftColorFree(gui->x11.display, visual, colormap, &gui->color.xft_white);
  XftColorFree(gui->x11.display, visual, colormap, &gui->color.xft_black);
  XftColorFree(gui->x11.display, visual, colormap, &gui->color.xft_default_fg);
  XftColorFree(gui->x11.display, visual, colormap, &gui->color.xft_default_bg);

  for (int i = 16; i < 256; i++) {
    if (gui->color.xft_color_cached[i])
      XftColorFree(gui->x11.display, visual, colormap, &gui->color.xft_color_cache[i]);
  }
  for (int i = 0; i < 64; i++) {
    if (gui->color.rgb_cache_valid[i])
      XftColorFree(gui->x11.display, visual, colormap, &gui->color.rgb_cache[i]);
  }

  if (gui->surface.backbuffer_picture)
    XRenderFreePicture(gui->x11.display, gui->surface.backbuffer_picture);

  free(gui->selection.selection_text);

  XftDrawDestroy(gui->color.xft_draw);
  XftFontClose(gui->x11.display, gui->fonts.font);
  if (gui->fonts.font_bold != gui->fonts.font)
    XftFontClose(gui->x11.display, gui->fonts.font_bold);
  if (gui->fonts.font_italic != gui->fonts.font && gui->fonts.font_italic != gui->fonts.font_bold)
    XftFontClose(gui->x11.display, gui->fonts.font_italic);
  XFreePixmap(gui->x11.display, gui->surface.backbuffer);
  XFreeGC(gui->x11.display, gui->x11.gc);
  XDestroyWindow(gui->x11.display, gui->x11.window);
  if (gui->x11.owns_colormap)
    XFreeColormap(gui->x11.display, colormap);
  XCloseDisplay(gui->x11.display);
}

int main(int argc, char *argv[]) {
  Args args;
  parse_args(argc, argv, &args);

  if (args.log_file != NULL) {
    log_set_file(args.log_file);
    LOG_INFO_MSG("Logging to file: %s", args.log_file);
  } else {
    log_init(stderr);
    LOG_INFO_MSG("Logging to stderr");
  }

  GuiContext gui;
  Terminal terminal;
  XEvent event;
  int running = 1;

  if (init_gui(&gui, &args) != 0) {
    log_close();
    return 1;
  }

  int term_cols = (gui.surface.window_width - 2 * gui.surface.margin) / gui.fonts.char_width;
  int term_rows = (gui.surface.window_height - 2 * gui.surface.margin) / (gui.fonts.char_height);
  if (term_cols < 1)
    term_cols = 1;
  if (term_rows < 1)
    term_rows = 1;
  init_terminal(&terminal, term_cols, term_rows, args.scrollback);
  terminal.osc.default_fg_rgb = (args.fg != -1) ? (unsigned long)args.fg : 0xffffff;
  init_shell(&gui, term_cols, term_rows);

  XMapWindow(gui.x11.display, gui.x11.window);

  while (running) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(gui.process.pipe_fd, &read_fds);
    int x11_fd = ConnectionNumber(gui.x11.display);
    FD_SET(x11_fd, &read_fds);
    int max_fd = (gui.process.pipe_fd > x11_fd) ? gui.process.pipe_fd : x11_fd;

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
    long elapsed_ms = (now.tv_sec - gui.cursor.last_blink.tv_sec) * 1000 +
                      (now.tv_nsec - gui.cursor.last_blink.tv_nsec) / 1000000;
    bool shape_steady = terminal.modes.cursor_shape % 2 == 0 && terminal.modes.cursor_shape != 0;
    bool steady = shape_steady || !terminal.modes.cursor_blink;
    if (elapsed_ms >= 500) {
      gui.cursor.cursor_visible = steady ? true : !gui.cursor.cursor_visible;
      gui.cursor.last_blink = now;
      draw_terminal(&gui, &terminal);
      XFlush(gui.x11.display);
    }

    if (gui.bell.bell_flash) {
      long bell_ms = (now.tv_sec - gui.bell.bell_start.tv_sec) * 1000 +
                     (now.tv_nsec - gui.bell.bell_start.tv_nsec) / 1000000;
      if (bell_ms >= 150) {
        gui.bell.bell_flash = false;
        draw_terminal(&gui, &terminal);
        XFlush(gui.x11.display);
      }
    }

    while (XPending(gui.x11.display)) {
      XNextEvent(gui.x11.display, &event);
      handle_events(&gui, &terminal, &event);
    }

    int status;
    pid_t result = waitpid(gui.process.child_pid, &status, WNOHANG);
    if (result != 0) {
      running = 0;
      break;
    }

    if (FD_ISSET(gui.process.pipe_fd, &read_fds)) {
      read_shell_output(&gui, &terminal);
      if (terminal.screens.screen.scrolled || terminal.screens.alt_screen.scrolled) {
        gui.selection.has_selection = false;
        terminal.screens.screen.scrolled = false;
        terminal.screens.alt_screen.scrolled = false;
      }
      if (terminal.response.response_len > 0) {
        write(gui.process.pipe_fd, terminal.response.response_buf, terminal.response.response_len);
        terminal.response.response_len = 0;
      }
      if (terminal.title.title_dirty) {
        XStoreName(gui.x11.display, gui.x11.window, terminal.title.window_title);
        terminal.title.title_dirty = false;
      }
      if (terminal.osc.fg_dirty) {
        unsigned long fg_val = terminal.osc.osc_fg;
        Colormap colormap = gui.x11.colormap;
        Visual *visual = gui.x11.visual;
        XColor color;
        color.red = ((fg_val >> 16) & 0xff) << 8;
        color.green = ((fg_val >> 8) & 0xff) << 8;
        color.blue = (fg_val & 0xff) << 8;
        color.flags = DoRed | DoGreen | DoBlue;
        gui.color.default_fg = XAllocColor(gui.x11.display, colormap, &color)
                             ? color.pixel
                             : gui.color.white;
        XftColorFree(gui.x11.display, visual, colormap, &gui.color.xft_default_fg);
        XRenderColor xrender_color;
        xrender_color.red = color.red;
        xrender_color.green = color.green;
        xrender_color.blue = color.blue;
        xrender_color.alpha = 0xffff;
        XftColorAllocValue(gui.x11.display, visual, colormap, &xrender_color,
                           &gui.color.xft_default_fg);
        terminal.osc.fg_dirty = false;
      }
      if (terminal.osc.bg_dirty) {
        unsigned long bg_val = terminal.osc.osc_bg;
        Colormap colormap = gui.x11.colormap;
        Visual *visual = gui.x11.visual;
        XColor color;
        color.red = ((bg_val >> 16) & 0xff) << 8;
        color.green = ((bg_val >> 8) & 0xff) << 8;
        color.blue = (bg_val & 0xff) << 8;
        color.flags = DoRed | DoGreen | DoBlue;
        gui.color.default_bg = XAllocColor(gui.x11.display, colormap, &color)
                             ? color.pixel
                             : gui.color.black;
        XftColorFree(gui.x11.display, visual, colormap, &gui.color.xft_default_bg);
        XRenderColor xrender_color;
        xrender_color.red = color.red;
        xrender_color.green = color.green;
        xrender_color.blue = color.blue;
        xrender_color.alpha = 0xffff;
        XftColorAllocValue(gui.x11.display, visual, colormap, &xrender_color,
                           &gui.color.xft_default_bg);
        terminal.osc.bg_dirty = false;
      }
      if (terminal.modes.bell_pending) {
        gui.bell.bell_flash = true;
        gui.bell.bell_start = now;
        terminal.modes.bell_pending = false;
      }
      if (terminal.osc.osc52_dirty) {
        free(gui.selection.selection_text);
        gui.selection.selection_text = terminal.osc.osc52_text;
        gui.selection.selection_len = terminal.osc.osc52_len;
        terminal.osc.osc52_text = NULL;
        terminal.osc.osc52_len = 0;
        terminal.osc.osc52_dirty = false;
        gui.selection.has_selection = true;
        XSetSelectionOwner(gui.x11.display, gui.selection.atom_clipboard, gui.x11.window,
                           CurrentTime);
      }
      draw_terminal(&gui, &terminal);
      XFlush(gui.x11.display);
    }
  }

  cleanup_gui(&gui);
  free_terminal(&terminal);
  LOG_INFO_MSG("GUI application terminated");
  log_close();
}
