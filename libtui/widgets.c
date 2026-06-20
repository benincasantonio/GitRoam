#include "tui_internal.h"

#include <stdlib.h>
#include <string.h>

static tui_status create_menu(tui_widget **out_widget,
                              tui_widget_type type,
                              const char *message,
                              const char *const *labels, size_t count,
                              tui_menu_fn callback, void *context)
{
    tui_widget *widget;
    size_t index;

    if (out_widget == NULL || labels == NULL || count == 0) {
        return TUI_ERR_ARGUMENT;
    }
    widget = calloc(1, sizeof(*widget));
    if (widget == NULL) {
        return TUI_ERR_MEMORY;
    }
    widget->type = type;
    widget->focusable = true;
    widget->data.menu.labels = calloc(count, sizeof(char *));
    if (widget->data.menu.labels == NULL) {
        free(widget);
        return TUI_ERR_MEMORY;
    }
    widget->data.menu.count = count;
    widget->data.menu.callback = callback;
    widget->data.menu.context = context;
    widget->data.menu.message = tui_strdup(message);
    if (widget->data.menu.message == NULL) {
        tui_widget_destroy(widget);
        return TUI_ERR_MEMORY;
    }
    for (index = 0; index < count; index++) {
        widget->data.menu.labels[index] = tui_strdup(labels[index]);
        if (widget->data.menu.labels[index] == NULL) {
            tui_widget_destroy(widget);
            return TUI_ERR_MEMORY;
        }
    }
    *out_widget = widget;
    return TUI_OK;
}

tui_status tui_menu_create(tui_widget **out_widget,
                           const char *const *labels, size_t count,
                           tui_menu_fn on_activate, void *context)
{
    return create_menu(out_widget, TUI_WIDGET_MENU, NULL, labels, count,
                       on_activate, context);
}

tui_status tui_action_dialog_create(tui_widget **out_widget,
                                    const char *message,
                                    const char *const *labels, size_t count,
                                    tui_menu_fn on_activate, void *context)
{
    return create_menu(out_widget, TUI_WIDGET_ACTION_DIALOG, message, labels,
                       count, on_activate, context);
}

tui_status tui_text_input_create(tui_widget **out_widget,
                                 const char *prompt, const char *initial,
                                 tui_input_fn on_submit, void *context)
{
    tui_widget *widget;
    size_t length;

    if (out_widget == NULL) {
        return TUI_ERR_ARGUMENT;
    }
    if (initial == NULL) {
        initial = "";
    }
    length = strlen(initial);
    widget = calloc(1, sizeof(*widget));
    if (widget == NULL) {
        return TUI_ERR_MEMORY;
    }
    widget->type = TUI_WIDGET_TEXT_INPUT;
    widget->focusable = true;
    widget->data.input.prompt = tui_strdup(prompt);
    widget->data.input.capacity = length + 32;
    widget->data.input.value = calloc(widget->data.input.capacity, 1);
    if (widget->data.input.prompt == NULL ||
        widget->data.input.value == NULL) {
        tui_widget_destroy(widget);
        return TUI_ERR_MEMORY;
    }
    memcpy(widget->data.input.value, initial, length + 1);
    widget->data.input.length = length;
    widget->data.input.cursor = length;
    widget->data.input.callback = on_submit;
    widget->data.input.context = context;
    *out_widget = widget;
    return TUI_OK;
}

tui_status tui_message_dialog_create(tui_widget **out_widget,
                                     const char *message,
                                     tui_dialog_fn on_close, void *context)
{
    tui_widget *widget;

    if (out_widget == NULL) {
        return TUI_ERR_ARGUMENT;
    }
    widget = calloc(1, sizeof(*widget));
    if (widget == NULL) {
        return TUI_ERR_MEMORY;
    }
    widget->type = TUI_WIDGET_MESSAGE_DIALOG;
    widget->focusable = true;
    widget->data.dialog.message = tui_strdup(message);
    if (widget->data.dialog.message == NULL) {
        free(widget);
        return TUI_ERR_MEMORY;
    }
    widget->data.dialog.callback = on_close;
    widget->data.dialog.context = context;
    *out_widget = widget;
    return TUI_OK;
}

tui_status tui_status_bar_create(tui_widget **out_widget, const char *text)
{
    tui_widget *widget;

    if (out_widget == NULL) {
        return TUI_ERR_ARGUMENT;
    }
    widget = calloc(1, sizeof(*widget));
    if (widget == NULL) {
        return TUI_ERR_MEMORY;
    }
    widget->type = TUI_WIDGET_STATUS_BAR;
    widget->data.status.text = tui_strdup(text);
    if (widget->data.status.text == NULL) {
        free(widget);
        return TUI_ERR_MEMORY;
    }
    *out_widget = widget;
    return TUI_OK;
}

void tui_widget_destroy(tui_widget *widget)
{
    size_t index;

    if (widget == NULL) {
        return;
    }
    switch (widget->type) {
    case TUI_WIDGET_MENU:
    case TUI_WIDGET_ACTION_DIALOG:
        for (index = 0; index < widget->data.menu.count; index++) {
            free(widget->data.menu.labels[index]);
        }
        free(widget->data.menu.labels);
        free(widget->data.menu.message);
        break;
    case TUI_WIDGET_TEXT_INPUT:
        free(widget->data.input.prompt);
        free(widget->data.input.value);
        break;
    case TUI_WIDGET_MESSAGE_DIALOG:
        free(widget->data.dialog.message);
        break;
    case TUI_WIDGET_STATUS_BAR:
        free(widget->data.status.text);
        break;
    }
    free(widget);
}

tui_status tui_widget_set_text(tui_widget *widget, const char *text)
{
    char *copy;
    char **target;

    if (widget == NULL) {
        return TUI_ERR_ARGUMENT;
    }
    if (widget->type == TUI_WIDGET_STATUS_BAR) {
        target = &widget->data.status.text;
    } else if (widget->type == TUI_WIDGET_MESSAGE_DIALOG) {
        target = &widget->data.dialog.message;
    } else {
        return TUI_ERR_ARGUMENT;
    }
    copy = tui_strdup(text);
    if (copy == NULL) {
        return TUI_ERR_MEMORY;
    }
    free(*target);
    *target = copy;
    return TUI_OK;
}

size_t tui_menu_selected(const tui_widget *widget)
{
    if (widget == NULL ||
        (widget->type != TUI_WIDGET_MENU &&
         widget->type != TUI_WIDGET_ACTION_DIALOG)) {
        return 0;
    }
    return widget->data.menu.selected;
}

tui_status tui_menu_set_selected(tui_widget *widget, size_t selected)
{
    if (widget == NULL ||
        (widget->type != TUI_WIDGET_MENU &&
         widget->type != TUI_WIDGET_ACTION_DIALOG) ||
        selected >= widget->data.menu.count) {
        return TUI_ERR_ARGUMENT;
    }
    widget->data.menu.selected = selected;
    return TUI_OK;
}

const char *tui_text_input_value(const tui_widget *widget)
{
    if (widget == NULL || widget->type != TUI_WIDGET_TEXT_INPUT) {
        return NULL;
    }
    return widget->data.input.value;
}

tui_status tui_text_input_set_value(tui_widget *widget, const char *value)
{
    char *copy;
    size_t length;

    if (widget == NULL || widget->type != TUI_WIDGET_TEXT_INPUT ||
        value == NULL) {
        return TUI_ERR_ARGUMENT;
    }
    length = strlen(value);
    copy = realloc(widget->data.input.value, length + 32);
    if (copy == NULL) {
        return TUI_ERR_MEMORY;
    }
    widget->data.input.value = copy;
    widget->data.input.capacity = length + 32;
    memcpy(widget->data.input.value, value, length + 1);
    widget->data.input.length = length;
    widget->data.input.cursor = length;
    return TUI_OK;
}

static bool input_insert(tui_widget *widget, unsigned int character)
{
    char *value;
    size_t cursor = widget->data.input.cursor;

    if (character < 32 || character > 126) {
        return false;
    }
    if (widget->data.input.length + 2 > widget->data.input.capacity) {
        size_t capacity = widget->data.input.capacity * 2;
        value = realloc(widget->data.input.value, capacity);
        if (value == NULL) {
            return true;
        }
        widget->data.input.value = value;
        widget->data.input.capacity = capacity;
    }
    memmove(widget->data.input.value + cursor + 1,
            widget->data.input.value + cursor,
            widget->data.input.length - cursor + 1);
    widget->data.input.value[cursor] = (char)character;
    widget->data.input.cursor++;
    widget->data.input.length++;
    return true;
}

bool tui_widget_handle_event(tui_app *app, tui_widget *widget,
                             const tui_event *event)
{
    if (widget == NULL || event == NULL) {
        return false;
    }
    if (widget->type == TUI_WIDGET_MENU ||
        widget->type == TUI_WIDGET_ACTION_DIALOG) {
        if (event->type == TUI_EVENT_UP) {
            if (widget->data.menu.selected > 0) {
                widget->data.menu.selected--;
            }
            return true;
        }
        if (event->type == TUI_EVENT_DOWN) {
            if (widget->data.menu.selected + 1 < widget->data.menu.count) {
                widget->data.menu.selected++;
            }
            return true;
        }
        if (event->type == TUI_EVENT_HOME) {
            widget->data.menu.selected = 0;
            return true;
        }
        if (event->type == TUI_EVENT_END) {
            widget->data.menu.selected = widget->data.menu.count - 1;
            return true;
        }
        if (event->type == TUI_EVENT_ENTER) {
            if (widget->data.menu.callback != NULL) {
                widget->data.menu.callback(app, widget,
                                           widget->data.menu.selected,
                                           widget->data.menu.context);
            }
            return true;
        }
        return false;
    }
    if (widget->type == TUI_WIDGET_TEXT_INPUT) {
        size_t cursor = widget->data.input.cursor;
        if (event->type == TUI_EVENT_LEFT) {
            if (cursor > 0) {
                widget->data.input.cursor--;
            }
            return true;
        }
        if (event->type == TUI_EVENT_RIGHT) {
            if (cursor < widget->data.input.length) {
                widget->data.input.cursor++;
            }
            return true;
        }
        if (event->type == TUI_EVENT_HOME) {
            widget->data.input.cursor = 0;
            return true;
        }
        if (event->type == TUI_EVENT_END) {
            widget->data.input.cursor = widget->data.input.length;
            return true;
        }
        if (event->type == TUI_EVENT_BACKSPACE) {
            if (cursor > 0) {
                memmove(widget->data.input.value + cursor - 1,
                        widget->data.input.value + cursor,
                        widget->data.input.length - cursor + 1);
                widget->data.input.cursor--;
                widget->data.input.length--;
            }
            return true;
        }
        if (event->type == TUI_EVENT_DELETE) {
            if (cursor < widget->data.input.length) {
                memmove(widget->data.input.value + cursor,
                        widget->data.input.value + cursor + 1,
                        widget->data.input.length - cursor);
                widget->data.input.length--;
            }
            return true;
        }
        if (event->type == TUI_EVENT_CHARACTER) {
            return input_insert(widget, event->character);
        }
        if (event->type == TUI_EVENT_ENTER) {
            if (widget->data.input.callback != NULL) {
                widget->data.input.callback(app, widget,
                                            widget->data.input.value,
                                            widget->data.input.context);
            }
            return true;
        }
        return false;
    }
    if (widget->type == TUI_WIDGET_MESSAGE_DIALOG) {
        if (event->type == TUI_EVENT_ENTER) {
            if (widget->data.dialog.callback != NULL) {
                widget->data.dialog.callback(app, widget, true,
                                             widget->data.dialog.context);
            } else {
                (void)tui_app_pop_screen(app);
            }
            return true;
        }
        if (event->type == TUI_EVENT_ESCAPE) {
            if (widget->data.dialog.callback != NULL) {
                widget->data.dialog.callback(app, widget, false,
                                             widget->data.dialog.context);
            } else {
                (void)tui_app_pop_screen(app);
            }
            return true;
        }
    }
    return false;
}
