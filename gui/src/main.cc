#include "app_main.hh"

#include "logo_view.hh"

int app_shell_main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    LogoWindow window;
    if (!window.create()) return 1;
    window.run();
    return 0;
}
