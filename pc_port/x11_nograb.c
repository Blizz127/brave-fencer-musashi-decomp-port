/* LD_PRELOAD / linked interpose: native VRAM is a spectator.
 * SDL3-compat still calls XSetInputFocus / xcb_set_input_focus / stack-mode
 * configure from show/swap; that makes KWin keep raising the window. */
#define _GNU_SOURCE
#include "musashi_x11_nograb.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void Display;
typedef unsigned long Window;
typedef unsigned long Time;
typedef unsigned long Atom;

static unsigned long g_our_window;

static void *real_dlsym(void *handle, const char *name)
{
    static void *(*fn)(void *, const char *);
    if (!fn)
        fn = (void *(*)(void *, const char *))dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
    return fn ? fn(handle, name) : 0;
}

static void *lib_sym(const char *lib, const char *name)
{
    void *h = dlopen(lib, RTLD_LAZY | RTLD_NOLOAD);
    if (!h)
        h = dlopen(lib, RTLD_LAZY);
    return h ? real_dlsym(h, name) : 0;
}

static int is_interactive(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char *env = getenv("MUSASHI_INTERACTIVE");
        cached = (env && (env[0] == '1' || strcmp(env, "true") == 0 || strcmp(env, "yes") == 0)) ? 1 : 0;
    }
    return cached;
}

static int our_window(unsigned long w)
{
    return g_our_window != 0 && w == g_our_window;
}

int XSetInputFocus(Display *dpy, Window w, int revert_to, Time time)
{
    static int (*real)(Display *, Window, int, Time);
    if (!real)
        real = (int (*)(Display *, Window, int, Time))lib_sym("libX11.so.6", "XSetInputFocus");
    /* Never take focus onto the scanout in spectator mode. PointerRoot/None (release) is allowed. */
    if (!is_interactive() && our_window((unsigned long)w))
        return 1;
    return real ? real(dpy, w, revert_to, time) : 1;
}

int XRaiseWindow(Display *dpy, Window w)
{
    (void)dpy;
    (void)w;
    return 1;
}

int XGrabKeyboard(Display *dpy, Window grab_window, int owner_events,
                  int pointer_mode, int keyboard_mode, Time time)
{
    (void)dpy;
    (void)grab_window;
    (void)owner_events;
    (void)pointer_mode;
    (void)keyboard_mode;
    (void)time;
    return 2; /* AlreadyGrabbed */
}

int XGrabPointer(Display *dpy, Window grab_window, int owner_events,
                 unsigned event_mask, int pointer_mode, int keyboard_mode,
                 Window confine_to, unsigned long cursor, Time time)
{
    (void)dpy;
    (void)grab_window;
    (void)owner_events;
    (void)event_mask;
    (void)pointer_mode;
    (void)keyboard_mode;
    (void)confine_to;
    (void)cursor;
    (void)time;
    return 2;
}

int XMapRaised(Display *dpy, Window w)
{
    static int (*map_window)(Display *, Window);
    if (!map_window)
        map_window = (int (*)(Display *, Window))lib_sym("libX11.so.6", "XMapWindow");
    return map_window ? map_window(dpy, w) : 1;
}

int XRestackWindows(Display *dpy, Window *windows, int nwindows)
{
    (void)dpy;
    (void)windows;
    (void)nwindows;
    return 1;
}

int XConfigureWindow(Display *dpy, Window w, unsigned value_mask, void *changes)
{
    static int (*real)(Display *, Window, unsigned, void *);
    unsigned mask;
    if (!real)
        real = (int (*)(Display *, Window, unsigned, void *))lib_sym("libX11.so.6", "XConfigureWindow");
    /* CWSibling = 1<<5, CWStackMode = 1<<6. Restacking raises over the desktop. */
    mask = value_mask & ~((1u << 5) | (1u << 6));
    if (mask == 0)
        return 1;
    return real ? real(dpy, w, mask, changes) : 1;
}

unsigned int xcb_set_input_focus(void *c, unsigned char revert_to,
                                 unsigned int focus, unsigned int time)
{
    static unsigned int (*real)(void *, unsigned char, unsigned int, unsigned int);
    if (!real)
        real = (unsigned int (*)(void *, unsigned char, unsigned int, unsigned int))
            lib_sym("libxcb.so.1", "xcb_set_input_focus");
    if (!is_interactive() && our_window(focus))
        return 0;
    return real ? real(c, revert_to, focus, time) : 0;
}

unsigned int xcb_set_input_focus_checked(void *c, unsigned char revert_to,
                                         unsigned int focus, unsigned int time)
{
    static unsigned int (*real)(void *, unsigned char, unsigned int, unsigned int);
    if (!real)
        real = (unsigned int (*)(void *, unsigned char, unsigned int, unsigned int))
            lib_sym("libxcb.so.1", "xcb_set_input_focus_checked");
    if (!is_interactive() && our_window(focus))
        return 0;
    return real ? real(c, revert_to, focus, time) : 0;
}

unsigned int xcb_grab_keyboard(void *c, unsigned char owner_events,
                               unsigned int grab_window, unsigned int time,
                               unsigned char pointer_mode,
                               unsigned char keyboard_mode)
{
    (void)c;
    (void)owner_events;
    (void)grab_window;
    (void)time;
    (void)pointer_mode;
    (void)keyboard_mode;
    return 0;
}

unsigned int xcb_grab_keyboard_unchecked(void *c, unsigned char owner_events,
                                         unsigned int grab_window,
                                         unsigned int time,
                                         unsigned char pointer_mode,
                                         unsigned char keyboard_mode)
{
    return xcb_grab_keyboard(c, owner_events, grab_window, time,
                             pointer_mode, keyboard_mode);
}

unsigned int xcb_grab_pointer(void *c, unsigned char owner_events,
                              unsigned int grab_window, unsigned short event_mask,
                              unsigned char pointer_mode, unsigned char keyboard_mode,
                              unsigned int confine_to, unsigned int cursor,
                              unsigned int time)
{
    (void)c;
    (void)owner_events;
    (void)grab_window;
    (void)event_mask;
    (void)pointer_mode;
    (void)keyboard_mode;
    (void)confine_to;
    (void)cursor;
    (void)time;
    return 0;
}

static unsigned int configure_without_stack(void *c, unsigned int window,
                                            unsigned short value_mask,
                                            const unsigned int *value_list,
                                            int checked)
{
    static unsigned int (*real)(void *, unsigned int, unsigned short, const unsigned int *);
    static unsigned int (*real_checked)(void *, unsigned int, unsigned short, const unsigned int *);
    unsigned short mask = value_mask & ~(unsigned short)(32u | 64u);
    unsigned int packed[7];
    unsigned bit, src = 0, dst = 0;
    if (!real)
        real = (unsigned int (*)(void *, unsigned int, unsigned short, const unsigned int *))
            lib_sym("libxcb.so.1", "xcb_configure_window");
    if (!real_checked)
        real_checked = (unsigned int (*)(void *, unsigned int, unsigned short, const unsigned int *))
            lib_sym("libxcb.so.1", "xcb_configure_window_checked");
    if (mask == value_mask) {
        if (checked)
            return real_checked ? real_checked(c, window, value_mask, value_list) : 0;
        return real ? real(c, window, value_mask, value_list) : 0;
    }
    if (mask == 0 || !value_list)
        return 0;
    for (bit = 1u; bit <= 16u; bit <<= 1) {
        if (value_mask & bit) {
            if (mask & bit)
                packed[dst++] = value_list[src];
            src++;
        }
    }
    if (checked)
        return real_checked ? real_checked(c, window, mask, packed) : 0;
    return real ? real(c, window, mask, packed) : 0;
}

unsigned int xcb_configure_window(void *c, unsigned int window,
                                  unsigned short value_mask,
                                  const unsigned int *value_list)
{
    return configure_without_stack(c, window, value_mask, value_list, 0);
}

unsigned int xcb_configure_window_checked(void *c, unsigned int window,
                                          unsigned short value_mask,
                                          const unsigned int *value_list)
{
    return configure_without_stack(c, window, value_mask, value_list, 1);
}

void *dlsym(void *handle, const char *name)
{
    if (name) {
        if (!strcmp(name, "XSetInputFocus"))
            return (void *)XSetInputFocus;
        if (!strcmp(name, "XRaiseWindow"))
            return (void *)XRaiseWindow;
        if (!strcmp(name, "XGrabKeyboard"))
            return (void *)XGrabKeyboard;
        if (!strcmp(name, "XGrabPointer"))
            return (void *)XGrabPointer;
        if (!strcmp(name, "XMapRaised"))
            return (void *)XMapRaised;
        if (!strcmp(name, "XRestackWindows"))
            return (void *)XRestackWindows;
        if (!strcmp(name, "XConfigureWindow"))
            return (void *)XConfigureWindow;
        if (!strcmp(name, "xcb_set_input_focus"))
            return (void *)xcb_set_input_focus;
        if (!strcmp(name, "xcb_set_input_focus_checked"))
            return (void *)xcb_set_input_focus_checked;
        if (!strcmp(name, "xcb_grab_keyboard"))
            return (void *)xcb_grab_keyboard;
        if (!strcmp(name, "xcb_grab_keyboard_unchecked"))
            return (void *)xcb_grab_keyboard_unchecked;
        if (!strcmp(name, "xcb_grab_pointer"))
            return (void *)xcb_grab_pointer;
        if (!strcmp(name, "xcb_configure_window"))
            return (void *)xcb_configure_window;
        if (!strcmp(name, "xcb_configure_window_checked"))
            return (void *)xcb_configure_window_checked;
    }
    return real_dlsym(handle, name);
}

void musashi_x11_spectator(void *display, unsigned long window)
{
    typedef struct {
        long flags;
        int input;
        int initial_state;
        void *icon_pixmap;
        unsigned long icon_window;
        int icon_x, icon_y;
        void *icon_mask;
        unsigned long window_group;
    } XWMHints;
    Display *dpy = display;
    Window w = (Window)window;
    XWMHints *(*get_hints)(Display *, Window);
    int (*set_hints)(Display *, Window, XWMHints *);
    int (*get_protocols)(Display *, Window, Atom **, int *);
    int (*set_protocols)(Display *, Window, Atom *, int);
    Atom (*intern)(Display *, const char *, int);
    int (*xfree)(void *);
    int (*real_focus)(Display *, Window, int, Time);
    XWMHints *hints;
    XWMHints fresh;
    Atom *protocols = 0;
    Atom take_focus;
    int n = 0, i, kept = 0;

    if (!dpy || !window)
        return;
    g_our_window = window;

    get_hints = (XWMHints *(*)(Display *, Window))lib_sym("libX11.so.6", "XGetWMHints");
    set_hints = (int (*)(Display *, Window, XWMHints *))lib_sym("libX11.so.6", "XSetWMHints");
    xfree = (int (*)(void *))lib_sym("libX11.so.6", "XFree");

    if (is_interactive()) {
        memset(&fresh, 0, sizeof(fresh));
        hints = get_hints ? get_hints(dpy, w) : 0;
        if (hints) {
            fresh = *hints;
            if (xfree)
                xfree(hints);
        }
        fresh.flags |= 1L; /* InputHint */
        fresh.input = 1;   /* Accept keyboard input */
        if (set_hints)
            set_hints(dpy, w, &fresh);
        return;
    }

    get_protocols = (int (*)(Display *, Window, Atom **, int *))lib_sym("libX11.so.6", "XGetWMProtocols");
    set_protocols = (int (*)(Display *, Window, Atom *, int))lib_sym("libX11.so.6", "XSetWMProtocols");
    intern = (Atom (*)(Display *, const char *, int))lib_sym("libX11.so.6", "XInternAtom");
    real_focus = (int (*)(Display *, Window, int, Time))lib_sym("libX11.so.6", "XSetInputFocus");

    memset(&fresh, 0, sizeof(fresh));
    hints = get_hints ? get_hints(dpy, w) : 0;
    if (hints) {
        fresh = *hints;
        if (xfree)
            xfree(hints);
    }
    fresh.flags |= 1L; /* InputHint */
    fresh.input = 0;
    if (set_hints)
        set_hints(dpy, w, &fresh);

    take_focus = intern ? intern(dpy, "WM_TAKE_FOCUS", 1) : 0;
    if (take_focus && get_protocols && set_protocols &&
        get_protocols(dpy, w, &protocols, &n) && protocols) {
        for (i = 0; i < n; ++i)
            if (protocols[i] != take_focus)
                protocols[kept++] = protocols[i];
        set_protocols(dpy, w, protocols, kept);
        if (xfree)
            xfree(protocols);
    }

    /* Give the desktop the keyboard back. PointerRoot = 1, RevertToPointerRoot = 1. */
    if (real_focus)
        real_focus(dpy, (Window)1, 1, 0);
}

void musashi_x11_spectator_focused(void)
{
    Display *(*xopen)(const char *);
    int (*xgetfocus)(Display *, Window *, int *);
    int (*xflush)(Display *);
    int (*xclose)(Display *);
    Display *dpy;
    Window focused = 0;
    int revert = 0;

    xopen = (Display *(*)(const char *))lib_sym("libX11.so.6", "XOpenDisplay");
    xgetfocus = (int (*)(Display *, Window *, int *))lib_sym("libX11.so.6", "XGetInputFocus");
    xflush = (int (*)(Display *))lib_sym("libX11.so.6", "XFlush");
    xclose = (int (*)(Display *))lib_sym("libX11.so.6", "XCloseDisplay");
    if (!xopen || !xgetfocus)
        return;
    dpy = xopen(NULL);
    if (!dpy)
        return;
    xgetfocus(dpy, &focused, &revert);
    if (is_interactive()) {
        fprintf(stderr, "native_boot: HOST_WINDOW interactive xid=%lu focus=1 grab=0\n",
                (unsigned long)focused);
        if (xflush)
            xflush(dpy);
        if (xclose)
            xclose(dpy);
        return;
    }
    if (focused > 1)
        musashi_x11_spectator(dpy, (unsigned long)focused);
    fprintf(stderr, "native_boot: HOST_WINDOW spectator xid=%lu focus=0 grab=0\n",
            (unsigned long)focused);
    if (xflush)
        xflush(dpy);
    if (xclose)
        xclose(dpy);
}
