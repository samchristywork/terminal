#ifndef SHELL_H
#define SHELL_H

#include "gui.h"
#include "terminal.h"

void init_shell(GuiContext *gui, int cols, int rows);
void read_shell_output(GuiContext *gui, Terminal *terminal);

#endif
