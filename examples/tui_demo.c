#include "tui.h"

#include <stdio.h>
#include <stdlib.h>

static void close_dialog(tui_app *app, tui_widget *widget, bool accepted,
                         void *context)
{
    (void)widget;
    (void)accepted;
    (void)context;
    (void)tui_app_pop_screen(app);
}

static void submitted(tui_app *app, tui_widget *widget, const char *value,
                      void *context)
{
    tui_screen *screen = NULL;
    tui_widget *dialog = NULL;
    char message[256];

    (void)widget;
    (void)context;
    (void)snprintf(message, sizeof(message), "Hello, %s", value);
    if (tui_screen_create(&screen, "TUI demo", NULL, NULL) != TUI_OK ||
        tui_message_dialog_create(&dialog, message, close_dialog, NULL) !=
            TUI_OK) {
        tui_widget_destroy(dialog);
        tui_screen_destroy(screen);
        tui_app_stop(app);
        return;
    }
    if (tui_screen_add_widget(screen, dialog) != TUI_OK) {
        tui_widget_destroy(dialog);
        tui_screen_destroy(screen);
        tui_app_stop(app);
        return;
    }
    dialog = NULL;
    if (tui_app_push_screen(app, screen) != TUI_OK) {
        tui_screen_destroy(screen);
        tui_app_stop(app);
    }
}

static void menu_selected(tui_app *app, tui_widget *widget, size_t selected,
                          void *context)
{
    tui_screen *screen = NULL;
    tui_widget *child = NULL;

    (void)widget;
    (void)context;
    if (selected == 2) {
        tui_app_stop(app);
        return;
    }
    if (tui_screen_create(&screen, "TUI demo", NULL, NULL) != TUI_OK) {
        tui_app_stop(app);
        return;
    }
    if (selected == 0) {
        if (tui_text_input_create(&child, "Your name", "", submitted, NULL) !=
            TUI_OK) {
            tui_screen_destroy(screen);
            tui_app_stop(app);
            return;
        }
    } else if (tui_message_dialog_create(&child,
                                         "A reusable message dialog.",
                                         close_dialog, NULL) != TUI_OK) {
        tui_screen_destroy(screen);
        tui_app_stop(app);
        return;
    }
    if (tui_screen_add_widget(screen, child) != TUI_OK) {
        tui_widget_destroy(child);
        tui_screen_destroy(screen);
        tui_app_stop(app);
        return;
    }
    child = NULL;
    if (tui_app_push_screen(app, screen) != TUI_OK) {
        tui_screen_destroy(screen);
        tui_app_stop(app);
    }
}

int main(void)
{
    static const char *items[] = { "Text input", "Message", "Quit" };
    tui_app *app = NULL;
    tui_screen *screen = NULL;
    tui_widget *menu = NULL;
    tui_widget *status = NULL;
    tui_status result;

    result = tui_app_create(&app);
    if (result == TUI_OK) {
        result = tui_screen_create(&screen, "Reusable TUI demo", NULL, NULL);
    }
    if (result == TUI_OK) {
        result = tui_menu_create(&menu, items, 3, menu_selected, NULL);
    }
    if (result == TUI_OK) {
        result = tui_status_bar_create(&status,
                                       "Arrows: move  Enter: select  Esc: back");
    }
    if (result == TUI_OK) {
        result = tui_screen_add_widget(screen, menu);
        menu = NULL;
    }
    if (result == TUI_OK) {
        result = tui_screen_add_widget(screen, status);
        status = NULL;
    }
    if (result == TUI_OK) {
        result = tui_app_push_screen(app, screen);
        screen = NULL;
    }
    if (result == TUI_OK) {
        result = tui_app_run(app);
    }
    tui_widget_destroy(menu);
    tui_widget_destroy(status);
    tui_screen_destroy(screen);
    tui_app_destroy(app);
    if (result != TUI_OK) {
        fprintf(stderr, "tui-demo: %s\n", tui_status_string(result));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
