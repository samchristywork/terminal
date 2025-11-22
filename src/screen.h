#ifndef SCREEN_H
#define SCREEN_H

#include "terminal.h"

void init_screen(Term_Screen *screen, int width, int height,
                 int scrollback_lines);
void free_screen(Term_Screen *screen, int height);
void reset_screen(Term_Screen *screen, int width, int height);
void scroll_screen(Term_Screen *screen, int width, int height);
void handle_newline(Term_Screen *screen, int width, int height);
void write_regular_cell(Term_Screen *screen, const char *data, int data_len,
                        int width, int height, Term_Attr attr);
void resize_screen(Term_Screen *screen, int old_width, int old_height,
                   int new_width, int new_height);

#endif
