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
  gui->window = XCreateSimpleWindow(
      gui->display, RootWindow(gui->display, gui->screen), 100, 100,
      gui->window_width, gui->window_height, 1, gui->white, gui->black);

  XSelectInput(gui->display, gui->window,
               ExposureMask | KeyPressMask | ButtonPressMask |
                   ButtonReleaseMask | Button1MotionMask | Button2MotionMask |
                   Button3MotionMask | PointerMotionMask | StructureNotifyMask);

  XStoreName(gui->display, gui->window, "Terminal GUI");

  gui->gc = XCreateGC(gui->display, gui->window, 0, NULL);
  XSetForeground(gui->display, gui->gc, gui->white);
  XSetBackground(gui->display, gui->gc, gui->black);

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
    gui->font = XftFontOpenName(gui->display, gui->screen, font_pattern);
    if (!gui->font) {
      fprintf(stderr, "Cannot load font '%s'\n", args->font);
      LOG_ERROR_MSG("Cannot load configured font: %s", args->font);
      XCloseDisplay(gui->display);
      return 1;
    }
    gui->font_bold = gui->font;
  } else {
    snprintf(font_pattern, sizeof(font_pattern),
             "Iosevka Nerd Font Mono:size=%d", font_size);
    gui->font = XftFontOpenName(gui->display, gui->screen, font_pattern);
    if (gui->font) {
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
      gui->font = XftFontOpenName(gui->display, gui->screen, font_pattern);
      if (gui->font)
        snprintf(regular_base, sizeof(regular_base), "FreeMono");
    }
    if (!gui->font) {
      LOG_WARNING_MSG("Cannot load FreeMono font, trying monospace fallback");
      snprintf(font_pattern, sizeof(font_pattern), "monospace:size=%d",
               font_size);
      gui->font = XftFontOpenName(gui->display, gui->screen, font_pattern);
      if (gui->font)
        snprintf(regular_base, sizeof(regular_base), "monospace");
    }
    if (!gui->font) {
      fprintf(stderr, "Cannot load any suitable font\n");
      LOG_ERROR_MSG("Cannot load any suitable font");
      XCloseDisplay(gui->display);
      return 1;
    }

    snprintf(font_pattern, sizeof(font_pattern),
             "Iosevka Nerd Font Mono:weight=bold:size=%d", font_size);
    gui->font_bold = XftFontOpenName(gui->display, gui->screen, font_pattern);
    if (gui->font_bold) {
      snprintf(bold_base, sizeof(bold_base),
               "Iosevka Nerd Font Mono:weight=bold");
    } else {
      snprintf(font_pattern, sizeof(font_pattern),
               "monospace:weight=bold:size=%d", font_size);
      gui->font_bold = XftFontOpenName(gui->display, gui->screen, font_pattern);
      if (gui->font_bold)
        snprintf(bold_base, sizeof(bold_base), "monospace:weight=bold");
    }
    if (!gui->font_bold)
      gui->font_bold = gui->font;
  }
  if (regular_base[0]) {
    snprintf(font_pattern, sizeof(font_pattern), "%s:slant=italic:size=%d",
             regular_base, font_size);
    gui->font_italic = XftFontOpenName(gui->display, gui->screen, font_pattern);
    if (gui->font_italic)
      snprintf(italic_base, sizeof(italic_base), "%s:slant=italic", regular_base);
  }
  if (!gui->font_italic)
    gui->font_italic = gui->font;

  LOG_INFO_MSG("Loaded font: %s", font_pattern);

  gui->font_size = font_size;
  snprintf(gui->font_base, sizeof(gui->font_base), "%s", regular_base);
  snprintf(gui->font_bold_base, sizeof(gui->font_bold_base), "%s", bold_base);
  snprintf(gui->font_italic_base, sizeof(gui->font_italic_base), "%s", italic_base);

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
  gui->bell_flash = false;
  gui->last_click_time.tv_sec = 0;
  gui->last_click_time.tv_nsec = 0;
  gui->last_click_x = -1;
  gui->last_click_y = -1;

  memset(gui->xft_color_cached, 0, sizeof(gui->xft_color_cached));
  memset(gui->rgb_cache_valid, 0, sizeof(gui->rgb_cache_valid));
  gui->rgb_cache_next = 0;
  gui->margin = args->margin;

  return 0;
}

void change_font_size(GuiContext *gui, Terminal *terminal, int delta) {
  int new_size = gui->font_size + delta;
  if (new_size < 6 || new_size > 72)
    return;

  bool bold_separate = (gui->font_bold != gui->font);
  bool italic_separate = (gui->font_italic != gui->font &&
                          gui->font_italic != gui->font_bold);
  XftFontClose(gui->display, gui->font);
  if (bold_separate)
    XftFontClose(gui->display, gui->font_bold);
  if (italic_separate)
    XftFontClose(gui->display, gui->font_italic);

  char pattern[512];
  snprintf(pattern, sizeof(pattern), "%s:size=%d", gui->font_base, new_size);
  gui->font = XftFontOpenName(gui->display, gui->screen, pattern);
  if (!gui->font) {
    snprintf(pattern, sizeof(pattern), "%s:size=%d", gui->font_base,
             gui->font_size);
    gui->font = XftFontOpenName(gui->display, gui->screen, pattern);
    if (!gui->font)
      return;
    if (gui->font_bold_base[0]) {
      snprintf(pattern, sizeof(pattern), "%s:size=%d", gui->font_bold_base,
               gui->font_size);
      gui->font_bold = XftFontOpenName(gui->display, gui->screen, pattern);
      if (!gui->font_bold)
        gui->font_bold = gui->font;
    } else {
      gui->font_bold = gui->font;
    }
    if (gui->font_italic_base[0]) {
      snprintf(pattern, sizeof(pattern), "%s:size=%d", gui->font_italic_base,
               gui->font_size);
      gui->font_italic = XftFontOpenName(gui->display, gui->screen, pattern);
      if (!gui->font_italic)
        gui->font_italic = gui->font;
    } else {
      gui->font_italic = gui->font;
    }
    return;
  }

  if (gui->font_bold_base[0]) {
    snprintf(pattern, sizeof(pattern), "%s:size=%d", gui->font_bold_base,
             new_size);
    gui->font_bold = XftFontOpenName(gui->display, gui->screen, pattern);
    if (!gui->font_bold)
      gui->font_bold = gui->font;
  } else {
    gui->font_bold = gui->font;
  }

  if (gui->font_italic_base[0]) {
    snprintf(pattern, sizeof(pattern), "%s:size=%d", gui->font_italic_base,
             new_size);
    gui->font_italic = XftFontOpenName(gui->display, gui->screen, pattern);
    if (!gui->font_italic)
      gui->font_italic = gui->font;
  } else {
    gui->font_italic = gui->font;
  }

  gui->font_size = new_size;
  gui->char_width = gui->font->max_advance_width;
  if (gui->font_bold != gui->font &&
      gui->font_bold->max_advance_width > gui->char_width)
    gui->char_width = gui->font_bold->max_advance_width;
  gui->char_height = gui->font->ascent + gui->font->descent;
  gui->char_ascent = gui->font->ascent;

  int term_cols = (gui->window_width - 2 * gui->margin) / gui->char_width;
  int term_rows = (gui->window_height - 2 * gui->margin) / gui->char_height;
  if (term_cols < 1)
    term_cols = 1;
  if (term_rows < 1)
    term_rows = 1;

  resize_terminal(terminal, term_cols, term_rows);

  struct winsize ws = {
      .ws_row = term_rows, .ws_col = term_cols,
      .ws_xpixel = 0,      .ws_ypixel = 0,
  };
  ioctl(gui->pipe_fd, TIOCSWINSZ, &ws);
  kill(gui->child_pid, SIGWINCH);

  draw_terminal(gui, terminal);
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

  for (int i = 16; i < 256; i++) {
    if (gui->xft_color_cached[i])
      XftColorFree(gui->display, visual, colormap, &gui->xft_color_cache[i]);
  }
  for (int i = 0; i < 64; i++) {
    if (gui->rgb_cache_valid[i])
      XftColorFree(gui->display, visual, colormap, &gui->rgb_cache[i]);
  }

  free(gui->selection_text);

  XftDrawDestroy(gui->xft_draw);
  XftFontClose(gui->display, gui->font);
  if (gui->font_bold != gui->font)
    XftFontClose(gui->display, gui->font_bold);
  if (gui->font_italic != gui->font && gui->font_italic != gui->font_bold)
    XftFontClose(gui->display, gui->font_italic);
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

  int term_cols = (gui.window_width - 2 * gui.margin) / gui.char_width;
  int term_rows = (gui.window_height - 2 * gui.margin) / (gui.char_height);
  if (term_cols < 1)
    term_cols = 1;
  if (term_rows < 1)
    term_rows = 1;
  init_terminal(&terminal, term_cols, term_rows, args.scrollback);
  terminal.default_fg_rgb = (args.fg != -1) ? (unsigned long)args.fg : 0xffffff;
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
    bool steady = terminal.cursor_shape % 2 == 0 && terminal.cursor_shape != 0;
    if (elapsed_ms >= 500) {
      gui.cursor_visible = steady ? true : !gui.cursor_visible;
      gui.last_blink = now;
      draw_terminal(&gui, &terminal);
      XFlush(gui.display);
    }

    if (gui.bell_flash) {
      long bell_ms = (now.tv_sec - gui.bell_start.tv_sec) * 1000 +
                     (now.tv_nsec - gui.bell_start.tv_nsec) / 1000000;
      if (bell_ms >= 150) {
        gui.bell_flash = false;
        draw_terminal(&gui, &terminal);
        XFlush(gui.display);
      }
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
      if (terminal.screen.scrolled || terminal.alt_screen.scrolled) {
        gui.has_selection = false;
        terminal.screen.scrolled = false;
        terminal.alt_screen.scrolled = false;
      }
      if (terminal.response_len > 0) {
        write(gui.pipe_fd, terminal.response_buf, terminal.response_len);
        terminal.response_len = 0;
      }
      if (terminal.title_dirty) {
        XStoreName(gui.display, gui.window, terminal.window_title);
        terminal.title_dirty = false;
      }
      if (terminal.fg_dirty) {
        unsigned long fg_val = terminal.osc_fg;
        Colormap colormap = DefaultColormap(gui.display, gui.screen);
        Visual *visual = DefaultVisual(gui.display, gui.screen);
        XColor color;
        color.red = ((fg_val >> 16) & 0xff) << 8;
        color.green = ((fg_val >> 8) & 0xff) << 8;
        color.blue = (fg_val & 0xff) << 8;
        color.flags = DoRed | DoGreen | DoBlue;
        gui.default_fg =
            XAllocColor(gui.display, colormap, &color) ? color.pixel : gui.white;
        XftColorFree(gui.display, visual, colormap, &gui.xft_default_fg);
        XRenderColor xrender_color;
        xrender_color.red = color.red;
        xrender_color.green = color.green;
        xrender_color.blue = color.blue;
        xrender_color.alpha = 0xffff;
        XftColorAllocValue(gui.display, visual, colormap, &xrender_color,
                           &gui.xft_default_fg);
        terminal.fg_dirty = false;
      }
      if (terminal.bg_dirty) {
        unsigned long bg_val = terminal.osc_bg;
        Colormap colormap = DefaultColormap(gui.display, gui.screen);
        Visual *visual = DefaultVisual(gui.display, gui.screen);
        XColor color;
        color.red = ((bg_val >> 16) & 0xff) << 8;
        color.green = ((bg_val >> 8) & 0xff) << 8;
        color.blue = (bg_val & 0xff) << 8;
        color.flags = DoRed | DoGreen | DoBlue;
        gui.default_bg =
            XAllocColor(gui.display, colormap, &color) ? color.pixel : gui.black;
        XftColorFree(gui.display, visual, colormap, &gui.xft_default_bg);
        XRenderColor xrender_color;
        xrender_color.red = color.red;
        xrender_color.green = color.green;
        xrender_color.blue = color.blue;
        xrender_color.alpha = 0xffff;
        XftColorAllocValue(gui.display, visual, colormap, &xrender_color,
                           &gui.xft_default_bg);
        terminal.bg_dirty = false;
      }
      if (terminal.bell_pending) {
        gui.bell_flash = true;
        gui.bell_start = now;
        terminal.bell_pending = false;
      }
      if (terminal.osc52_dirty) {
        free(gui.selection_text);
        gui.selection_text = terminal.osc52_text;
        gui.selection_len = terminal.osc52_len;
        terminal.osc52_text = NULL;
        terminal.osc52_len = 0;
        terminal.osc52_dirty = false;
        gui.has_selection = true;
        XSetSelectionOwner(gui.display, gui.atom_clipboard, gui.window,
                           CurrentTime);
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
