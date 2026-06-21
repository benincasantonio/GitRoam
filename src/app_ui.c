#include "app_ui.h"

#include <stdlib.h>
#include <string.h>

/* Intentionally duplicates git_internal_string_copy: the app/UI layer keeps
 * its own string helper so it does not depend on the git module's internals. */
char *app_string_copy(const char *value)
{
    size_t length;
    char *copy;

    if (value == NULL) {
        return NULL;
    }
    length = strlen(value);
    copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, value, length + 1);
    }
    return copy;
}

char *app_worktree_search_term(const char *branch, const char *path)
{
    const char *branch_term = branch == NULL ? "detached" : branch;
    const char *folder_start;
    const char *folder_end;
    char *term;
    size_t branch_length;
    size_t folder_length;
    size_t length;

    if (path == NULL) {
        return NULL;
    }
    folder_end = path + strlen(path);
    while (folder_end > path + 1 && folder_end[-1] == '/') {
        folder_end--;
    }
    folder_start = folder_end;
    while (folder_start > path && folder_start[-1] != '/') {
        folder_start--;
    }
    branch_length = strlen(branch_term);
    folder_length = (size_t)(folder_end - folder_start);
    length = branch_length + (folder_length == 0 ? 1 : folder_length + 2);
    term = malloc(length);
    if (term != NULL) {
        memcpy(term, branch_term, branch_length);
        if (folder_length != 0) {
            term[branch_length] = ' ';
            memcpy(term + branch_length + 1, folder_start, folder_length);
        }
        term[length - 1] = '\0';
    }
    return term;
}

bool app_quit_shortcut(tui_app *app, tui_screen *screen,
                       const tui_event *event, void *context)
{
    (void)screen;
    (void)context;
    if (event->type == TUI_EVENT_CHARACTER && event->character == 'q') {
        tui_app_stop(app);
        return true;
    }
    return false;
}

tui_status app_screen_take_widget(tui_screen *screen, tui_widget **widget)
{
    tui_status status;

    if (screen == NULL || widget == NULL || *widget == NULL) {
        return TUI_ERR_ARGUMENT;
    }
    status = tui_screen_add_widget(screen, *widget);
    if (status == TUI_OK) {
        *widget = NULL;
    }
    return status;
}

tui_status app_screen_add_status(tui_screen *screen, const char *text)
{
    tui_widget *status = NULL;
    tui_status result = tui_status_bar_create(&status, text);

    if (result == TUI_OK) {
        result = tui_screen_add_widget(screen, status);
    }
    if (result != TUI_OK) {
        tui_widget_destroy(status);
    }
    return result;
}

static void close_message(tui_app *app, tui_widget *widget, bool accepted,
                          void *context)
{
    (void)widget;
    (void)accepted;
    (void)context;
    (void)tui_app_pop_screen(app);
}

void app_show_message(tui_app *app, const char *title, const char *message)
{
    tui_screen *screen = NULL;
    tui_widget *dialog = NULL;

    if (tui_screen_create(&screen, title, NULL, NULL) != TUI_OK ||
        tui_message_dialog_create(&dialog, message, close_message, NULL) !=
            TUI_OK) {
        tui_widget_destroy(dialog);
        tui_screen_destroy(screen);
        return;
    }
    if (app_screen_take_widget(screen, &dialog) != TUI_OK) {
        tui_widget_destroy(dialog);
        tui_screen_destroy(screen);
        return;
    }
    if (tui_app_push_screen(app, screen) != TUI_OK) {
        tui_screen_destroy(screen);
    }
}
