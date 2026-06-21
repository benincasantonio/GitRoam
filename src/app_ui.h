#ifndef GITROAM_APP_UI_H
#define GITROAM_APP_UI_H

#include "tui.h"

#include <stdbool.h>

char *app_string_copy(const char *value);
char *app_worktree_search_term(const char *branch, const char *path);
bool app_quit_shortcut(tui_app *app, tui_screen *screen,
                       const tui_event *event, void *context);
void app_show_message(tui_app *app, const char *title, const char *message);
tui_status app_screen_add_status(tui_screen *screen, const char *text);
tui_status app_screen_take_widget(tui_screen *screen, tui_widget **widget);

#endif
