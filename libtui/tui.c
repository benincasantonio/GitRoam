#include "tui_internal.h"

#include <stdlib.h>
#include <string.h>

char *tui_strdup(const char *value)
{
    size_t length;
    char *copy;

    if (value == NULL) {
        value = "";
    }
    length = strlen(value);
    copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, value, length + 1);
    }
    return copy;
}

const char *tui_status_string(tui_status status)
{
    switch (status) {
    case TUI_OK:
        return "success";
    case TUI_ERR_ARGUMENT:
        return "invalid argument";
    case TUI_ERR_MEMORY:
        return "out of memory";
    case TUI_ERR_TERMINAL:
        return "terminal error";
    case TUI_ERR_STATE:
        return "invalid state";
    }
    return "unknown error";
}

tui_status tui_app_create_with_backend(tui_app **out_app,
                                       tui_backend *backend)
{
    tui_app *app;

    if (out_app == NULL || backend == NULL) {
        return TUI_ERR_ARGUMENT;
    }
    app = calloc(1, sizeof(*app));
    if (app == NULL) {
        return TUI_ERR_MEMORY;
    }
    app->backend = backend;
    app->dirty = true;
    *out_app = app;
    return TUI_OK;
}

tui_status tui_app_create(tui_app **out_app)
{
    tui_backend *backend;
    tui_status status;

    if (out_app == NULL) {
        return TUI_ERR_ARGUMENT;
    }
    backend = tui_ncurses_backend_create();
    if (backend == NULL) {
        return TUI_ERR_MEMORY;
    }
    status = tui_app_create_with_backend(out_app, backend);
    if (status != TUI_OK) {
        if (backend->destroy != NULL) {
            backend->destroy(backend->context);
        }
        free(backend);
    }
    return status;
}

static tui_status ensure_screen_capacity(tui_app *app)
{
    tui_screen **screens;
    size_t capacity;

    if (app->screen_count < app->screen_capacity) {
        return TUI_OK;
    }
    capacity = app->screen_capacity == 0 ? 4 : app->screen_capacity * 2;
    screens = realloc(app->screens, capacity * sizeof(*screens));
    if (screens == NULL) {
        return TUI_ERR_MEMORY;
    }
    app->screens = screens;
    app->screen_capacity = capacity;
    return TUI_OK;
}

tui_status tui_app_push_screen(tui_app *app, tui_screen *screen)
{
    tui_status status;

    if (app == NULL || screen == NULL) {
        return TUI_ERR_ARGUMENT;
    }
    status = ensure_screen_capacity(app);
    if (status != TUI_OK) {
        return status;
    }
    app->screens[app->screen_count++] = screen;
    app->dirty = true;
    return TUI_OK;
}

tui_status tui_app_pop_screen(tui_app *app)
{
    if (app == NULL) {
        return TUI_ERR_ARGUMENT;
    }
    if (app->screen_count == 0) {
        return TUI_ERR_STATE;
    }
    tui_screen_destroy(app->screens[--app->screen_count]);
    app->dirty = true;
    if (app->screen_count == 0) {
        app->running = false;
    }
    return TUI_OK;
}

tui_status tui_app_replace_screen(tui_app *app, tui_screen *screen)
{
    if (app == NULL || screen == NULL) {
        return TUI_ERR_ARGUMENT;
    }
    if (app->screen_count == 0) {
        return tui_app_push_screen(app, screen);
    }
    tui_screen_destroy(app->screens[app->screen_count - 1]);
    app->screens[app->screen_count - 1] = screen;
    app->dirty = true;
    return TUI_OK;
}

tui_screen *tui_app_current_screen(const tui_app *app)
{
    if (app == NULL || app->screen_count == 0) {
        return NULL;
    }
    return app->screens[app->screen_count - 1];
}

size_t tui_app_screen_count(const tui_app *app)
{
    return app == NULL ? 0 : app->screen_count;
}

void tui_app_request_redraw(tui_app *app)
{
    if (app != NULL) {
        app->dirty = true;
    }
}

void tui_app_stop(tui_app *app)
{
    if (app != NULL) {
        app->running = false;
    }
}

static bool dispatch_default(tui_app *app, tui_screen *screen,
                             const tui_event *event)
{
    tui_widget *widget = NULL;

    if (screen->widget_count > 0 && screen->focused < screen->widget_count) {
        widget = screen->widgets[screen->focused];
    }
    if (widget != NULL && tui_widget_handle_event(app, widget, event)) {
        return true;
    }
    if (event->type == TUI_EVENT_ESCAPE) {
        if (app->screen_count > 1) {
            (void)tui_app_pop_screen(app);
        } else {
            tui_app_stop(app);
        }
        return true;
    }
    if (event->type == TUI_EVENT_QUIT) {
        tui_app_stop(app);
        return true;
    }
    return false;
}

tui_status tui_app_run(tui_app *app)
{
    tui_status status;

    if (app == NULL || app->backend == NULL || app->screen_count == 0) {
        return TUI_ERR_ARGUMENT;
    }
    if (!app->initialized) {
        status = app->backend->init(app->backend->context);
        if (status != TUI_OK) {
            return status;
        }
        app->initialized = true;
    }
    app->running = true;
    app->dirty = true;
    while (app->running && app->screen_count > 0) {
        tui_event event = { TUI_EVENT_NONE, 0 };
        tui_screen *screen;
        bool handled = false;

        if (app->dirty) {
            tui_render(app);
            app->dirty = false;
        }
        status = app->backend->read_event(app->backend->context, &event);
        if (status != TUI_OK) {
            app->running = false;
            return status;
        }
        screen = tui_app_current_screen(app);
        if (screen == NULL) {
            break;
        }
        if (event.type == TUI_EVENT_RESIZE) {
            app->dirty = true;
            continue;
        }
        if (screen->event_handler != NULL) {
            handled = screen->event_handler(app, screen, &event,
                                            screen->event_context);
        }
        if (!handled) {
            handled = dispatch_default(app, screen, &event);
        }
        if (handled) {
            app->dirty = true;
        }
    }
    return TUI_OK;
}

tui_status tui_app_suspend(tui_app *app, tui_suspend_fn function,
                           void *context, int *result)
{
    tui_status status;
    int call_result;

    if (app == NULL || function == NULL || !app->initialized) {
        return TUI_ERR_ARGUMENT;
    }
    status = app->backend->suspend(app->backend->context);
    if (status != TUI_OK) {
        return status;
    }
    call_result = function(context);
    status = app->backend->resume(app->backend->context);
    app->dirty = true;
    if (result != NULL) {
        *result = call_result;
    }
    return status;
}

void tui_app_destroy(tui_app *app)
{
    if (app == NULL) {
        return;
    }
    while (app->screen_count > 0) {
        tui_screen_destroy(app->screens[--app->screen_count]);
    }
    free(app->screens);
    if (app->initialized && app->backend->shutdown != NULL) {
        app->backend->shutdown(app->backend->context);
    }
    if (app->backend != NULL) {
        if (app->backend->destroy != NULL) {
            app->backend->destroy(app->backend->context);
        }
        free(app->backend);
    }
    free(app);
}

tui_status tui_screen_create(tui_screen **out_screen, const char *title,
                             void *context, tui_destroy_fn destroy_context)
{
    tui_screen *screen;

    if (out_screen == NULL) {
        return TUI_ERR_ARGUMENT;
    }
    screen = calloc(1, sizeof(*screen));
    if (screen == NULL) {
        return TUI_ERR_MEMORY;
    }
    screen->title = tui_strdup(title);
    if (screen->title == NULL) {
        free(screen);
        return TUI_ERR_MEMORY;
    }
    screen->context = context;
    screen->destroy_context = destroy_context;
    *out_screen = screen;
    return TUI_OK;
}

void *tui_screen_context(const tui_screen *screen)
{
    return screen == NULL ? NULL : screen->context;
}

void tui_screen_set_event_handler(tui_screen *screen,
                                  tui_screen_event_fn handler,
                                  void *context)
{
    if (screen != NULL) {
        screen->event_handler = handler;
        screen->event_context = context;
    }
}

tui_status tui_screen_add_widget(tui_screen *screen, tui_widget *widget)
{
    tui_widget **widgets;
    size_t capacity;

    if (screen == NULL || widget == NULL) {
        return TUI_ERR_ARGUMENT;
    }
    if (screen->widget_count == screen->widget_capacity) {
        capacity = screen->widget_capacity == 0 ? 4 :
                   screen->widget_capacity * 2;
        widgets = realloc(screen->widgets, capacity * sizeof(*widgets));
        if (widgets == NULL) {
            return TUI_ERR_MEMORY;
        }
        screen->widgets = widgets;
        screen->widget_capacity = capacity;
    }
    screen->widgets[screen->widget_count] = widget;
    if (widget->focusable && (screen->widget_count == 0 ||
                             !screen->widgets[screen->focused]->focusable)) {
        screen->focused = screen->widget_count;
    }
    screen->widget_count++;
    return TUI_OK;
}

void tui_screen_destroy(tui_screen *screen)
{
    size_t index;

    if (screen == NULL) {
        return;
    }
    for (index = 0; index < screen->widget_count; index++) {
        tui_widget_destroy(screen->widgets[index]);
    }
    free(screen->widgets);
    free(screen->title);
    if (screen->destroy_context != NULL) {
        screen->destroy_context(screen->context);
    }
    free(screen);
}
