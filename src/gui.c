#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
  Display *display;
  Window window;
  GC gc;
  XEvent event;
  XFontStruct *font_info;
  const char *text = "Testing X11 GUI";
  int screen;
  unsigned long black, white;
  int running = 1;

  display = XOpenDisplay(NULL);
  if (display == NULL) {
    fprintf(stderr, "Cannot open display\n");
    return 1;
  }

  screen = DefaultScreen(display);
  black = BlackPixel(display, screen);
  white = WhitePixel(display, screen);

  window = XCreateSimpleWindow(display, RootWindow(display, screen), 100, 100,
                               400, 200, 0, white, black);

  XSelectInput(display, window, ExposureMask | KeyPressMask | ButtonPressMask);

  XStoreName(display, window, "X11 Window");

  gc = XCreateGC(display, window, 0, NULL);
  XSetForeground(display, gc, white);
  XSetBackground(display, gc, black);

  font_info = XLoadQueryFont(display, "fixed");
  if (!font_info) {
    fprintf(stderr, "Cannot load font: fixed\n");
    return 1;
  }
  XSetFont(display, gc, font_info->fid);

  XMapWindow(display, window);

  while (running) {
    XNextEvent(display, &event);

    switch (event.type) {
    case Expose:
      XClearWindow(display, window);
      XDrawString(display, window, gc, 50, 100, text, strlen(text));
      break;
    case KeyPress:
    case ButtonPress:
      running = 0;
      break;
    }
  }

  XFreeGC(display, gc);
  XUnloadFont(display, font_info->fid);
  XDestroyWindow(display, window);
  XCloseDisplay(display);

  return 0;
}
