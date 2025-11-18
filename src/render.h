#ifndef RENDER_H
#define RENDER_H

#include "gui.h"
#include "terminal.h"

void init_colors(GuiContext *gui, Args *args);
void build_selection_text(GuiContext *gui, Terminal *terminal);
unsigned long get_color_pixel(GuiContext *gui, Term_Color color);
XftColor *get_xft_color(GuiContext *gui, Term_Color color);
void draw_terminal(GuiContext *gui, Terminal *terminal);

#endif
