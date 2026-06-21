#ifndef TUI_INTERNAL_H
#define TUI_INTERNAL_H

#include "tui_backend.h"

typedef enum {
    TUI_WIDGET_MENU,
    TUI_WIDGET_ACTION_DIALOG,
    TUI_WIDGET_TEXT_INPUT,
    TUI_WIDGET_MESSAGE_DIALOG,
    TUI_WIDGET_STATUS_BAR
} tui_widget_type;

typedef enum {
    TUI_MENU_FILTER_INACTIVE,
    TUI_MENU_FILTER_EDITING,
    TUI_MENU_FILTER_APPLIED
} tui_menu_filter_state;

struct tui_widget {
    tui_widget_type type;
    bool focusable;
    union {
        struct {
            char **labels;
            size_t count;
            size_t selected;
            size_t scroll;
            char *message;
            tui_menu_fn callback;
            void *context;
            struct {
                char **terms;
                size_t searchable_count;
                size_t *visible_items;
                size_t visible_count;
                size_t match_count;
                char *query;
                size_t length;
                size_t capacity;
                char *saved_query;
                size_t saved_length;
                tui_menu_filter_state state;
            } filter;
        } menu;
        struct {
            char *prompt;
            char *value;
            size_t length;
            size_t capacity;
            size_t cursor;
            tui_input_fn callback;
            void *context;
        } input;
        struct {
            char *message;
            tui_dialog_fn callback;
            void *context;
        } dialog;
        struct {
            char *text;
        } status;
    } data;
};

struct tui_screen {
    char *title;
    void *context;
    tui_destroy_fn destroy_context;
    tui_screen_event_fn event_handler;
    void *event_context;
    tui_widget **widgets;
    size_t widget_count;
    size_t widget_capacity;
    size_t focused;
};

struct tui_app {
    tui_backend *backend;
    tui_screen **screens;
    size_t screen_count;
    size_t screen_capacity;
    bool running;
    bool initialized;
    bool dirty;
};

char *tui_strdup(const char *value);
size_t tui_menu_visible_count(const tui_widget *widget);
size_t tui_menu_visible_item(const tui_widget *widget, size_t visible);
void tui_render(tui_app *app);
bool tui_widget_handle_event(tui_app *app, tui_widget *widget,
                             const tui_event *event);

#endif
