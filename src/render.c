#include <stdlib.h>
#include <string.h>

#include "render.h"

void init_colors(GuiContext *gui, Args *args) {
  Colormap colormap = gui->x11.colormap;
  XColor color;
  XRenderColor xrender_color;
  Visual *visual = gui->x11.visual;

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

    if (XAllocColor(gui->x11.display, colormap, &color)) {
      gui->color.colors[i] = color.pixel;
    } else {
      gui->color.colors[i] = (i < 8) ? gui->color.black : gui->color.white;
    }

    xrender_color.red = color.red;
    xrender_color.green = color.green;
    xrender_color.blue = color.blue;
    xrender_color.alpha = 0xffff;
    XftColorAllocValue(gui->x11.display, visual, colormap, &xrender_color,
                       &gui->color.xft_colors[i]);
  }

  xrender_color.red = 0xffff;
  xrender_color.green = 0xffff;
  xrender_color.blue = 0xffff;
  xrender_color.alpha = 0xffff;
  XftColorAllocValue(gui->x11.display, visual, colormap, &xrender_color,
                     &gui->color.xft_white);

  xrender_color.red = 0;
  xrender_color.green = 0;
  xrender_color.blue = 0;
  xrender_color.alpha = 0xffff;
  XftColorAllocValue(gui->x11.display, visual, colormap, &xrender_color,
                     &gui->color.xft_black);

  unsigned long fg_val = (args->fg != -1) ? (unsigned long)args->fg : 0xffffff;
  unsigned long bg_val = (args->bg != -1) ? (unsigned long)args->bg : 0x000000;

  color.red = ((fg_val >> 16) & 0xff) << 8;
  color.green = ((fg_val >> 8) & 0xff) << 8;
  color.blue = (fg_val & 0xff) << 8;
  color.flags = DoRed | DoGreen | DoBlue;
  gui->color.default_fg =
      XAllocColor(gui->x11.display, colormap, &color) ? color.pixel : gui->color.white;

  color.red = ((bg_val >> 16) & 0xff) << 8;
  color.green = ((bg_val >> 8) & 0xff) << 8;
  color.blue = (bg_val & 0xff) << 8;
  color.flags = DoRed | DoGreen | DoBlue;
  gui->color.default_bg =
      XAllocColor(gui->x11.display, colormap, &color) ? color.pixel : gui->color.black;

  xrender_color.red = ((fg_val >> 16) & 0xff) << 8;
  xrender_color.green = ((fg_val >> 8) & 0xff) << 8;
  xrender_color.blue = (fg_val & 0xff) << 8;
  xrender_color.alpha = 0xffff;
  XftColorAllocValue(gui->x11.display, visual, colormap, &xrender_color,
                     &gui->color.xft_default_fg);

  xrender_color.red = ((bg_val >> 16) & 0xff) << 8;
  xrender_color.green = ((bg_val >> 8) & 0xff) << 8;
  xrender_color.blue = (bg_val & 0xff) << 8;
  xrender_color.alpha = 0xffff;
  XftColorAllocValue(gui->x11.display, visual, colormap, &xrender_color,
                     &gui->color.xft_default_bg);
}

unsigned long get_color_pixel(GuiContext *gui, Term_Color color) {
  if (color.type == COLOR_DEFAULT && color.color >= 30 && color.color <= 37) {
    return gui->color.colors[color.color - 30];
  } else if (color.type == COLOR_BRIGHT && color.color >= 90 &&
             color.color <= 97) {
    return gui->color.colors[color.color - 90 + 8];
  } else if (color.type == COLOR_DEFAULT && color.color >= 40 &&
             color.color <= 47) {
    return gui->color.colors[color.color - 40];
  } else if (color.type == COLOR_BRIGHT && color.color >= 100 &&
             color.color <= 107) {
    return gui->color.colors[color.color - 100 + 8];
  } else if (color.type == COLOR_256) {
    static const int cube_levels[6] = {0, 95, 135, 175, 215, 255};
    int idx = color.color;
    unsigned long r, g, b;

    if (idx < 16) {
      return gui->color.colors[idx];
    } else if (idx < 232) {
      idx -= 16;
      r = cube_levels[idx / 36];
      g = cube_levels[(idx / 6) % 6];
      b = cube_levels[idx % 6];
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
  return gui->color.white;
}

XftColor *get_xft_color(GuiContext *gui, Term_Color color) {
  if (color.type == COLOR_DEFAULT && color.color >= 30 && color.color <= 37) {
    return &gui->color.xft_colors[color.color - 30];
  } else if (color.type == COLOR_BRIGHT && color.color >= 90 &&
             color.color <= 97) {
    return &gui->color.xft_colors[color.color - 90 + 8];
  } else if (color.type == COLOR_DEFAULT && color.color >= 40 &&
             color.color <= 47) {
    return &gui->color.xft_colors[color.color - 40];
  } else if (color.type == COLOR_BRIGHT && color.color >= 100 &&
             color.color <= 107) {
    return &gui->color.xft_colors[color.color - 100 + 8];
  } else if (color.type == COLOR_256) {
    int idx = color.color;

    if (idx >= 0 && idx < 256) {
      if (idx < 16) {
        return &gui->color.xft_colors[idx];
      }

      if (!gui->color.xft_color_cached[idx]) {
        XRenderColor xrender_color;
        unsigned long r, g, b;

        if (idx < 232) {
          static const int cube_levels[6] = {0, 95, 135, 175, 215, 255};
          int cube_idx = idx - 16;
          r = cube_levels[cube_idx / 36];
          g = cube_levels[(cube_idx / 6) % 6];
          b = cube_levels[cube_idx % 6];
        } else {
          int gray = 8 + (idx - 232) * 10;
          r = g = b = gray;
        }

        xrender_color.red = r << 8;
        xrender_color.green = g << 8;
        xrender_color.blue = b << 8;
        xrender_color.alpha = 0xffff;
        XftColorAllocValue(gui->x11.display, gui->x11.visual, gui->x11.colormap,
                           &xrender_color, &gui->color.xft_color_cache[idx]);
        gui->color.xft_color_cached[idx] = true;
      }
      return &gui->color.xft_color_cache[idx];
    }
  } else if (color.type == COLOR_RGB) {
    int key = (color.rgb.red << 16) | (color.rgb.green << 8) | color.rgb.blue;
    for (int i = 0; i < 64; i++) {
      if (gui->color.rgb_cache_valid[i] && gui->color.rgb_cache_keys[i] == key)
        return &gui->color.rgb_cache[i];
    }
    int slot = gui->color.rgb_cache_next;
    gui->color.rgb_cache_next = (gui->color.rgb_cache_next + 1) % 64;
    if (gui->color.rgb_cache_valid[slot]) {
      XftColorFree(gui->x11.display, gui->x11.visual, gui->x11.colormap,
                   &gui->color.rgb_cache[slot]);
    }
    XRenderColor xrender_color;
    xrender_color.red = (unsigned short)(color.rgb.red << 8);
    xrender_color.green = (unsigned short)(color.rgb.green << 8);
    xrender_color.blue = (unsigned short)(color.rgb.blue << 8);
    xrender_color.alpha = 0xffff;
    XftColorAllocValue(gui->x11.display, gui->x11.visual, gui->x11.colormap, &xrender_color,
                       &gui->color.rgb_cache[slot]);
    gui->color.rgb_cache_keys[slot] = key;
    gui->color.rgb_cache_valid[slot] = true;
    return &gui->color.rgb_cache[slot];
  }
  return &gui->color.xft_white;
}

static bool cell_in_selection(GuiContext *gui, int x, int y) {
  if (!gui->selection.has_selection)
    return false;
  int ax = gui->selection.sel_anchor_x, ay = gui->selection.sel_anchor_y;
  int bx = gui->selection.sel_cur_x, by = gui->selection.sel_cur_y;
  int start_x, start_y, end_x, end_y;
  if (ay < by || (ay == by && ax <= bx)) {
    start_x = ax;
    start_y = ay;
    end_x = bx;
    end_y = by;
  } else {
    start_x = bx;
    start_y = by;
    end_x = ax;
    end_y = ay;
  }
  if (y < start_y || y > end_y)
    return false;
  if (y == start_y && x < start_x)
    return false;
  if (y == end_y && x > end_x)
    return false;
  return true;
}

void build_selection_text(GuiContext *gui, Terminal *terminal) {
  free(gui->selection.selection_text);
  gui->selection.selection_text = NULL;
  gui->selection.selection_len = 0;
  if (!gui->selection.has_selection)
    return;

  int ax = gui->selection.sel_anchor_x, ay = gui->selection.sel_anchor_y;
  int bx = gui->selection.sel_cur_x, by = gui->selection.sel_cur_y;
  int start_x, start_y, end_x, end_y;
  if (ay < by || (ay == by && ax <= bx)) {
    start_x = ax;
    start_y = ay;
    end_x = bx;
    end_y = by;
  } else {
    start_x = bx;
    start_y = by;
    end_x = ax;
    end_y = ay;
  }

  Term_Screen *scr =
      terminal->screens.using_alt_screen ? &terminal->screens.alt_screen : &terminal->screens.screen;
  Term_Scrollback *sb = &scr->scrollback;

  int max_len = (terminal->dims.width * 6 + 1) * (end_y - start_y + 1) + 1;
  char *buf = malloc(max_len);
  if (!buf)
    return;
  int pos = 0;

  for (int combined = start_y; combined <= end_y; combined++) {
    int x0 = (combined == start_y) ? start_x : 0;
    int x1 = (combined == end_y) ? end_x : terminal->dims.width - 1;

    for (int x = x0; x <= x1; x++) {
      Term_Cell cell;
      if (combined < 0) {
        memset(&cell, 0, sizeof(Term_Cell));
      } else if (combined < sb->count) {
        int idx = (sb->head + combined) % sb->capacity;
        cell = (x < sb->widths[idx]) ? sb->lines[idx][x] : (Term_Cell){0};
      } else {
        cell = scr->lines[combined - sb->count].cells[x];
      }
      if (cell.length > 0) {
        memcpy(buf + pos, cell.data, cell.length);
        pos += cell.length;
      } else {
        buf[pos++] = ' ';
      }
    }
    if (combined < end_y)
      buf[pos++] = '\n';
  }

  buf[pos] = '\0';
  gui->selection.selection_text = buf;
  gui->selection.selection_len = pos;
}

void run_search(GuiContext *gui, Terminal *terminal) {
  gui->search.search_match_count = 0;
  gui->search.search_current = -1;
  if (gui->search.search_query_len == 0)
    return;

  Term_Screen *scr =
      terminal->screens.using_alt_screen ? &terminal->screens.alt_screen : &terminal->screens.screen;
  Term_Scrollback *sb = &scr->scrollback;
  int total_rows = sb->count + terminal->dims.height;

  for (int row = 0; row < total_rows; row++) {
    char buf[4096];
    int col_at_byte[4096];
    int buf_len = 0;

    for (int x = 0; x < terminal->dims.width && buf_len < (int)sizeof(buf) - 7;
         x++) {
      Term_Cell cell;
      if (row < sb->count) {
        int idx = (sb->head + row) % sb->capacity;
        cell = (x < sb->widths[idx]) ? sb->lines[idx][x] : (Term_Cell){0};
      } else {
        cell = scr->lines[row - sb->count].cells[x];
      }
      for (int k = 0; k < cell.length && buf_len < (int)sizeof(buf) - 1; k++) {
        col_at_byte[buf_len] = x;
        buf[buf_len++] = cell.data[k];
      }
    }
    buf[buf_len] = '\0';

    const char *p = buf;
    while (*p) {
      const char *found = strstr(p, gui->search.search_query);
      if (!found)
        break;
      int bs = found - buf;
      int be = bs + gui->search.search_query_len - 1;
      if (be >= buf_len)
        be = buf_len - 1;
      if (gui->search.search_match_count < SEARCH_MAX_MATCHES) {
        gui->search.search_rows[gui->search.search_match_count] = row;
        gui->search.search_start_cols[gui->search.search_match_count] =
            (bs < buf_len) ? col_at_byte[bs] : 0;
        gui->search.search_end_cols[gui->search.search_match_count] =
            (be >= 0 && be < buf_len) ? col_at_byte[be] : 0;
        gui->search.search_match_count++;
      }
      p = found + gui->search.search_query_len;
    }
  }

  // Focus first match at or after the current scroll position
  if (gui->search.search_match_count > 0) {
    int first_visible = scr->scrollback.count - scr->scroll_offset;
    gui->search.search_current = 0;
    for (int m = 0; m < gui->search.search_match_count; m++) {
      if (gui->search.search_rows[m] >= first_visible) {
        gui->search.search_current = m;
        break;
      }
    }
    // Scroll to bring the focused match into view
    int target = scr->scrollback.count - gui->search.search_rows[gui->search.search_current];
    if (target < 0)
      target = 0;
    if (target > scr->scrollback.count)
      target = scr->scrollback.count;
    scr->scroll_offset = target;
  }
}

// Fill a rectangle on the backbuffer using XRender (for transparency) or
// XFillRectangle. When gui->surface.alpha < 255, all bg fills go through XRender so
// the alpha channel in the ARGB pixmap is set correctly for the compositor.
static void bg_fill(GuiContext *gui, int x, int y, int w, int h,
                    unsigned long rgb, int alpha) {
  if (gui->surface.alpha == 255) {
    XSetForeground(gui->x11.display, gui->x11.gc, rgb);
    XFillRectangle(gui->x11.display, gui->surface.backbuffer, gui->x11.gc, x, y, w, h);
  } else {
    XRenderColor xrc = {
        .red = (unsigned short)(((rgb >> 16) & 0xFF) * 257),
        .green = (unsigned short)(((rgb >> 8) & 0xFF) * 257),
        .blue = (unsigned short)((rgb & 0xFF) * 257),
        .alpha = (unsigned short)(alpha * 257),
    };
    XRenderFillRectangle(gui->x11.display, PictOpSrc, gui->surface.backbuffer_picture, &xrc,
                         x, y, w, h);
  }
}

// When using an ARGB visual, XFillRectangle/XDrawLine foreground pixels must
// have 0xFF in the alpha byte or they render as transparent.
static unsigned long opaque_pixel(GuiContext *gui, unsigned long pixel) {
  return (gui->surface.alpha < 255) ? (0xFF000000UL | (pixel & 0xFFFFFF)) : pixel;
}

void draw_terminal(GuiContext *gui, Terminal *terminal) {
  Term_Screen *term_screen =
      terminal->screens.using_alt_screen ? &terminal->screens.alt_screen : &terminal->screens.screen;

  // Clear entire backbuffer: transparent bg, or opaque fg during bell flash
  if (gui->bell.bell_flash) {
    bg_fill(gui, 0, 0, gui->surface.window_width, gui->surface.window_height, gui->color.default_fg,
            255);
  } else {
    bg_fill(gui, 0, 0, gui->surface.window_width, gui->surface.window_height, gui->color.default_bg,
            gui->surface.alpha);
  }

  int scroll_offset = term_screen->scroll_offset;
  Term_Scrollback *sb = &term_screen->scrollback;

  for (int y = 0; y < terminal->dims.height; y++) {
    if (gui->surface.margin >= 4) {
      int combined_row = sb->count - scroll_offset + y;
      int oldest = sb->count - sb->capacity;
      for (int m = 0; m < terminal->marks.shell_mark_count; m++) {
        int mark =
            terminal
                ->marks.shell_marks[(terminal->marks.shell_mark_head + m) % SHELL_MARK_MAX];
        if (mark < oldest)
          continue;
        if (mark == combined_row) {
          XSetForeground(gui->x11.display, gui->x11.gc,
                         opaque_pixel(gui, gui->color.colors[2]));
          XFillRectangle(gui->x11.display, gui->surface.backbuffer, gui->x11.gc, 0,
                         y * gui->fonts.char_height + gui->surface.margin, 3,
                         gui->fonts.char_height);
          break;
        }
      }
    }

    for (int x = 0; x < terminal->dims.width; x++) {
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

      if (cell.wide_cont)
        continue;

      int pixel_x = x * gui->fonts.char_width + gui->surface.margin;
      int pixel_y = y * (gui->fonts.char_height) + gui->surface.margin;
      int draw_width = cell.wide ? gui->fonts.char_width * 2 : gui->fonts.char_width;

      bool is_default_bg =
          (cell.attr.bg.type == COLOR_DEFAULT && cell.attr.bg.color == 0);
      unsigned long bg_color = gui->color.default_bg;
      if (!is_default_bg) {
        bg_color = get_color_pixel(gui, cell.attr.bg);
      }

      if (gui->search.search_active) {
        for (int m = 0; m < gui->search.search_match_count; m++) {
          if (gui->search.search_rows[m] > combined)
            break;
          if (gui->search.search_rows[m] == combined &&
              gui->search.search_start_cols[m] <= x && x <= gui->search.search_end_cols[m]) {
            Term_Color hc;
            hc.type = COLOR_RGB;
            if (m == gui->search.search_current) {
              hc.rgb = (Term_RGB){255, 165, 0}; // orange: focused match
            } else {
              hc.rgb = (Term_RGB){160, 120, 0}; // dark gold: other matches
            }
            bg_color = get_color_pixel(gui, hc);
            is_default_bg = false;
            break;
          }
        }
      }

      bool is_cursor =
          gui->cursor.cursor_visible && !term_screen->cursor_hidden &&
          (scroll_offset == 0) &&
          (term_screen->cursor.x == x && term_screen->cursor.y == y);
      int cursor_shape = terminal->modes.cursor_shape;
      bool is_block_cursor = is_cursor && (cursor_shape <= 2);
      bool in_selection = cell_in_selection(gui, x, combined);
      bool reverse = cell.attr.reverse || is_block_cursor || in_selection;

      if (reverse)
        is_default_bg = false;

      unsigned long text_color;
      if (reverse) {
        text_color = bg_color;
        bg_color =
            (cell.attr.fg.type != COLOR_DEFAULT || cell.attr.fg.color != 0)
                ? get_color_pixel(gui, cell.attr.fg)
                : gui->color.default_fg;
      } else {
        text_color =
            (cell.attr.fg.type != COLOR_DEFAULT || cell.attr.fg.color != 0)
                ? get_color_pixel(gui, cell.attr.fg)
                : gui->color.default_fg;
      }

      // Default-bg cells are already painted by the initial clear; only draw
      // explicitly-colored backgrounds (and always opaque).
      int cell_alpha = is_default_bg ? gui->surface.alpha : 255;
      bg_fill(gui, pixel_x, pixel_y, draw_width, gui->fonts.char_height, bg_color,
              cell_alpha);

      if (cell.length > 0) {
        XftColor *fg_color;
        XftFont *font_to_use = cell.attr.bold     ? gui->fonts.font_bold
                               : cell.attr.italic ? gui->fonts.font_italic
                                                  : gui->fonts.font;

        if (reverse) {
          fg_color =
              (cell.attr.bg.type != COLOR_DEFAULT || cell.attr.bg.color != 0)
                  ? get_xft_color(gui, cell.attr.bg)
                  : &gui->color.xft_default_bg;
        } else {
          fg_color =
              (cell.attr.fg.type != COLOR_DEFAULT || cell.attr.fg.color != 0)
                  ? get_xft_color(gui, cell.attr.fg)
                  : &gui->color.xft_default_fg;
        }

        XftColor dim_color;
        if (cell.attr.dim && !reverse) {
          dim_color = *fg_color;
          dim_color.color.red >>= 1;
          dim_color.color.green >>= 1;
          dim_color.color.blue >>= 1;
          fg_color = &dim_color;
        }

        if (cell.attr.blink && !gui->cursor.cursor_visible)
          goto skip_text;

        XftDrawStringUtf8(gui->color.xft_draw, fg_color, font_to_use, pixel_x,
                          pixel_y + gui->fonts.char_ascent, (FcChar8 *)cell.data,
                          cell.length);

        if (cell.attr.underline || cell.attr.uri_idx > 0) {
          XSetForeground(gui->x11.display, gui->x11.gc, opaque_pixel(gui, text_color));
          XDrawLine(gui->x11.display, gui->surface.backbuffer, gui->x11.gc, pixel_x,
                    pixel_y + gui->fonts.char_height - 1, pixel_x + draw_width - 1,
                    pixel_y + gui->fonts.char_height - 1);
        }
        if (cell.attr.strikethrough) {
          XSetForeground(gui->x11.display, gui->x11.gc, opaque_pixel(gui, text_color));
          XDrawLine(gui->x11.display, gui->surface.backbuffer, gui->x11.gc, pixel_x,
                    pixel_y + gui->fonts.char_ascent / 2, pixel_x + draw_width - 1,
                    pixel_y + gui->fonts.char_ascent / 2);
        }
      skip_text:;
      }

      if (is_cursor && !is_block_cursor) {
        XSetForeground(gui->x11.display, gui->x11.gc,
                       opaque_pixel(gui, gui->color.default_fg));
        if (cursor_shape == 3 || cursor_shape == 4) {
          XFillRectangle(gui->x11.display, gui->surface.backbuffer, gui->x11.gc, pixel_x,
                         pixel_y + gui->fonts.char_height - 2, draw_width, 2);
        } else {
          XFillRectangle(gui->x11.display, gui->surface.backbuffer, gui->x11.gc, pixel_x,
                         pixel_y, 2, gui->fonts.char_height);
        }
      }
    }
  }

  if (gui->search.search_active) {
    int bar_y = gui->surface.window_height - gui->fonts.char_height - gui->surface.margin;
    Term_Color bar_bg = {.type = COLOR_RGB, .rgb = {255, 220, 50}};
    XSetForeground(gui->x11.display, gui->x11.gc,
                   opaque_pixel(gui, get_color_pixel(gui, bar_bg)));
    XFillRectangle(gui->x11.display, gui->surface.backbuffer, gui->x11.gc, 0, bar_y,
                   gui->surface.window_width, gui->fonts.char_height + gui->surface.margin);
    char bar[400];
    int blen;
    if (gui->search.search_query_len == 0) {
      blen = snprintf(bar, sizeof(bar), " /");
    } else if (gui->search.search_match_count == 0) {
      blen =
          snprintf(bar, sizeof(bar), " /%s  [no matches]", gui->search.search_query);
    } else {
      blen = snprintf(bar, sizeof(bar), " /%s  [%d/%d]", gui->search.search_query,
                      gui->search.search_current + 1, gui->search.search_match_count);
    }
    XftDrawStringUtf8(gui->color.xft_draw, &gui->color.xft_colors[0], gui->fonts.font,
                      gui->surface.margin, bar_y + gui->fonts.char_ascent, (FcChar8 *)bar,
                      blen > 0 ? blen : 0);
  }

  XCopyArea(gui->x11.display, gui->surface.backbuffer, gui->x11.window, gui->x11.gc, 0, 0,
            gui->surface.window_width, gui->surface.window_height, 0, 0);
}
