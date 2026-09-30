#ifndef MUSASHI_X11_NOGRAB_H
#define MUSASHI_X11_NOGRAB_H

/* Spectator scanout: never take keyboard focus or restack above the desktop. */
void musashi_x11_spectator(void *display, unsigned long window);
/* Apply spectator policy to the window that currently has X input focus. */
void musashi_x11_spectator_focused(void);

#endif
