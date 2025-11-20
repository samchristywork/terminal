#ifndef GUI_H
#define GUI_H

#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <time.h>

#include "args.h"
#include "terminal.h"

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
  XftColor xft_color_cache[256];
  bool xft_color_cached[256];
  XftColor rgb_cache[64];
  int rgb_cache_keys[64];
  bool rgb_cache_valid[64];
  int rgb_cache_next;
  int font_size;
  char font_base[256];
  char font_bold_base[256];
  struct timespec last_click_time;
  int last_click_x;
  int last_click_y;
} GuiContext;

int init_gui(GuiContext *gui, Args *args);
void cleanup_gui(GuiContext *gui);
void change_font_size(GuiContext *gui, Terminal *terminal, int delta);

#endif
