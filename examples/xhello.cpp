// The C5 live judgment's X guest: a real X11 program that draws the desk's
// amber, names its window, and speaks WM_DELETE_WINDOW so the desk's close
// words arrive as a graceful goodbye instead of a kill. Xlib rides as a
// system library, the eyeball lane's own line. Run it against any X server:
//   DISPLAY=:9 xhello
// Xlib speaks in C macros and typedef-aliases the house checks cannot see
// through; the guest's own tongue is exempt from the house spelling rules.
// NOLINTBEGIN(misc-include-cleaner,misc-misplaced-const,modernize-use-integer-sign-comparison)
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <unistd.h>

#include <array>
#include <print>

int main() {
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        std::println(stderr, "xhello: no display");
        return 1;
    }
    const int screen = DefaultScreen(display);
    const Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, 96, 64, 0,
                                              BlackPixel(display, screen), 0x00c9'8a2dU);
    const std::array<Atom, 1> protocols{XInternAtom(display, "WM_DELETE_WINDOW", False)};
    XSetWMProtocols(display, window, const_cast<Atom*>(protocols.data()), 1);
    XStoreName(display, window, "Hello amber");
    XSelectInput(display, window, ExposureMask | StructureNotifyMask);
    XMapWindow(display, window);

    const Atom delete_window = protocols[0];
    GC gc = DefaultGC(display, screen);
    XColor amber{};
    amber.red = 0xc98a;
    amber.green = 0x8a2d;
    amber.blue = 0x2d00;
    amber.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(display, DefaultColormap(display, screen), &amber);

    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == Expose) {
            XSetForeground(display, gc, amber.pixel);
            XFillRectangle(display, window, gc, 8, 8, 80, 48);
            XFlush(display);
        } else if (event.type == ClientMessage &&
                   static_cast<Atom>(event.xclient.data.l[0]) == delete_window) {
            break; // the desk asked politely; the guest answers by leaving
        }
    }
    XCloseDisplay(display);
    return 0;
}
// NOLINTEND(misc-include-cleaner,misc-misplaced-const,modernize-use-integer-sign-comparison)
