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

  XMapWindow(display, window);

  while (running) {
    XNextEvent(display, &event);

    switch (event.type) {
    case Expose:
      XClearWindow(display, window);
      break;
    case KeyPress:
    case ButtonPress:
      running = 0;
      break;
    }
  }

  XFreeGC(display, gc);
  XDestroyWindow(display, window);
  XCloseDisplay(display);

  return 0;
}
