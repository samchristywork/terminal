#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#include "events.h"
#include "log.h"
#include "render.h"

static void send_mouse_event(GuiContext *gui, Terminal *terminal, int btn,
                             int x, int y, bool release) {
  char buf[32];
  int len;
  if (terminal->mouse_sgr) {
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
  write(gui->pipe_fd, buf, len);
}

static void on_configure(GuiContext *gui, Terminal *terminal,
                         XConfigureEvent *ev) {
  int new_width = ev->width;
  int new_height = ev->height;

  if (new_width == gui->window_width && new_height == gui->window_height)
    return;

  LOG_DEBUG_MSG("Window resized to %dx%d", new_width, new_height);
  gui->window_width = new_width;
  gui->window_height = new_height;

  XFreePixmap(gui->display, gui->backbuffer);
  gui->backbuffer =
      XCreatePixmap(gui->display, gui->window, gui->window_width,
                    gui->window_height, DefaultDepth(gui->display, gui->screen));

  XftDrawDestroy(gui->xft_draw);
  gui->xft_draw = XftDrawCreate(gui->display, gui->backbuffer,
                                DefaultVisual(gui->display, gui->screen),
                                DefaultColormap(gui->display, gui->screen));

  int term_cols = (new_width - 20) / gui->char_width;
  int term_rows = (new_height - 20) / gui->char_height;
  if (term_cols < 1)
    term_cols = 1;
  if (term_rows < 1)
    term_rows = 1;

  LOG_DEBUG_MSG("Terminal resized to %dx%d", term_cols, term_rows);
  resize_terminal(terminal, term_cols, term_rows);

  struct winsize ws = {
      .ws_row = term_rows, .ws_col = term_cols,
      .ws_xpixel = 0,      .ws_ypixel = 0,
  };
  ioctl(gui->pipe_fd, TIOCSWINSZ, &ws);
  kill(gui->child_pid, SIGWINCH);

  draw_terminal(gui, terminal);
}

static void on_key_press(GuiContext *gui, Terminal *terminal, XKeyEvent *ev) {
  gui->cursor_visible = true;
  clock_gettime(CLOCK_MONOTONIC, &gui->last_blink);

  char buffer[32];
  KeySym keysym;
  XLookupString(ev, buffer, sizeof(buffer), &keysym, NULL);

  Term_Screen *scr =
      terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;
  int max_scroll = scr->scrollback.count;

  if (keysym == XK_Prior && (ev->state & ShiftMask)) {
    scr->scroll_offset += terminal->height;
    if (scr->scroll_offset > max_scroll)
      scr->scroll_offset = max_scroll;
    draw_terminal(gui, terminal);
  } else if (keysym == XK_Next && (ev->state & ShiftMask)) {
    scr->scroll_offset -= terminal->height;
    if (scr->scroll_offset < 0)
      scr->scroll_offset = 0;
    draw_terminal(gui, terminal);
  } else if (keysym == XK_c && (ev->state & ControlMask) &&
             (ev->state & ShiftMask)) {
    if (gui->has_selection)
      XSetSelectionOwner(gui->display, gui->atom_clipboard, gui->window,
                         CurrentTime);
  } else if (keysym == XK_v && (ev->state & ControlMask) &&
             (ev->state & ShiftMask)) {
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
    write(gui->pipe_fd,
          (ev->state & ControlMask) ? "\x1b[1;5A" : "\x1b[A",
          (ev->state & ControlMask) ? 6 : 3);
  } else if (keysym == XK_Down) {
    write(gui->pipe_fd,
          (ev->state & ControlMask) ? "\x1b[1;5B" : "\x1b[B",
          (ev->state & ControlMask) ? 6 : 3);
  } else if (keysym == XK_Right) {
    write(gui->pipe_fd,
          (ev->state & ControlMask) ? "\x1b[1;5C" : "\x1b[C",
          (ev->state & ControlMask) ? 6 : 3);
  } else if (keysym == XK_Left) {
    write(gui->pipe_fd,
          (ev->state & ControlMask) ? "\x1b[1;5D" : "\x1b[D",
          (ev->state & ControlMask) ? 6 : 3);
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
    write(gui->pipe_fd, fkeys[keysym - XK_F1],
          strlen(fkeys[keysym - XK_F1]));
  } else {
    int len = XLookupString(ev, buffer, sizeof(buffer), NULL, NULL);
    if (len > 0) {
      if (ev->state & Mod1Mask) {
        char alt_buf[33];
        alt_buf[0] = '\x1b';
        memcpy(&alt_buf[1], buffer, len);
        write(gui->pipe_fd, alt_buf, len + 1);
      } else {
        write(gui->pipe_fd, buffer, len);
      }
    }
  }
}

static void on_button_press(GuiContext *gui, Terminal *terminal,
                            XButtonEvent *ev) {
  Term_Screen *scr =
      terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;
  int max_scroll = scr->scrollback.count;
  int cell_x = (ev->x - 10) / gui->char_width;
  int cell_y = (ev->y - 10) / gui->char_height;
  if (cell_x < 0) cell_x = 0;
  if (cell_x >= terminal->width) cell_x = terminal->width - 1;
  if (cell_y < 0) cell_y = 0;
  if (cell_y >= terminal->height) cell_y = terminal->height - 1;
  bool shift = (ev->state & ShiftMask) != 0;

  if (terminal->mouse_mode >= 1 && !shift) {
    int btn = -1;
    switch (ev->button) {
    case Button1: btn = 0;  break;
    case Button2: btn = 1;  break;
    case Button3: btn = 2;  break;
    case Button4: btn = 64; break;
    case Button5: btn = 65; break;
    }
    if (btn >= 0) {
      if (ev->state & Mod1Mask) btn |= 8;
      if (ev->state & ControlMask) btn |= 16;
      send_mouse_event(gui, terminal, btn, cell_x, cell_y, false);
      return;
    }
  }

  if (ev->button == Button1) {
    int anchor_row = scr->scrollback.count - scr->scroll_offset + cell_y;
    gui->selecting = true;
    gui->has_selection = false;
    gui->sel_anchor_x = cell_x;
    gui->sel_anchor_y = anchor_row;
    gui->sel_cur_x = cell_x;
    gui->sel_cur_y = anchor_row;
    draw_terminal(gui, terminal);
  } else if (ev->button == Button2) {
    XConvertSelection(gui->display, XA_PRIMARY, gui->atom_utf8_string,
                      gui->atom_xsel_data, gui->window, CurrentTime);
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
  if (ev->button != Button1)
    return;

  bool shift = (ev->state & ShiftMask) != 0;

  if (terminal->mouse_mode >= 1 && !shift) {
    int cell_x = (ev->x - 10) / gui->char_width;
    int cell_y = (ev->y - 10) / gui->char_height;
    if (cell_x < 0) cell_x = 0;
    if (cell_x >= terminal->width) cell_x = terminal->width - 1;
    if (cell_y < 0) cell_y = 0;
    if (cell_y >= terminal->height) cell_y = terminal->height - 1;
    int btn = 0;
    if (ev->state & Mod1Mask) btn |= 8;
    if (ev->state & ControlMask) btn |= 16;
    send_mouse_event(gui, terminal, btn, cell_x, cell_y, true);
    return;
  }

  if (gui->selecting) {
    Term_Screen *scr =
        terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;
    int cell_x = (ev->x - 10) / gui->char_width;
    int cell_y = (ev->y - 10) / gui->char_height;
    if (cell_x < 0) cell_x = 0;
    if (cell_x >= terminal->width) cell_x = terminal->width - 1;
    if (cell_y < 0) cell_y = 0;
    if (cell_y >= terminal->height) cell_y = terminal->height - 1;
    int cur_row = scr->scrollback.count - scr->scroll_offset + cell_y;
    gui->sel_cur_x = cell_x;
    gui->sel_cur_y = cur_row;
    gui->selecting = false;
    gui->has_selection =
        (gui->sel_anchor_x != cell_x || gui->sel_anchor_y != cur_row);
    if (gui->has_selection) {
      build_selection_text(gui, terminal);
      XSetSelectionOwner(gui->display, XA_PRIMARY, gui->window, CurrentTime);
    }
    draw_terminal(gui, terminal);
  }
}

static void on_motion(GuiContext *gui, Terminal *terminal, XMotionEvent *ev) {
  int cell_x = (ev->x - 10) / gui->char_width;
  int cell_y = (ev->y - 10) / gui->char_height;
  if (cell_x < 0) cell_x = 0;
  if (cell_x >= terminal->width) cell_x = terminal->width - 1;
  if (cell_y < 0) cell_y = 0;
  if (cell_y >= terminal->height) cell_y = terminal->height - 1;
  bool shift = (ev->state & ShiftMask) != 0;

  if (terminal->mouse_mode >= 1 && !shift) {
    bool btn1 = (ev->state & Button1Mask) != 0;
    bool btn2 = (ev->state & Button2Mask) != 0;
    bool btn3 = (ev->state & Button3Mask) != 0;
    bool any_btn = btn1 || btn2 || btn3;
    bool should_report =
        (terminal->mouse_mode >= 3) || (terminal->mouse_mode >= 2 && any_btn);
    if (should_report) {
      int btn = btn2 ? 33 : btn3 ? 34 : 32;
      if (ev->state & Mod1Mask) btn |= 8;
      if (ev->state & ControlMask) btn |= 16;
      send_mouse_event(gui, terminal, btn, cell_x, cell_y, false);
      return;
    }
  }

  if (gui->selecting) {
    Term_Screen *scr =
        terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;
    gui->sel_cur_x = cell_x;
    gui->sel_cur_y = scr->scrollback.count - scr->scroll_offset + cell_y;
    gui->has_selection = true;
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
  if (gui->has_selection && gui->selection_text &&
      (req->target == gui->atom_utf8_string || req->target == XA_STRING)) {
    XChangeProperty(req->display, req->requestor, req->property, req->target,
                    8, PropModeReplace, (unsigned char *)gui->selection_text,
                    gui->selection_len);
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
  XGetWindowProperty(gui->display, gui->window, gui->atom_xsel_data, 0, 65536,
                     True, AnyPropertyType, &actual_type, &actual_format,
                     &nitems, &bytes_after, &data);
  if (data) {
    if (terminal->bracketed_paste)
      write(gui->pipe_fd, "\x1b[200~", 6);
    write(gui->pipe_fd, data, nitems);
    if (terminal->bracketed_paste)
      write(gui->pipe_fd, "\x1b[201~", 6);
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
