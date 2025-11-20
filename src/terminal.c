#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "screen.h"
#include "terminal.h"
#include "tokenize.h"

static void handle_erase(Term_Screen *screen, Term_TokenType type,
                         int width, int height);

static void terminal_respond(Terminal *t, const char *data, int len) {
  int available = (int)sizeof(t->response_buf) - t->response_len;
  if (len > available)
    len = available;
  memcpy(t->response_buf + t->response_len, data, len);
  t->response_len += len;
}

static void handle_field(Term_Cursor **cursor, int value) {
  if (value == 0) {
    (*cursor)->attr.fg.color = 0;
    (*cursor)->attr.fg.type = COLOR_DEFAULT;
    (*cursor)->attr.bg.color = 0;
    (*cursor)->attr.bg.type = COLOR_DEFAULT;
    (*cursor)->attr.bold = 0;
    (*cursor)->attr.dim = 0;
    (*cursor)->attr.italic = 0;
    (*cursor)->attr.underline = 0;
    (*cursor)->attr.reverse = 0;
  } else if (value == 1) {
    (*cursor)->attr.bold = 1;
  } else if (value == 2) {
    (*cursor)->attr.dim = 1;
  } else if (value == 3) {
    (*cursor)->attr.italic = 1;
  } else if (value == 4) {
    (*cursor)->attr.underline = 1;
  } else if (value == 7) {
    (*cursor)->attr.reverse = 1;
  } else if (value >= 30 && value <= 37) {
    (*cursor)->attr.fg.type = COLOR_DEFAULT;
    (*cursor)->attr.fg.color = value;
  } else if (value >= 40 && value <= 47) {
    (*cursor)->attr.bg.type = COLOR_DEFAULT;
    (*cursor)->attr.bg.color = value;
  } else if (value >= 90 && value <= 97) {
    (*cursor)->attr.fg.type = COLOR_BRIGHT;
    (*cursor)->attr.fg.color = value;
  } else if (value >= 100 && value <= 107) {
    (*cursor)->attr.bg.type = COLOR_BRIGHT;
    (*cursor)->attr.bg.color = value;
  } else if (value == 22) {
    (*cursor)->attr.bold = 0;
    (*cursor)->attr.dim = 0;
  } else if (value == 23) {
    (*cursor)->attr.italic = 0;
  } else if (value == 24) {
    (*cursor)->attr.underline = 0;
  } else if (value == 27) {
    (*cursor)->attr.reverse = 0;
  }
}

static void modify_cursor(Term_Cursor **cursor, Term_Token token) {
  if (starts_with(token.value, token.length, "\x1b[38;5;")) {
    if (token.length < 8)
      return;
    char num_str[16];
    int num_len = token.length - 8;
    if (num_len >= (int)sizeof(num_str))
      num_len = (int)sizeof(num_str) - 1;
    memcpy(num_str, &token.value[7], num_len);
    num_str[num_len] = '\0';
    int num = atoi(num_str);
    if (num < 0 || num > 255)
      return;
    (*cursor)->attr.fg.type = COLOR_256;
    (*cursor)->attr.fg.color = num;
  } else if (starts_with(token.value, token.length, "\x1b[48;5;")) {
    if (token.length < 8)
      return;
    char num_str[16];
    int num_len = token.length - 8;
    if (num_len >= (int)sizeof(num_str))
      num_len = (int)sizeof(num_str) - 1;
    memcpy(num_str, &token.value[7], num_len);
    num_str[num_len] = '\0';
    int num = atoi(num_str);
    if (num < 0 || num > 255)
      return;
    (*cursor)->attr.bg.type = COLOR_256;
    (*cursor)->attr.bg.color = num;
  } else if (starts_with(token.value, token.length, "\x1b[38;2;")) {
    char params[32];
    int params_len = token.length - 8;
    if (params_len <= 0 || params_len >= (int)sizeof(params))
      return;
    memcpy(params, &token.value[7], params_len);
    params[params_len] = '\0';
    int r, g, b;
    if (sscanf(params, "%d;%d;%d", &r, &g, &b) == 3) {
      (*cursor)->attr.fg.type = COLOR_RGB;
      (*cursor)->attr.fg.rgb.red = r < 0 ? 0 : (r > 255 ? 255 : r);
      (*cursor)->attr.fg.rgb.green = g < 0 ? 0 : (g > 255 ? 255 : g);
      (*cursor)->attr.fg.rgb.blue = b < 0 ? 0 : (b > 255 ? 255 : b);
    }
  } else if (starts_with(token.value, token.length, "\x1b[48;2;")) {
    char params[32];
    int params_len = token.length - 8;
    if (params_len <= 0 || params_len >= (int)sizeof(params))
      return;
    memcpy(params, &token.value[7], params_len);
    params[params_len] = '\0';
    int r, g, b;
    if (sscanf(params, "%d;%d;%d", &r, &g, &b) == 3) {
      (*cursor)->attr.bg.type = COLOR_RGB;
      (*cursor)->attr.bg.rgb.red = r < 0 ? 0 : (r > 255 ? 255 : r);
      (*cursor)->attr.bg.rgb.green = g < 0 ? 0 : (g > 255 ? 255 : g);
      (*cursor)->attr.bg.rgb.blue = b < 0 ? 0 : (b > 255 ? 255 : b);
    }
  } else if (ends_with(token.value, token.length, 'm')) {
    if (token.length < 3)
      return;
    for (int i = 2; i < token.length - 1;) {
      int j = i;
      while (j < token.length - 1 && token.value[j] != ';')
        j++;
      char num_str[16];
      int num_len = j - i;
      if (num_len >= (int)sizeof(num_str))
        num_len = (int)sizeof(num_str) - 1;
      memcpy(num_str, &token.value[i], num_len);
      num_str[num_len] = '\0';
      int num = atoi(num_str);
      handle_field(cursor, num);
      i = j + 1;
    }
  } else if (ends_with(token.value, token.length, 'H') ||
             ends_with(token.value, token.length, 'f')) {
    const char *p = token.value + 2;
    const char *end = token.value + token.length - 1;
    int row = 0, col = 0;
    while (p < end && *p != ';')
      row = row * 10 + (*p++ - '0');
    if (p < end)
      p++;
    while (p < end)
      col = col * 10 + (*p++ - '0');
    if (row < 1)
      row = 1;
    if (col < 1)
      col = 1;
    (*cursor)->y = row - 1;
    (*cursor)->x = col - 1;
  } else {
    char repr[128];
    token_repr(&token, repr, sizeof(repr));
    LOG_WARNING_MSG("unhandled CSI sequence: %s", repr);
  }
}

static int csi_param(Term_Token token, int default_val) {
  if (token.length < 3)
    return default_val;
  char buf[32];
  int param_len = token.length - 3;
  if (param_len <= 0)
    return default_val;
  if (param_len >= (int)sizeof(buf))
    param_len = (int)sizeof(buf) - 1;
  memcpy(buf, &token.value[2], param_len);
  buf[param_len] = '\0';
  int val = atoi(buf);
  return val <= 0 ? default_val : val;
}

static int incomplete_escape_tail(const char *buf, int len) {
  int i = len - 1;
  while (i >= 0 && (unsigned char)buf[i] != 0x1b)
    i--;
  if (i < 0)
    return 0;

  int have = len - i;
  const char *p = buf + i;

  if (have == 1)
    return 1;

  unsigned char next = (unsigned char)p[1];
  if (next == '[') {
    int j = 2;
    while (j < have && (unsigned char)p[j] >= 0x30 &&
           (unsigned char)p[j] <= 0x3f)
      j++;
    while (j < have && (unsigned char)p[j] >= 0x20 &&
           (unsigned char)p[j] <= 0x2f)
      j++;
    if (j < have && (unsigned char)p[j] >= 0x40 && (unsigned char)p[j] <= 0x7e)
      return 0;
    return have;
  } else if (next == ']') {
    for (int j = 2; j < have; j++) {
      if ((unsigned char)p[j] == 0x07)
        return 0;
      if (j + 1 < have && p[j] == '\x1b' && p[j + 1] == '\\')
        return 0;
    }
    return have;
  } else if (next == '(' || next == ')' || next == '*' || next == '+') {
    return (have >= 3) ? 0 : have;
  }
  return 0;
}

static void handle_csi(Terminal *terminal, Term_Screen *screen, Term_Token token) {
  int width = terminal->width;
  int height = terminal->height;
  Term_Cursor *cursor = &screen->cursor;
  char final = token.value[token.length - 1];
  int n = csi_param(token, 1);

  if (final == 'A') {
    cursor->y -= n;
    if (cursor->y < 0)
      cursor->y = 0;
  } else if (final == 'B') {
    cursor->y += n;
    if (cursor->y >= height)
      cursor->y = height - 1;
  } else if (final == 'C') {
    cursor->x += n;
    if (cursor->x >= width)
      cursor->x = width - 1;
  } else if (final == 'D') {
    cursor->x -= n;
    if (cursor->x < 0)
      cursor->x = 0;
  } else if (final == 'E') {
    cursor->y += n;
    if (cursor->y >= height)
      cursor->y = height - 1;
    cursor->x = 0;
  } else if (final == 'F') {
    cursor->y -= n;
    if (cursor->y < 0)
      cursor->y = 0;
    cursor->x = 0;
  } else if (final == 'G') {
    cursor->x = n - 1;
    if (cursor->x < 0)
      cursor->x = 0;
    if (cursor->x >= width)
      cursor->x = width - 1;
  } else if (final == 'd') {
    cursor->y = n - 1;
    if (cursor->y < 0)
      cursor->y = 0;
    if (cursor->y >= height)
      cursor->y = height - 1;
  } else if (final == '7' || final == 's') {
    screen->saved_cursor = *cursor;
  } else if (final == '8' || final == 'u') {
    *cursor = screen->saved_cursor;
  } else if (final == 'L') {
    int bot = screen->scroll_bot;
    int rows = n < (bot - cursor->y + 1) ? n : (bot - cursor->y + 1);
    for (int j = bot; j >= cursor->y + rows; j--)
      memcpy(screen->lines[j].cells, screen->lines[j - rows].cells,
             width * sizeof(Term_Cell));
    for (int j = cursor->y; j < cursor->y + rows; j++)
      for (int k = 0; k < width; k++)
        memset(&screen->lines[j].cells[k], 0, sizeof(Term_Cell));
  } else if (final == 'M') {
    int bot = screen->scroll_bot;
    int rows = n < (bot - cursor->y + 1) ? n : (bot - cursor->y + 1);
    for (int j = cursor->y; j <= bot - rows; j++)
      memcpy(screen->lines[j].cells, screen->lines[j + rows].cells,
             width * sizeof(Term_Cell));
    for (int j = bot - rows + 1; j <= bot; j++)
      for (int k = 0; k < width; k++)
        memset(&screen->lines[j].cells[k], 0, sizeof(Term_Cell));
  } else if (final == 'r') {
    const char *p = token.value + 2;
    const char *end = token.value + token.length - 1;
    int top = 0, bot = 0;
    while (p < end && *p != ';')
      top = top * 10 + (*p++ - '0');
    if (p < end)
      p++;
    while (p < end)
      bot = bot * 10 + (*p++ - '0');
    if (top < 1)
      top = 1;
    if (bot < 1 || bot > height)
      bot = height;
    if (top < bot) {
      screen->scroll_top = top - 1;
      screen->scroll_bot = bot - 1;
    }
    cursor->x = 0;
    cursor->y = 0;
  } else if (final == '@') {
    int cols = (n < width - cursor->x) ? n : width - cursor->x;
    int move = width - cursor->x - cols;
    if (move > 0)
      memmove(&screen->lines[cursor->y].cells[cursor->x + cols],
              &screen->lines[cursor->y].cells[cursor->x],
              move * sizeof(Term_Cell));
    for (int k = cursor->x; k < cursor->x + cols; k++)
      memset(&screen->lines[cursor->y].cells[k], 0, sizeof(Term_Cell));
  } else if (final == 'P') {
    int cols = (n < width - cursor->x) ? n : width - cursor->x;
    int move = width - cursor->x - cols;
    if (move > 0)
      memmove(&screen->lines[cursor->y].cells[cursor->x],
              &screen->lines[cursor->y].cells[cursor->x + cols],
              move * sizeof(Term_Cell));
    for (int k = width - cols; k < width; k++)
      memset(&screen->lines[cursor->y].cells[k], 0, sizeof(Term_Cell));
  } else if (final == 'X') {
    int cols = (n < width - cursor->x) ? n : width - cursor->x;
    for (int k = cursor->x; k < cursor->x + cols; k++)
      memset(&screen->lines[cursor->y].cells[k], 0, sizeof(Term_Cell));
  } else if (final == 'J') {
    int nj = csi_param(token, 0);
    if (nj == 0) {
      handle_erase(screen, TOKEN_ERASE_DOWN, width, height);
    } else if (nj == 1) {
      handle_erase(screen, TOKEN_ERASE_UP, width, height);
    } else if (nj == 2) {
      handle_erase(screen, TOKEN_ERASE_ALL, width, height);
    } else if (nj == 3) {
      handle_erase(screen, TOKEN_ERASE_SCROLLBACK, width, height);
    }
  } else if (final == 'K') {
    int nk = csi_param(token, 0);
    if (nk == 0) {
      handle_erase(screen, TOKEN_ERASE_EOL, width, height);
    } else if (nk == 1) {
      handle_erase(screen, TOKEN_ERASE_SOL, width, height);
    } else if (nk == 2) {
      handle_erase(screen, TOKEN_ERASE_LINE, width, height);
    }
  } else if (final == 'h' || final == 'l') {
    bool enable = (final == 'h');
    if (starts_with(token.value, token.length, "\x1b[?1000"))
      terminal->mouse_mode = enable ? 1 : 0;
    else if (starts_with(token.value, token.length, "\x1b[?1002"))
      terminal->mouse_mode = enable ? 2 : 0;
    else if (starts_with(token.value, token.length, "\x1b[?1003"))
      terminal->mouse_mode = enable ? 3 : 0;
    else if (starts_with(token.value, token.length, "\x1b[?1006"))
      terminal->mouse_sgr = enable;
    else if (starts_with(token.value, token.length, "\x1b[?1049"))
      terminal->using_alt_screen = enable;
    else if (starts_with(token.value, token.length, "\x1b[?25"))
      screen->cursor_hidden = !enable;
    else if (starts_with(token.value, token.length, "\x1b[?2004"))
      terminal->bracketed_paste = enable;
  }
 else if (final == 't') {
    const char *p = token.value + 2;
    const char *end = token.value + token.length - 1;
    int n1 = 0, n2 = 0;
    while (p < end && *p >= '0' && *p <= '9')
      n1 = n1 * 10 + (*p++ - '0');
    if (p < end && *p == ';') {
      p++;
      while (p < end && *p >= '0' && *p <= '9')
        n2 = n2 * 10 + (*p++ - '0');
    }
    if (n1 == 22) {
      if (n2 == 0 || n2 == 2) {
        if (terminal->window_title_stack_depth < 32) {
          memcpy(terminal->window_title_stack[terminal->window_title_stack_depth],
                 terminal->window_title, 256);
          terminal->window_title_stack_depth++;
        }
      }
      if (n2 == 0 || n2 == 1) {
        if (terminal->icon_name_stack_depth < 32) {
          memcpy(terminal->icon_name_stack[terminal->icon_name_stack_depth],
                 terminal->icon_name, 256);
          terminal->icon_name_stack_depth++;
        }
      }
    } else if (n1 == 23) {
      if (n2 == 0 || n2 == 2) {
        if (terminal->window_title_stack_depth > 0) {
          terminal->window_title_stack_depth--;
          memcpy(terminal->window_title,
                 terminal->window_title_stack[terminal->window_title_stack_depth],
                 256);
          terminal->title_dirty = true;
        }
      }
      if (n2 == 0 || n2 == 1) {
        if (terminal->icon_name_stack_depth > 0) {
          terminal->icon_name_stack_depth--;
          memcpy(terminal->icon_name,
                 terminal->icon_name_stack[terminal->icon_name_stack_depth], 256);
          terminal->title_dirty = true;
        }
      }
    }
  } else if (final == 'n') {
    if (n == 6) {
      char buf[32];
      int len = snprintf(buf, sizeof(buf), "\x1b[%d;%dR",
                         cursor->y + 1, cursor->x + 1);
      terminal_respond(terminal, buf, len);
    }
  } else if (final == 'c') {
    char buf[32];
    int len;
    if (starts_with(token.value, token.length, "\x1b[>"))
      len = snprintf(buf, sizeof(buf), "\x1b[>0;0;0c");
    else
      len = snprintf(buf, sizeof(buf), "\x1b[?1;0c");
    terminal_respond(terminal, buf, len);
  } else if (final == 'q') {
    if (token.length >= 3 && token.value[token.length - 2] == ' ')
      terminal->cursor_shape = csi_param(token, 0);
  } else {
    modify_cursor(&cursor, token);
  }
}

static bool parse_color(const char *val, int len, unsigned long *rgb) {
  if (len < 1)
    return false;

  if (val[0] == '#') {
    if (len == 4) { // #RGB
      char r[2] = {val[1], val[1]};
      char g[2] = {val[2], val[2]};
      char b[2] = {val[3], val[3]};
      unsigned long rv = strtoul(r, NULL, 16);
      unsigned long gv = strtoul(g, NULL, 16);
      unsigned long bv = strtoul(b, NULL, 16);
      *rgb = (rv << 16) | (gv << 8) | bv;
      return true;
    } else if (len == 7) { // #RRGGBB
      char hex[7];
      memcpy(hex, val + 1, 6);
      hex[6] = '\0';
      char *end;
      *rgb = strtoul(hex, &end, 16);
      return end == hex + 6;
    } else if (len == 10) { // #RRRGGGBBB
      char r[3], g[3], b[3];
      memcpy(r, val + 1, 3);
      memcpy(g, val + 4, 3);
      memcpy(b, val + 7, 3);
      *rgb = ((strtoul(r, NULL, 16) >> 4) << 16) |
             ((strtoul(g, NULL, 16) >> 4) << 8) | (strtoul(b, NULL, 16) >> 4);
      return true;
    } else if (len == 13) { // #RRRRGGGGBBBB
      char r[3], g[3], b[3];
      memcpy(r, val + 1, 2);
      memcpy(g, val + 5, 2);
      memcpy(b, val + 9, 2);
      *rgb = (strtoul(r, NULL, 16) << 16) | (strtoul(g, NULL, 16) << 8) |
             strtoul(b, NULL, 16);
      return true;
    }
  } else if (len >= 7 && strncmp(val, "rgb:", 4) == 0) {
    // rgb:R/G/B, rgb:RR/GG/BB, rgb:RRR/GGG/BBB, rgb:RRRR/GGGG/BBBB
    char r_str[8], g_str[8], b_str[8];
    const char *p = val + 4;
    const char *slash1 = NULL;
    for (int k = 0; k < len - 4; k++) {
      if (p[k] == '/') {
        slash1 = p + k;
        break;
      }
    }
    if (!slash1)
      return false;
    int r_len = slash1 - p;
    if (r_len < 1 || r_len > 4)
      return false;
    memcpy(r_str, p, r_len);
    r_str[r_len] = '\0';

    p = slash1 + 1;
    const char *slash2 = NULL;
    int rem = len - (p - val);
    for (int k = 0; k < rem; k++) {
      if (p[k] == '/') {
        slash2 = p + k;
        break;
      }
    }
    if (!slash2)
      return false;
    int g_len = slash2 - p;
    if (g_len < 1 || g_len > 4)
      return false;
    memcpy(g_str, p, g_len);
    g_str[g_len] = '\0';

    p = slash2 + 1;
    int b_len = len - (p - val);
    if (b_len < 1 || b_len > 4)
      return false;
    memcpy(b_str, p, b_len);
    b_str[b_len] = '\0';

    unsigned long r = strtoul(r_str, NULL, 16);
    unsigned long g = strtoul(g_str, NULL, 16);
    unsigned long b = strtoul(b_str, NULL, 16);

    if (r_len == 1) r = (r << 4) | r; else if (r_len > 2) r >>= (r_len - 2) * 4;
    if (g_len == 1) g = (g << 4) | g; else if (g_len > 2) g >>= (g_len - 2) * 4;
    if (b_len == 1) b = (b << 4) | b; else if (b_len > 2) b >>= (b_len - 2) * 4;

    *rgb = (r << 16) | (g << 8) | b;
    return true;
  }

  return false;
}

static void handle_osc(Terminal *terminal, Term_Token token) {
  int i = 2;
  int cmd = 0;
  while (i < token.length && token.value[i] >= '0' && token.value[i] <= '9')
    cmd = cmd * 10 + (token.value[i++] - '0');
  if (i < token.length && token.value[i] == ';')
    i++;

  if (cmd == 0 || cmd == 1 || cmd == 2) {
    LOG_DEBUG_MSG("OSC title/icon cmd %d, token len %d", cmd, token.length);
    int text_start = i;
    int text_end = token.length - 1;
    if (text_end > text_start && (unsigned char)token.value[text_end] == 0x07)
      text_end--;
    else if (text_end > text_start && token.value[text_end] == '\\' &&
             token.value[text_end - 1] == '\x1b')
      text_end -= 2;
    int tlen = text_end - text_start + 1;
    if (tlen < 0)
      tlen = 0;

    if (cmd == 0 || cmd == 2) {
      if (tlen >= (int)sizeof(terminal->window_title))
        tlen = sizeof(terminal->window_title) - 1;
      memcpy(terminal->window_title, &token.value[text_start], tlen);
      terminal->window_title[tlen] = '\0';
      terminal->title_dirty = true;
    }
    if (cmd == 0 || cmd == 1) {
      if (tlen >= (int)sizeof(terminal->icon_name))
        tlen = sizeof(terminal->icon_name) - 1;
      memcpy(terminal->icon_name, &token.value[text_start], tlen);
      terminal->icon_name[tlen] = '\0';
    }
  } else if (cmd == 10 || cmd == 11) {
    const char *val = &token.value[i];
    int text_start = i;
    int text_end = token.length - 1;
    if (text_end > text_start && (unsigned char)token.value[text_end] == 0x07)
      text_end--;
    else if (text_end > text_start && token.value[text_end] == '\\' &&
             token.value[text_end - 1] == '\x1b')
      text_end -= 2;
    int tlen = text_end - text_start + 1;

    if (tlen >= 1 && val[0] == '?') {
      unsigned long rgb = (cmd == 10) ? terminal->osc_fg : terminal->osc_bg;
      unsigned int r = (rgb >> 16) & 0xff;
      unsigned int g = (rgb >> 8) & 0xff;
      unsigned int b = rgb & 0xff;
      char buf[64];
      int len = snprintf(buf, sizeof(buf),
                         "\x1b]%d;rgb:%02x%02x/%02x%02x/%02x%02x\x07", cmd, r,
                         r, g, g, b, b);
      terminal_respond(terminal, buf, len);
    } else {
      unsigned long rgb;
      if (parse_color(val, tlen, &rgb)) {
        if (cmd == 10) {
          terminal->osc_fg = rgb;
          terminal->fg_dirty = true;
        } else {
          terminal->osc_bg = rgb;
          terminal->bg_dirty = true;
        }
      }
    }
  } else if (cmd == 7 || cmd == 133) {

    // OSC 7: current working directory notification
    // OSC 133: shell integration (FinalTerm semantic zones)
  } else {
    char repr[128];
    token_repr(&token, repr, sizeof(repr));
    LOG_WARNING_MSG("unhandled OSC command %d: %s", cmd, repr);
  }
}

static void handle_erase(Term_Screen *screen, Term_TokenType type,
                         int width, int height) {
  Term_Cursor *cursor = &screen->cursor;
  switch (type) {
  case TOKEN_ERASE_EOL:
    for (int j = cursor->x; j < width; j++)
      memset(&screen->lines[cursor->y].cells[j], 0, sizeof(Term_Cell));
    break;
  case TOKEN_ERASE_SOL:
    for (int j = 0; j <= cursor->x; j++)
      memset(&screen->lines[cursor->y].cells[j], 0, sizeof(Term_Cell));
    break;
  case TOKEN_ERASE_LINE:
    for (int j = 0; j < width; j++)
      memset(&screen->lines[cursor->y].cells[j], 0, sizeof(Term_Cell));
    break;
  case TOKEN_ERASE_DOWN:
    for (int j = cursor->y; j < height; j++)
      for (int k = 0; k < width; k++) {
        if (j == cursor->y && k < cursor->x)
          continue;
        memset(&screen->lines[j].cells[k], 0, sizeof(Term_Cell));
      }
    break;
  case TOKEN_ERASE_UP:
    for (int j = 0; j <= cursor->y; j++)
      for (int k = 0; k < width; k++) {
        if (j == cursor->y && k > cursor->x)
          continue;
        memset(&screen->lines[j].cells[k], 0, sizeof(Term_Cell));
      }
    break;
  case TOKEN_ERASE_ALL:
    for (int j = 0; j < height; j++)
      for (int k = 0; k < width; k++)
        memset(&screen->lines[j].cells[k], 0, sizeof(Term_Cell));
    break;
  default:
    break;
  }
}

void free_terminal(Terminal *terminal) {
  free_screen(&terminal->screen, terminal->height);
  free_screen(&terminal->alt_screen, terminal->height);
}

void init_terminal(Terminal *terminal, int width, int height, int scrollback_lines) {
  terminal->width = width;
  terminal->height = height;
  terminal->using_alt_screen = false;
  terminal->window_title[0] = '\0';
  terminal->icon_name[0] = '\0';
  terminal->title_dirty = false;
  terminal->partial_len = 0;
  terminal->bracketed_paste = false;
  terminal->mouse_mode = 0;
  terminal->mouse_sgr = false;
  terminal->osc_fg = 0xffffff;
  terminal->fg_dirty = false;
  terminal->osc_bg = 0;
  terminal->bg_dirty = false;
  terminal->default_fg_rgb = 0xffffff;
  terminal->cursor_shape = 0;
  terminal->response_len = 0;
  terminal->window_title_stack_depth = 0;
  terminal->icon_name_stack_depth = 0;
  init_screen(&terminal->screen, width, height, scrollback_lines);
  init_screen(&terminal->alt_screen, width, height, scrollback_lines);
}

void reset_terminal(Terminal *terminal) {
  reset_screen(&terminal->screen, terminal->width, terminal->height);
  reset_screen(&terminal->alt_screen, terminal->width, terminal->height);
  terminal->using_alt_screen = false;
  terminal->window_title[0] = '\0';
  terminal->icon_name[0] = '\0';
  terminal->title_dirty = false;
  terminal->partial_len = 0;
  terminal->bracketed_paste = false;
  terminal->mouse_mode = 0;
  terminal->mouse_sgr = false;
  terminal->osc_fg = 0xffffff;
  terminal->fg_dirty = false;
  terminal->osc_bg = 0;
  terminal->bg_dirty = false;
  terminal->response_len = 0;
  terminal->window_title_stack_depth = 0;
  terminal->icon_name_stack_depth = 0;
}

void resize_terminal(Terminal *terminal, int new_width, int new_height) {
  if (new_width <= 0 || new_height <= 0)
    return;
  if (terminal->width == new_width && terminal->height == new_height)
    return;

  int old_width = terminal->width;
  int old_height = terminal->height;

  resize_screen(&terminal->screen, old_width, old_height, new_width, new_height);
  resize_screen(&terminal->alt_screen, old_width, old_height, new_width, new_height);

  terminal->width = new_width;
  terminal->height = new_height;
}

static int incomplete_utf8_len(const char *buf, int len) {
  if (len <= 0)
    return 0;

  int i = len - 1;
  while (i >= 0 && (unsigned char)buf[i] >= 0x80 &&
         (unsigned char)buf[i] < 0xc0) {
    i--;
  }

  if (i < 0) {
    if (len > 0 && (unsigned char)buf[0] >= 0x80 && (unsigned char)buf[0] < 0xc0)
      return len > 3 ? 0 : len;
    return 0;
  }

  unsigned char c = (unsigned char)buf[i];
  int expected = 0;
  if (c < 0x80)
    return 0;
  else if ((c & 0xe0) == 0xc0)
    expected = 2;
  else if ((c & 0xf0) == 0xe0)
    expected = 3;
  else if ((c & 0xf8) == 0xf0)
    expected = 4;
  else
    return 0;

  int have = len - i;
  if (have < expected)
    return have;

  return 0;
}

void write_terminal(Terminal *terminal, const char *text, int length) {
  char *combined = NULL;
  int combined_len = length;

  if (terminal->partial_len > 0) {
    combined_len = terminal->partial_len + length;
    combined = malloc(combined_len);
    if (!combined)
      return;
    memcpy(combined, terminal->partial_buf, terminal->partial_len);
    memcpy(combined + terminal->partial_len, text, length);
    text = combined;
    terminal->partial_len = 0;
  }

  int tail = incomplete_escape_tail(text, combined_len);
  if (tail <= 0)
    tail = incomplete_utf8_len(text, combined_len);

  if (tail >= combined_len) {
    int save = tail < (int)sizeof(terminal->partial_buf)
                   ? tail : (int)sizeof(terminal->partial_buf) - 1;
    memcpy(terminal->partial_buf, text, save);
    terminal->partial_len = save;
    free(combined);
    return;
  }
  if (tail > 0) {
    int save = tail < (int)sizeof(terminal->partial_buf)
                   ? tail : (int)sizeof(terminal->partial_buf) - 1;
    memcpy(terminal->partial_buf, text + combined_len - tail, save);
    terminal->partial_len = save;
    combined_len -= tail;
  }

  Term_Tokens *tokens = tokenize(text, combined_len);
  int width = terminal->width;
  int height = terminal->height;

  Term_Screen *active =
      terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;
  active->scroll_offset = 0;

#ifdef DEBUG
  for (int i = 0; i < tokens->count; i++)
    print_token(tokens->tokens[i]);
#endif

  for (int i = 0; i < tokens->count; i++) {
    Term_Token token = tokens->tokens[i];
    Term_Screen *screen =
        terminal->using_alt_screen ? &terminal->alt_screen : &terminal->screen;
    Term_Cursor *cursor = &screen->cursor;

    switch (token.type) {
    case TOKEN_TEXT: {
      int j = 0;
      while (j < token.length) {
        unsigned char c = (unsigned char)token.value[j];
        int char_len;
        if (c < 0x80) char_len = 1;
        else if (c < 0xE0) char_len = 2;
        else if (c < 0xF0) char_len = 3;
        else char_len = 4;
        if (j + char_len > token.length)
          char_len = token.length - j;
        write_regular_cell(screen, &token.value[j], char_len, width, height,
                           cursor->attr);
        j += char_len;
      }
      break;
    }
    case TOKEN_NEWLINE:
      handle_newline(active, width, height);
      break;
    case TOKEN_CARRIAGE_RETURN:
      cursor->x = 0;
      break;
    case TOKEN_CSI_CODE:
      handle_csi(terminal, active, token);
      break;
    case TOKEN_FULL_RESET:
      reset_terminal(terminal);
      break;
    case TOKEN_TAB: {
      int next_tab_stop = ((cursor->x / 8) + 1) * 8;
      if (next_tab_stop >= width)
        handle_newline(active, width, height);
      else
        cursor->x = next_tab_stop;
      break;
    }
    case TOKEN_BACKSPACE:
      if (cursor->x > 0)
        cursor->x--;
      break;
    case TOKEN_REVERSE_INDEX:
      if (cursor->y > screen->scroll_top) {
        cursor->y--;
      } else if (cursor->y == screen->scroll_top) {
        for (int j = screen->scroll_bot; j > screen->scroll_top; j--)
          memcpy(screen->lines[j].cells, screen->lines[j - 1].cells,
                 width * sizeof(Term_Cell));
        memset(screen->lines[screen->scroll_top].cells, 0,
               width * sizeof(Term_Cell));
      } else {
        if (cursor->y > 0)
          cursor->y--;
      }
      break;
    case TOKEN_OSC:
      handle_osc(terminal, token);
      break;
    default: {
      char repr[128];
      token_repr(&token, repr, sizeof(repr));
      LOG_WARNING_MSG("unhandled token type %d: %s", token.type, repr);
      break;
    }
    }
  }

  free(tokens->tokens);
  free(tokens);
  free(combined);
}
