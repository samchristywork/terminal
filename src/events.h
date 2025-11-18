#ifndef EVENTS_H
#define EVENTS_H

#include "gui.h"
#include "terminal.h"

void handle_events(GuiContext *gui, Terminal *terminal, XEvent *event);

#endif
