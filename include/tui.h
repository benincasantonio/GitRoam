#ifndef TUI_H
#define TUI_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tui_app tui_app;
typedef struct tui_screen tui_screen;
typedef struct tui_widget tui_widget;

typedef enum {
    TUI_OK = 0,
    TUI_ERR_ARGUMENT,
    TUI_ERR_MEMORY,
    TUI_ERR_TERMINAL,
    TUI_ERR_STATE
} tui_status;

typedef enum {
    TUI_EVENT_NONE = 0,
    TUI_EVENT_UP,
    TUI_EVENT_DOWN,
    TUI_EVENT_LEFT,
    TUI_EVENT_RIGHT,
    TUI_EVENT_ENTER,
    TUI_EVENT_ESCAPE,
    TUI_EVENT_BACKSPACE,
    TUI_EVENT_DELETE,
    TUI_EVENT_HOME,
    TUI_EVENT_END,
    TUI_EVENT_RESIZE,
    TUI_EVENT_QUIT,
    TUI_EVENT_CHARACTER
} tui_event_type;

typedef struct {
    tui_event_type type;
    unsigned int character;
} tui_event;

typedef void (*tui_destroy_fn)(void *context);
typedef bool (*tui_screen_event_fn)(tui_app *app, tui_screen *screen,
                                    const tui_event *event, void *context);
typedef void (*tui_menu_fn)(tui_app *app, tui_widget *menu,
                            size_t selected, void *context);
typedef void (*tui_input_fn)(tui_app *app, tui_widget *input,
                             const char *value, void *context);
typedef void (*tui_dialog_fn)(tui_app *app, tui_widget *dialog,
                              bool accepted, void *context);
typedef int (*tui_suspend_fn)(void *context);

const char *tui_status_string(tui_status status);

tui_status tui_app_create(tui_app **out_app);
tui_status tui_app_run(tui_app *app);
void tui_app_request_redraw(tui_app *app);
void tui_app_stop(tui_app *app);
void tui_app_destroy(tui_app *app);
tui_status tui_app_suspend(tui_app *app, tui_suspend_fn function,
                           void *context, int *result);

tui_status tui_screen_create(tui_screen **out_screen, const char *title,
                             void *context, tui_destroy_fn destroy_context);
void *tui_screen_context(const tui_screen *screen);
void tui_screen_set_event_handler(tui_screen *screen,
                                  tui_screen_event_fn handler,
                                  void *context);
tui_status tui_screen_add_widget(tui_screen *screen, tui_widget *widget);
void tui_screen_destroy(tui_screen *screen);

tui_status tui_app_push_screen(tui_app *app, tui_screen *screen);
tui_status tui_app_pop_screen(tui_app *app);
tui_status tui_app_replace_screen(tui_app *app, tui_screen *screen);
tui_screen *tui_app_current_screen(const tui_app *app);
size_t tui_app_screen_count(const tui_app *app);

tui_status tui_menu_create(tui_widget **out_widget,
                           const char *const *labels, size_t count,
                           tui_menu_fn on_activate, void *context);
tui_status tui_action_dialog_create(tui_widget **out_widget,
                                    const char *message,
                                    const char *const *labels, size_t count,
                                    tui_menu_fn on_activate, void *context);
tui_status tui_text_input_create(tui_widget **out_widget,
                                 const char *prompt, const char *initial,
                                 tui_input_fn on_submit, void *context);
tui_status tui_message_dialog_create(tui_widget **out_widget,
                                     const char *message,
                                     tui_dialog_fn on_close, void *context);
tui_status tui_status_bar_create(tui_widget **out_widget, const char *text);

void tui_widget_destroy(tui_widget *widget);
tui_status tui_widget_set_text(tui_widget *widget, const char *text);
size_t tui_menu_selected(const tui_widget *widget);
tui_status tui_menu_set_selected(tui_widget *widget, size_t selected);
const char *tui_text_input_value(const tui_widget *widget);
tui_status tui_text_input_set_value(tui_widget *widget, const char *value);

#ifdef __cplusplus
}
#endif

#endif
