#include <stdlib.h>
#include <string.h>

#include "render.h"

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
    XftColorAllocValue(gui->display, visual, colormap, &xrender_color,
                       &gui->xft_colors[i]);
  }

  xrender_color.red = 0xffff;
  xrender_color.green = 0xffff;
  xrender_color.blue = 0xffff;
  xrender_color.alpha = 0xffff;
  XftColorAllocValue(gui->display, visual, colormap, &xrender_color,
                     &gui->xft_white);

  xrender_color.red = 0;
  xrender_color.green = 0;
  xrender_color.blue = 0;
  xrender_color.alpha = 0xffff;
  XftColorAllocValue(gui->display, visual, colormap, &xrender_color,
                     &gui->xft_black);

  unsigned long fg_val = (args->fg != -1) ? (unsigned long)args->fg : 0xffffff;
  unsigned long bg_val = (args->bg != -1) ? (unsigned long)args->bg : 0x000000;

  color.red = ((fg_val >> 16) & 0xff) << 8;
  color.green = ((fg_val >> 8) & 0xff) << 8;
  color.blue = (fg_val & 0xff) << 8;
  color.flags = DoRed | DoGreen | DoBlue;
  gui->default_fg =
      XAllocColor(gui->display, colormap, &color) ? color.pixel : gui->white;

  color.red = ((bg_val >> 16) & 0xff) << 8;
  color.green = ((bg_val >> 8) & 0xff) << 8;
  color.blue = (bg_val & 0xff) << 8;
  color.flags = DoRed | DoGreen | DoBlue;
  gui->default_bg =
      XAllocColor(gui->display, colormap, &color) ? color.pixel : gui->black;

  xrender_color.red = ((fg_val >> 16) & 0xff) << 8;
  xrender_color.green = ((fg_val >> 8) & 0xff) << 8;
  xrender_color.blue = (fg_val & 0xff) << 8;
  xrender_color.alpha = 0xffff;
  XftColorAllocValue(gui->display, visual, colormap, &xrender_color,
                     &gui->xft_default_fg);

  xrender_color.red = ((bg_val >> 16) & 0xff) << 8;
  xrender_color.green = ((bg_val >> 8) & 0xff) << 8;
  xrender_color.blue = (bg_val & 0xff) << 8;
  xrender_color.alpha = 0xffff;
  XftColorAllocValue(gui->display, visual, colormap, &xrender_color,
                     &gui->xft_default_bg);
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

XftColor *get_xft_color(GuiContext *gui, Term_Color color) {
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
    int idx = color.color;

    if (idx >= 0 && idx < 256) {
      if (idx < 16) {
        return &gui->xft_colors[idx];
      }

      if (!gui->xft_color_cached[idx]) {
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
        XftColorAllocValue(gui->display,
                           DefaultVisual(gui->display, gui->screen),
                           DefaultColormap(gui->display, gui->screen),
                           &xrender_color, &gui->xft_color_cache[idx]);
        gui->xft_color_cached[idx] = true;
      }
      return &gui->xft_color_cache[idx];
    }
  } else if (color.type == COLOR_RGB) {
    int key = (color.rgb.red << 16) | (color.rgb.green << 8) | color.rgb.blue;
    for (int i = 0; i < 64; i++) {
      if (gui->rgb_cache_valid[i] && gui->rgb_cache_keys[i] == key)
        return &gui->rgb_cache[i];
    }
    int slot = gui->rgb_cache_next;
    gui->rgb_cache_next = (gui->rgb_cache_next + 1) % 64;
    if (gui->rgb_cache_valid[slot]) {
      XftColorFree(gui->display, DefaultVisual(gui->display, gui->screen),
                   DefaultColormap(gui->display, gui->screen),
                   &gui->rgb_cache[slot]);
    }
    XRenderColor xrender_color;
    xrender_color.red = (unsigned short)(color.rgb.red << 8);
    xrender_color.green = (unsigned short)(color.rgb.green << 8);
    xrender_color.blue = (unsigned short)(color.rgb.blue << 8);
    xrender_color.alpha = 0xffff;
    XftColorAllocValue(gui->display, DefaultVisual(gui->display, gui->screen),
                       DefaultColormap(gui->display, gui->screen),
                       &xrender_color, &gui->rgb_cache[slot]);
    gui->rgb_cache_keys[slot] = key;
    gui->rgb_cache_valid[slot] = true;
    return &gui->rgb_cache[slot];
  }
  return &gui->xft_white;
}

static bool cell_in_selection(GuiContext *gui, int x, int y) {
  if (!gui->has_selection)
    return false;
  int ax = gui->sel_anchor_x, ay = gui->sel_anchor_y;
  int bx = gui->sel_cur_x, by = gui->sel_cur_y;
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
  free(gui->selection_text);
  gui->selection_text = NULL;
  gui->selection_len = 0;
  if (!gui->has_selection)
    return;

  int ax = gui->sel_anchor_x, ay = gui->sel_anchor_y;
  int bx = gui->sel_cur_x, by = gui->sel_cur_y;
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
      terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;
  Term_Scrollback *sb = &scr->scrollback;

  int max_len = (terminal->width * 6 + 1) * (end_y - start_y + 1) + 1;
  char *buf = malloc(max_len);
  if (!buf)
    return;
  int pos = 0;

  for (int combined = start_y; combined <= end_y; combined++) {
    int x0 = (combined == start_y) ? start_x : 0;
    int x1 = (combined == end_y) ? end_x : terminal->width - 1;

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
  gui->selection_text = buf;
  gui->selection_len = pos;
}

void draw_terminal(GuiContext *gui, Terminal *terminal) {
  Term_Screen *term_screen =
      terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;

  XSetForeground(gui->display, gui->gc,
                 gui->bell_flash ? gui->default_fg : gui->default_bg);
  XFillRectangle(gui->display, gui->backbuffer, gui->gc, 0, 0,
                 gui->window_width, gui->window_height);

  int scroll_offset = term_screen->scroll_offset;
  Term_Scrollback *sb = &term_screen->scrollback;

  for (int y = 0; y < terminal->height; y++) {
    if (gui->margin >= 4) {
      int combined_row = sb->count - scroll_offset + y;
      int oldest = sb->count - sb->capacity;
      for (int m = 0; m < terminal->shell_mark_count; m++) {
        int mark = terminal->shell_marks[(terminal->shell_mark_head + m) % SHELL_MARK_MAX];
        if (mark < oldest) continue;
        if (mark == combined_row) {
          XSetForeground(gui->display, gui->gc, gui->colors[2]);
          XFillRectangle(gui->display, gui->backbuffer, gui->gc,
                         0, y * gui->char_height + gui->margin, 3, gui->char_height);
          break;
        }
      }
    }

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

      int pixel_x = x * gui->char_width + gui->margin;
      int pixel_y = y * (gui->char_height) + gui->margin;

      unsigned long bg_color = gui->default_bg;
      if (cell.attr.bg.type != COLOR_DEFAULT || cell.attr.bg.color != 0) {
        bg_color = get_color_pixel(gui, cell.attr.bg);
      }

      bool is_cursor =
          gui->cursor_visible && !term_screen->cursor_hidden &&
          (scroll_offset == 0) &&
          (term_screen->cursor.x == x && term_screen->cursor.y == y);
      int cursor_shape = terminal->cursor_shape;
      bool is_block_cursor = is_cursor && (cursor_shape <= 2);
      bool in_selection = cell_in_selection(gui, x, combined);
      bool reverse = cell.attr.reverse || is_block_cursor || in_selection;

      unsigned long text_color;
      if (reverse) {
        text_color = bg_color;
        bg_color =
            (cell.attr.fg.type != COLOR_DEFAULT || cell.attr.fg.color != 0)
                ? get_color_pixel(gui, cell.attr.fg)
                : gui->default_fg;
      } else {
        text_color =
            (cell.attr.fg.type != COLOR_DEFAULT || cell.attr.fg.color != 0)
                ? get_color_pixel(gui, cell.attr.fg)
                : gui->default_fg;
      }

      XSetForeground(gui->display, gui->gc, bg_color);
      XFillRectangle(gui->display, gui->backbuffer, gui->gc, pixel_x, pixel_y,
                     gui->char_width, gui->char_height);

      if (cell.length > 0) {
        XftColor *fg_color;
        XftFont *font_to_use = cell.attr.bold   ? gui->font_bold
                             : cell.attr.italic ? gui->font_italic
                                                : gui->font;

        if (reverse) {
          fg_color =
              (cell.attr.bg.type != COLOR_DEFAULT || cell.attr.bg.color != 0)
                  ? get_xft_color(gui, cell.attr.bg)
                  : &gui->xft_default_bg;
        } else {
          fg_color =
              (cell.attr.fg.type != COLOR_DEFAULT || cell.attr.fg.color != 0)
                  ? get_xft_color(gui, cell.attr.fg)
                  : &gui->xft_default_fg;
        }

        XftColor dim_color;
        if (cell.attr.dim && !reverse) {
          dim_color = *fg_color;
          dim_color.color.red   >>= 1;
          dim_color.color.green >>= 1;
          dim_color.color.blue  >>= 1;
          fg_color = &dim_color;
        }

        if (cell.attr.blink && !gui->cursor_visible)
          goto skip_text;

        XftDrawStringUtf8(gui->xft_draw, fg_color, font_to_use, pixel_x,
                          pixel_y + gui->char_ascent, (FcChar8 *)cell.data,
                          cell.length);

        if (cell.attr.underline || cell.attr.uri_idx > 0) {
          XSetForeground(gui->display, gui->gc, text_color);
          XDrawLine(gui->display, gui->backbuffer, gui->gc, pixel_x,
                    pixel_y + gui->char_height - 1,
                    pixel_x + gui->char_width - 1,
                    pixel_y + gui->char_height - 1);
        }
        if (cell.attr.strikethrough) {
          XSetForeground(gui->display, gui->gc, text_color);
          XDrawLine(gui->display, gui->backbuffer, gui->gc, pixel_x,
                    pixel_y + gui->char_ascent / 2,
                    pixel_x + gui->char_width - 1,
                    pixel_y + gui->char_ascent / 2);
        }
        skip_text:;
      }

      if (is_cursor && !is_block_cursor) {
        XSetForeground(gui->display, gui->gc, gui->default_fg);
        if (cursor_shape == 3 || cursor_shape == 4) {
          XFillRectangle(gui->display, gui->backbuffer, gui->gc, pixel_x,
                         pixel_y + gui->char_height - 2, gui->char_width, 2);
        } else {
          XFillRectangle(gui->display, gui->backbuffer, gui->gc, pixel_x,
                         pixel_y, 2, gui->char_height);
        }
      }
    }
  }

  XCopyArea(gui->display, gui->backbuffer, gui->window, gui->gc, 0, 0,
            gui->window_width, gui->window_height, 0, 0);
}
