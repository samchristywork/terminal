#ifndef GUI_H
#define GUI_H

#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>
#include <stdbool.h>
#include <sys/types.h>
#include <time.h>

#include "args.h"
#include "terminal.h"

#define SEARCH_MAX_MATCHES 4096

typedef struct {
  Display *display;
  Window window;
  GC gc;
  int screen;
  Visual *visual;
  Colormap colormap;
  bool owns_colormap;
} GuiX11;

typedef struct {
  XftFont *font;
  XftFont *font_bold;
  XftFont *font_italic;
  int font_size;
  char font_base[256];
  char font_bold_base[256];
  char font_italic_base[256];
  int char_width, char_height;
  int char_ascent;
} GuiFonts;

typedef struct {
  XftDraw *xft_draw;
  XftColor xft_colors[16];
  XftColor xft_white;
  XftColor xft_black;
  XftColor xft_default_fg;
  XftColor xft_default_bg;
  unsigned long black, white;
  unsigned long colors[16];
  unsigned long default_fg;
  unsigned long default_bg;
  XftColor xft_color_cache[256];
  bool xft_color_cached[256];
  XftColor rgb_cache[64];
  int rgb_cache_keys[64];
  bool rgb_cache_valid[64];
  int rgb_cache_next;
} GuiColor;

typedef struct {
  Pixmap backbuffer;
  Picture backbuffer_picture;
  int window_width, window_height;
  int margin;
  int alpha;
} GuiSurface;

typedef struct {
  int pipe_fd;
  int input_fd;
  pid_t child_pid;
} GuiProcess;

typedef struct {
  Atom atom_clipboard;
  Atom atom_utf8_string;
  Atom atom_xsel_data;
  bool selecting;
  bool has_selection;
  int sel_anchor_x, sel_anchor_y;
  int sel_cur_x, sel_cur_y;
  char *selection_text;
  int selection_len;
} GuiSelection;

typedef struct {
  bool cursor_visible;
  struct timespec last_blink;
} GuiCursor;

typedef struct {
  bool bell_flash;
  struct timespec bell_start;
} GuiBell;

typedef struct {
  struct timespec last_click_time;
  int last_click_x;
  int last_click_y;
} GuiClick;

typedef struct {
  bool search_active;
  char search_query[256];
  int search_query_len;
  int search_rows[SEARCH_MAX_MATCHES];
  int search_start_cols[SEARCH_MAX_MATCHES];
  int search_end_cols[SEARCH_MAX_MATCHES];
  int search_match_count;
  int search_current;
} GuiSearch;

typedef struct {
  GuiX11 x11;
  GuiFonts fonts;
  GuiColor color;
  GuiSurface surface;
  GuiProcess process;
  GuiSelection selection;
  GuiCursor cursor;
  GuiBell bell;
  GuiClick click;
  GuiSearch search;
} GuiContext;

int init_gui(GuiContext *gui, Args *args);
void cleanup_gui(GuiContext *gui);
void change_font_size(GuiContext *gui, Terminal *terminal, int delta);

#endif
