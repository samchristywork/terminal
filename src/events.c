#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>
#include <X11/keysym.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "events.h"
#include "log.h"
#include "render.h"

static void send_mouse_event(GuiContext *gui, Terminal *terminal, int btn,
                             int x, int y, bool release) {
  char buf[32];
  int len;
  if (terminal->modes.mouse_sgr) {
    len = snprintf(buf, sizeof(buf), "\x1b[<%d;%d;%d%c", btn, x + 1, y + 1,
                   release ? 'm' : 'M');
  } else {
    if (x + 1 > 223 || y + 1 > 223)
      return;
    buf[0] = '\x1b';
    buf[1] = '[';
    buf[2] = 'M';
    buf[3] = (char)((release ? 3 : btn) + 32);
    buf[4] = (char)(x + 1 + 32);
    buf[5] = (char)(y + 1 + 32);
    len = 6;
  }
  write(gui->process.pipe_fd, buf, len);
}

static void on_configure(GuiContext *gui, Terminal *terminal,
                         XConfigureEvent *ev) {
  int new_width = ev->width;
  int new_height = ev->height;

  if (new_width == gui->surface.window_width && new_height == gui->surface.window_height)
    return;

  LOG_DEBUG_MSG("Window resized to %dx%d", new_width, new_height);
  gui->surface.window_width = new_width;
  gui->surface.window_height = new_height;

  int depth = (gui->surface.alpha < 255) ? 32 : DefaultDepth(gui->x11.display, gui->x11.screen);
  XFreePixmap(gui->x11.display, gui->surface.backbuffer);
  gui->surface.backbuffer = XCreatePixmap(gui->x11.display, gui->x11.window, gui->surface.window_width,
                                  gui->surface.window_height, depth);

  XftDrawDestroy(gui->color.xft_draw);
  gui->color.xft_draw =
      XftDrawCreate(gui->x11.display, gui->surface.backbuffer, gui->x11.visual, gui->x11.colormap);

  if (gui->surface.backbuffer_picture) {
    XRenderFreePicture(gui->x11.display, gui->surface.backbuffer_picture);
    XRenderPictFormat *fmt = XRenderFindVisualFormat(gui->x11.display, gui->x11.visual);
    gui->surface.backbuffer_picture =
        fmt ? XRenderCreatePicture(gui->x11.display, gui->surface.backbuffer, fmt, 0, NULL)
            : None;
  }

  int term_cols = (new_width - 2 * gui->surface.margin) / gui->fonts.char_width;
  int term_rows = (new_height - 2 * gui->surface.margin) / gui->fonts.char_height;
  if (term_cols < 1)
    term_cols = 1;
  if (term_rows < 1)
    term_rows = 1;

  LOG_DEBUG_MSG("Terminal resized to %dx%d", term_cols, term_rows);
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

static void on_key_press(GuiContext *gui, Terminal *terminal, XKeyEvent *ev) {
  gui->cursor.cursor_visible = true;
  clock_gettime(CLOCK_MONOTONIC, &gui->cursor.last_blink);

  char buffer[32];
  KeySym keysym;
  int buffer_len = XLookupString(ev, buffer, sizeof(buffer), &keysym, NULL);

  Term_Screen *scr =
      terminal->screens.using_alt_screen ? &terminal->screens.alt_screen : &terminal->screens.screen;
  int max_scroll = scr->scrollback.count;

  // Toggle search with Ctrl+Shift+F
  if ((keysym == XK_f || keysym == XK_F) && (ev->state & ControlMask) &&
      (ev->state & ShiftMask)) {
    gui->search.search_active = !gui->search.search_active;
    if (gui->search.search_active) {
      gui->search.search_query_len = 0;
      gui->search.search_query[0] = '\0';
      gui->search.search_match_count = 0;
      gui->search.search_current = -1;
    }
    draw_terminal(gui, terminal);
    return;
  }

  if (gui->search.search_active) {
    if (keysym == XK_Escape) {
      gui->search.search_active = false;
      gui->search.search_match_count = 0;
    } else if (keysym == XK_Return || keysym == XK_KP_Enter) {
      if (gui->search.search_match_count > 0) {
        int dir = (ev->state & ShiftMask) ? -1 : 1;
        gui->search.search_current =
            (gui->search.search_current + dir + gui->search.search_match_count) %
            gui->search.search_match_count;
        int abs_row = gui->search.search_rows[gui->search.search_current];
        int offset = scr->scrollback.count - abs_row;
        if (offset < 0)
          offset = 0;
        if (offset > max_scroll)
          offset = max_scroll;
        scr->scroll_offset = offset;
      }
    } else if (keysym == XK_BackSpace) {
      if (gui->search.search_query_len > 0) {
        gui->search.search_query_len--;
        while (gui->search.search_query_len > 0 &&
               (gui->search.search_query[gui->search.search_query_len] & 0xC0) == 0x80)
          gui->search.search_query_len--;
        gui->search.search_query[gui->search.search_query_len] = '\0';
        run_search(gui, terminal);
      }
    } else if (buffer_len > 0 && (unsigned char)buffer[0] >= 0x20) {
      if (gui->search.search_query_len + buffer_len <
          (int)sizeof(gui->search.search_query) - 1) {
        memcpy(gui->search.search_query + gui->search.search_query_len, buffer, buffer_len);
        gui->search.search_query_len += buffer_len;
        gui->search.search_query[gui->search.search_query_len] = '\0';
        run_search(gui, terminal);
      }
    }
    draw_terminal(gui, terminal);
    return;
  }

  if (keysym == XK_Prior && (ev->state & ShiftMask)) {
    scr->scroll_offset += terminal->dims.height;
    if (scr->scroll_offset > max_scroll)
      scr->scroll_offset = max_scroll;
    draw_terminal(gui, terminal);
  } else if (keysym == XK_Next && (ev->state & ShiftMask)) {
    scr->scroll_offset -= terminal->dims.height;
    if (scr->scroll_offset < 0)
      scr->scroll_offset = 0;
    draw_terminal(gui, terminal);
  } else if ((keysym == XK_c || keysym == XK_C) && (ev->state & ControlMask) &&
             (ev->state & ShiftMask)) {
    if (gui->selection.has_selection)
      XSetSelectionOwner(gui->x11.display, gui->selection.atom_clipboard, gui->x11.window,
                         CurrentTime);
  } else if ((keysym == XK_v || keysym == XK_V) && (ev->state & ControlMask) &&
             (ev->state & ShiftMask)) {
    XConvertSelection(gui->x11.display, gui->selection.atom_clipboard, gui->selection.atom_utf8_string,
                      gui->selection.atom_xsel_data, gui->x11.window, CurrentTime);
  } else if (keysym == XK_BackSpace) {
    write(gui->process.pipe_fd, "\x7f", 1);
  } else if (keysym == XK_Return || keysym == XK_KP_Enter) {
    write(gui->process.pipe_fd, "\r", 1);
  } else if (keysym == XK_Tab) {
    write(gui->process.pipe_fd, "\t", 1);
  } else if (keysym == XK_Escape) {
    write(gui->process.pipe_fd, "\x1b", 1);
  } else if (keysym == XK_Up && (ev->state & ControlMask) &&
             (ev->state & ShiftMask)) {
    int cur_top = scr->scrollback.count - scr->scroll_offset;
    int oldest = scr->scrollback.count - scr->scrollback.capacity;
    int best = -1;
    for (int m = terminal->marks.shell_mark_count - 1; m >= 0; m--) {
      int mark =
          terminal
              ->marks.shell_marks[(terminal->marks.shell_mark_head + m) % SHELL_MARK_MAX];
      if (mark < oldest)
        continue;
      if (mark < cur_top) {
        best = mark;
        break;
      }
    }
    if (best >= 0) {
      scr->scroll_offset = scr->scrollback.count - best;
      if (scr->scroll_offset > max_scroll)
        scr->scroll_offset = max_scroll;
      draw_terminal(gui, terminal);
    }
  } else if (keysym == XK_Down && (ev->state & ControlMask) &&
             (ev->state & ShiftMask)) {
    int cur_top = scr->scrollback.count - scr->scroll_offset;
    int oldest = scr->scrollback.count - scr->scrollback.capacity;
    int best = -1;
    for (int m = 0; m < terminal->marks.shell_mark_count; m++) {
      int mark =
          terminal
              ->marks.shell_marks[(terminal->marks.shell_mark_head + m) % SHELL_MARK_MAX];
      if (mark < oldest)
        continue;
      if (mark > cur_top) {
        best = mark;
        break;
      }
    }
    if (best >= 0) {
      scr->scroll_offset = scr->scrollback.count - best;
      if (scr->scroll_offset < 0)
        scr->scroll_offset = 0;
    } else {
      scr->scroll_offset = 0;
    }
    draw_terminal(gui, terminal);
  } else if (keysym == XK_Up) {
    write(gui->process.pipe_fd, (ev->state & ControlMask) ? "\x1b[1;5A" : "\x1b[A",
          (ev->state & ControlMask) ? 6 : 3);
  } else if (keysym == XK_Down) {
    write(gui->process.pipe_fd, (ev->state & ControlMask) ? "\x1b[1;5B" : "\x1b[B",
          (ev->state & ControlMask) ? 6 : 3);
  } else if (keysym == XK_Right) {
    write(gui->process.pipe_fd, (ev->state & ControlMask) ? "\x1b[1;5C" : "\x1b[C",
          (ev->state & ControlMask) ? 6 : 3);
  } else if (keysym == XK_Left) {
    write(gui->process.pipe_fd, (ev->state & ControlMask) ? "\x1b[1;5D" : "\x1b[D",
          (ev->state & ControlMask) ? 6 : 3);
  } else if (keysym == XK_Home) {
    write(gui->process.pipe_fd, "\x1b[H", 3);
  } else if (keysym == XK_End) {
    write(gui->process.pipe_fd, "\x1b[F", 3);
  } else if (keysym == XK_Insert) {
    write(gui->process.pipe_fd, "\x1b[2~", 4);
  } else if (keysym == XK_Delete) {
    write(gui->process.pipe_fd, "\x1b[3~", 4);
  } else if (keysym == XK_Prior) {
    write(gui->process.pipe_fd, "\x1b[5~", 4);
  } else if (keysym == XK_Next) {
    write(gui->process.pipe_fd, "\x1b[6~", 4);
  } else if ((keysym == XK_plus || keysym == XK_equal) &&
             (ev->state & ControlMask)) {
    change_font_size(gui, terminal, 1);
  } else if (keysym == XK_minus && (ev->state & ControlMask)) {
    change_font_size(gui, terminal, -1);
  } else if (keysym >= XK_F1 && keysym <= XK_F12) {
    const char *fkeys[] = {
        "\x1bOP",   "\x1bOQ",   "\x1bOR",   "\x1bOS",   "\x1b[15~", "\x1b[17~",
        "\x1b[18~", "\x1b[19~", "\x1b[20~", "\x1b[21~", "\x1b[23~", "\x1b[24~",
    };
    write(gui->process.pipe_fd, fkeys[keysym - XK_F1], strlen(fkeys[keysym - XK_F1]));
  } else {
    int len = XLookupString(ev, buffer, sizeof(buffer), NULL, NULL);
    if (len > 0) {
      if (ev->state & Mod1Mask) {
        char alt_buf[33];
        alt_buf[0] = '\x1b';
        memcpy(&alt_buf[1], buffer, len);
        write(gui->process.pipe_fd, alt_buf, len + 1);
      } else {
        write(gui->process.pipe_fd, buffer, len);
      }
    }
  }
}

static Term_Cell *cell_at(Term_Screen *scr, Terminal *terminal, int abs_row,
                          int col) {
  if (abs_row < 0 || col < 0)
    return NULL;
  if (abs_row < scr->scrollback.count) {
    int sb_idx = (scr->scrollback.head + abs_row) % scr->scrollback.capacity;
    if (col >= scr->scrollback.widths[sb_idx])
      return NULL;
    return &scr->scrollback.lines[sb_idx][col];
  }
  int row = abs_row - scr->scrollback.count;
  if (row >= terminal->dims.height || col >= terminal->dims.width)
    return NULL;
  return &scr->lines[row].cells[col];
}

static bool is_word_char(Term_Cell *cell) {
  if (!cell || cell->length == 0)
    return false;
  unsigned char c = (unsigned char)cell->data[0];
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c > 127;
}

static void select_word(GuiContext *gui, Terminal *terminal, Term_Screen *scr,
                        int abs_row, int col) {
  int row_width =
      (abs_row < scr->scrollback.count)
          ? scr->scrollback.widths[(scr->scrollback.head + abs_row) %
                                   scr->scrollback.capacity]
          : terminal->dims.width;

  int start = col;
  int end = col;

  if (is_word_char(cell_at(scr, terminal, abs_row, col))) {
    while (start > 0 &&
           is_word_char(cell_at(scr, terminal, abs_row, start - 1)))
      start--;
    while (end < row_width - 1 &&
           is_word_char(cell_at(scr, terminal, abs_row, end + 1)))
      end++;
  }

  gui->selection.sel_anchor_x = start;
  gui->selection.sel_anchor_y = abs_row;
  gui->selection.sel_cur_x = end;
  gui->selection.sel_cur_y = abs_row;
  gui->selection.has_selection = true;
  gui->selection.selecting = false;
}

static void on_button_press(GuiContext *gui, Terminal *terminal,
                            XButtonEvent *ev) {
  Term_Screen *scr =
      terminal->screens.using_alt_screen ? &terminal->screens.alt_screen : &terminal->screens.screen;
  int max_scroll = scr->scrollback.count;
  int cell_x = (ev->x - gui->surface.margin) / gui->fonts.char_width;
  int cell_y = (ev->y - gui->surface.margin) / gui->fonts.char_height;
  if (cell_x < 0)
    cell_x = 0;
  if (cell_x >= terminal->dims.width)
    cell_x = terminal->dims.width - 1;
  if (cell_y < 0)
    cell_y = 0;
  if (cell_y >= terminal->dims.height)
    cell_y = terminal->dims.height - 1;
  bool shift = (ev->state & ShiftMask) != 0;

  if (ev->button == Button1 && (ev->state & ControlMask)) {
    int abs_row = scr->scrollback.count - scr->scroll_offset + cell_y;
    Term_Cell *lc = cell_at(scr, terminal, abs_row, cell_x);
    if (lc && lc->attr.uri_idx > 0) {
      const char *uri = terminal->uri.uri_table[lc->attr.uri_idx - 1];
      pid_t pid = fork();
      if (pid == 0) {
        setsid();
        execlp("xdg-open", "xdg-open", uri, (char *)NULL);
        _exit(1);
      }
      return;
    }
  }

  if (terminal->modes.mouse_mode >= 1 && !shift) {
    int btn = -1;
    switch (ev->button) {
    case Button1:
      btn = 0;
      break;
    case Button2:
      btn = 1;
      break;
    case Button3:
      btn = 2;
      break;
    case Button4:
      btn = 64;
      break;
    case Button5:
      btn = 65;
      break;
    }
    if (btn >= 0) {
      if (ev->state & Mod1Mask)
        btn |= 8;
      if (ev->state & ControlMask)
        btn |= 16;
      send_mouse_event(gui, terminal, btn, cell_x, cell_y, false);
      return;
    }
  }

  if (ev->button == Button1) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed_ms = (now.tv_sec - gui->click.last_click_time.tv_sec) * 1000 +
                      (now.tv_nsec - gui->click.last_click_time.tv_nsec) / 1000000;
    int anchor_row = scr->scrollback.count - scr->scroll_offset + cell_y;

    if (elapsed_ms < 300 && cell_x == gui->click.last_click_x &&
        anchor_row == gui->click.last_click_y) {
      select_word(gui, terminal, scr, anchor_row, cell_x);
      if (gui->selection.has_selection) {
        build_selection_text(gui, terminal);
        XSetSelectionOwner(gui->x11.display, XA_PRIMARY, gui->x11.window, CurrentTime);
      }
      gui->click.last_click_time.tv_sec = 0;
      gui->click.last_click_time.tv_nsec = 0;
    } else {
      gui->click.last_click_time = now;
      gui->click.last_click_x = cell_x;
      gui->click.last_click_y = anchor_row;
      gui->selection.selecting = true;
      gui->selection.has_selection = false;
      gui->selection.sel_anchor_x = cell_x;
      gui->selection.sel_anchor_y = anchor_row;
      gui->selection.sel_cur_x = cell_x;
      gui->selection.sel_cur_y = anchor_row;
    }
    draw_terminal(gui, terminal);
  } else if (ev->button == Button2) {
    XConvertSelection(gui->x11.display, XA_PRIMARY, gui->selection.atom_utf8_string,
                      gui->selection.atom_xsel_data, gui->x11.window, CurrentTime);
  } else if (ev->button == Button4) {
    scr->scroll_offset += 3;
    if (scr->scroll_offset > max_scroll)
      scr->scroll_offset = max_scroll;
    draw_terminal(gui, terminal);
  } else if (ev->button == Button5) {
    scr->scroll_offset -= 3;
    if (scr->scroll_offset < 0)
      scr->scroll_offset = 0;
    draw_terminal(gui, terminal);
  }
}

static void on_button_release(GuiContext *gui, Terminal *terminal,
                              XButtonEvent *ev) {
  bool shift = (ev->state & ShiftMask) != 0;

  if (terminal->modes.mouse_mode >= 1 && !shift) {
    int cell_x = (ev->x - gui->surface.margin) / gui->fonts.char_width;
    int cell_y = (ev->y - gui->surface.margin) / gui->fonts.char_height;
    if (cell_x < 0)
      cell_x = 0;
    if (cell_x >= terminal->dims.width)
      cell_x = terminal->dims.width - 1;
    if (cell_y < 0)
      cell_y = 0;
    if (cell_y >= terminal->dims.height)
      cell_y = terminal->dims.height - 1;

    int btn = -1;
    switch (ev->button) {
    case Button1:
      btn = 0;
      break;
    case Button2:
      btn = 1;
      break;
    case Button3:
      btn = 2;
      break;
    }

    if (btn >= 0) {
      if (ev->state & Mod1Mask)
        btn |= 8;
      if (ev->state & ControlMask)
        btn |= 16;
      send_mouse_event(gui, terminal, btn, cell_x, cell_y, true);
      return;
    }
  }

  if (ev->button != Button1)
    return;

  if (gui->selection.selecting) {
    Term_Screen *scr =
        terminal->screens.using_alt_screen ? &terminal->screens.alt_screen : &terminal->screens.screen;
    int cell_x = (ev->x - gui->surface.margin) / gui->fonts.char_width;
    int cell_y = (ev->y - gui->surface.margin) / gui->fonts.char_height;
    if (cell_x < 0)
      cell_x = 0;
    if (cell_x >= terminal->dims.width)
      cell_x = terminal->dims.width - 1;
    if (cell_y < 0)
      cell_y = 0;
    if (cell_y >= terminal->dims.height)
      cell_y = terminal->dims.height - 1;
    int cur_row = scr->scrollback.count - scr->scroll_offset + cell_y;
    gui->selection.sel_cur_x = cell_x;
    gui->selection.sel_cur_y = cur_row;
    gui->selection.selecting = false;
    gui->selection.has_selection =
        (gui->selection.sel_anchor_x != cell_x || gui->selection.sel_anchor_y != cur_row);
    if (gui->selection.has_selection) {
      build_selection_text(gui, terminal);
      XSetSelectionOwner(gui->x11.display, XA_PRIMARY, gui->x11.window, CurrentTime);
    }
    draw_terminal(gui, terminal);
  }
}

static void on_motion(GuiContext *gui, Terminal *terminal, XMotionEvent *ev) {
  int cell_x = (ev->x - gui->surface.margin) / gui->fonts.char_width;
  int cell_y = (ev->y - gui->surface.margin) / gui->fonts.char_height;
  if (cell_x < 0)
    cell_x = 0;
  if (cell_x >= terminal->dims.width)
    cell_x = terminal->dims.width - 1;
  if (cell_y < 0)
    cell_y = 0;
  if (cell_y >= terminal->dims.height)
    cell_y = terminal->dims.height - 1;
  bool shift = (ev->state & ShiftMask) != 0;

  if (terminal->modes.mouse_mode >= 1 && !shift) {
    bool btn1 = (ev->state & Button1Mask) != 0;
    bool btn2 = (ev->state & Button2Mask) != 0;
    bool btn3 = (ev->state & Button3Mask) != 0;
    bool any_btn = btn1 || btn2 || btn3;
    bool should_report =
        (terminal->modes.mouse_mode >= 3) || (terminal->modes.mouse_mode >= 2 && any_btn);
    if (should_report) {
      int btn;
      if (btn1)
        btn = 32;
      else if (btn2)
        btn = 33;
      else if (btn3)
        btn = 34;
      else
        btn = 35;

      if (ev->state & Mod1Mask)
        btn |= 8;
      if (ev->state & ControlMask)
        btn |= 16;
      send_mouse_event(gui, terminal, btn, cell_x, cell_y, false);
      return;
    }
  }

  if (gui->selection.selecting) {
    Term_Screen *scr =
        terminal->screens.using_alt_screen ? &terminal->screens.alt_screen : &terminal->screens.screen;
    gui->selection.sel_cur_x = cell_x;
    gui->selection.sel_cur_y = scr->scrollback.count - scr->scroll_offset + cell_y;
    gui->selection.has_selection = true;
    build_selection_text(gui, terminal);
    draw_terminal(gui, terminal);
  }
}

static void on_selection_request(GuiContext *gui, XSelectionRequestEvent *req) {
  XSelectionEvent reply;
  memset(&reply, 0, sizeof(reply));
  reply.type = SelectionNotify;
  reply.display = req->display;
  reply.requestor = req->requestor;
  reply.selection = req->selection;
  reply.target = req->target;
  reply.property = None;
  reply.time = req->time;
  if (gui->selection.has_selection && gui->selection.selection_text &&
      (req->target == gui->selection.atom_utf8_string || req->target == XA_STRING)) {
    XChangeProperty(req->display, req->requestor, req->property, req->target, 8,
                    PropModeReplace, (unsigned char *)gui->selection.selection_text,
                    gui->selection.selection_len);
    reply.property = req->property;
  }
  XSendEvent(req->display, req->requestor, False, 0, (XEvent *)&reply);
}

static void on_selection_notify(GuiContext *gui, Terminal *terminal,
                                XSelectionEvent *ev) {
  if (ev->property == None)
    return;
  Atom actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  unsigned char *data = NULL;
  XGetWindowProperty(gui->x11.display, gui->x11.window, gui->selection.atom_xsel_data, 0, 65536,
                     True, AnyPropertyType, &actual_type, &actual_format,
                     &nitems, &bytes_after, &data);
  if (data) {
    if (terminal->modes.bracketed_paste)
      write(gui->process.pipe_fd, "\x1b[200~", 6);
    write(gui->process.pipe_fd, data, nitems);
    if (terminal->modes.bracketed_paste)
      write(gui->process.pipe_fd, "\x1b[201~", 6);
    XFree(data);
  }
}

void handle_events(GuiContext *gui, Terminal *terminal, XEvent *event) {
  switch (event->type) {
  case Expose:
    draw_terminal(gui, terminal);
    break;
  case ConfigureNotify:
    on_configure(gui, terminal, &event->xconfigure);
    break;
  case KeyPress:
    on_key_press(gui, terminal, &event->xkey);
    break;
  case ButtonPress:
    on_button_press(gui, terminal, &event->xbutton);
    break;
  case ButtonRelease:
    on_button_release(gui, terminal, &event->xbutton);
    break;
  case MotionNotify:
    on_motion(gui, terminal, &event->xmotion);
    break;
  case SelectionRequest:
    on_selection_request(gui, &event->xselectionrequest);
    break;
  case SelectionNotify:
    on_selection_notify(gui, terminal, &event->xselection);
    break;
  }
}
