#include "tui_internal.h"

#include <stdint.h>
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

tui_status tui_menu_enable_filter(
    tui_widget *widget, const char *const *search_terms,
    size_t searchable_count)
{
    char **terms = NULL;
    size_t *visible_items = NULL;
    char *query = NULL;
    size_t index;

    if (widget == NULL || widget->type != TUI_WIDGET_MENU ||
        search_terms == NULL || searchable_count == 0 ||
        searchable_count > widget->data.menu.count) {
        return TUI_ERR_ARGUMENT;
    }
    if (widget->data.menu.filter.terms != NULL) {
        return TUI_ERR_STATE;
    }
    if (widget->data.menu.count >
        SIZE_MAX / sizeof(*visible_items)) {
        return TUI_ERR_MEMORY;
    }
    terms = calloc(searchable_count, sizeof(*terms));
    visible_items = malloc(widget->data.menu.count *
                           sizeof(*visible_items));
    query = calloc(32, 1);
    if (terms == NULL || visible_items == NULL || query == NULL) {
        free(terms);
        free(visible_items);
        free(query);
        return TUI_ERR_MEMORY;
    }
    for (index = 0; index < searchable_count; index++) {
        terms[index] = tui_strdup(search_terms[index]);
        if (terms[index] == NULL) {
            while (index > 0) {
                free(terms[--index]);
            }
            free(terms);
            free(visible_items);
            free(query);
            return TUI_ERR_MEMORY;
        }
    }
    for (index = 0; index < widget->data.menu.count; index++) {
        visible_items[index] = index;
    }
    widget->data.menu.filter.terms = terms;
    widget->data.menu.filter.searchable_count = searchable_count;
    widget->data.menu.filter.visible_items = visible_items;
    widget->data.menu.filter.visible_count = widget->data.menu.count;
    widget->data.menu.filter.match_count = searchable_count;
    widget->data.menu.filter.query = query;
    widget->data.menu.filter.capacity = 32;
    return TUI_OK;
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
        for (index = 0;
             index < widget->data.menu.filter.searchable_count; index++) {
            free(widget->data.menu.filter.terms[index]);
        }
        free(widget->data.menu.labels);
        free(widget->data.menu.message);
        free(widget->data.menu.filter.terms);
        free(widget->data.menu.filter.visible_items);
        free(widget->data.menu.filter.query);
        free(widget->data.menu.filter.saved_query);
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
    size_t selected;

    if (widget == NULL ||
        (widget->type != TUI_WIDGET_MENU &&
         widget->type != TUI_WIDGET_ACTION_DIALOG)) {
        return 0;
    }
    selected = widget->data.menu.selected;
    return tui_menu_visible_item(widget, selected);
}

tui_status tui_menu_set_selected(tui_widget *widget, size_t selected)
{
    size_t visible;

    if (widget == NULL ||
        (widget->type != TUI_WIDGET_MENU &&
         widget->type != TUI_WIDGET_ACTION_DIALOG) ||
        selected >= widget->data.menu.count) {
        return TUI_ERR_ARGUMENT;
    }
    for (visible = 0; visible < tui_menu_visible_count(widget); visible++) {
        if (tui_menu_visible_item(widget, visible) == selected) {
            widget->data.menu.selected = visible;
            return TUI_OK;
        }
    }
    return TUI_ERR_ARGUMENT;
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

size_t tui_menu_visible_count(const tui_widget *widget)
{
    if (widget == NULL ||
        (widget->type != TUI_WIDGET_MENU &&
         widget->type != TUI_WIDGET_ACTION_DIALOG)) {
        return 0;
    }
    return widget->data.menu.filter.terms == NULL ?
           widget->data.menu.count : widget->data.menu.filter.visible_count;
}

size_t tui_menu_visible_item(const tui_widget *widget, size_t visible)
{
    size_t count = tui_menu_visible_count(widget);

    if (visible >= count) {
        return 0;
    }
    return widget->data.menu.filter.terms == NULL ?
           visible : widget->data.menu.filter.visible_items[visible];
}

static unsigned char ascii_lower(unsigned char character)
{
    if (character >= 'A' && character <= 'Z') {
        return (unsigned char)(character + ('a' - 'A'));
    }
    return character;
}

static bool contains_case_insensitive(const char *value, const char *query)
{
    size_t value_index;
    size_t query_index;

    if (query[0] == '\0') {
        return true;
    }
    for (value_index = 0; value[value_index] != '\0'; value_index++) {
        for (query_index = 0;
             query[query_index] != '\0' &&
             value[value_index + query_index] != '\0' &&
             ascii_lower((unsigned char)value[value_index + query_index]) ==
                 ascii_lower((unsigned char)query[query_index]);
             query_index++) {
        }
        if (query[query_index] == '\0') {
            return true;
        }
    }
    return false;
}

static void menu_apply_filter(tui_widget *widget)
{
    size_t visible_count = 0;
    size_t index;

    for (index = 0;
         index < widget->data.menu.filter.searchable_count; index++) {
        if (contains_case_insensitive(widget->data.menu.filter.terms[index],
                                      widget->data.menu.filter.query)) {
            widget->data.menu.filter.visible_items[visible_count++] = index;
        }
    }
    widget->data.menu.filter.match_count = visible_count;
    for (index = widget->data.menu.filter.searchable_count;
         index < widget->data.menu.count; index++) {
        widget->data.menu.filter.visible_items[visible_count++] = index;
    }
    widget->data.menu.filter.visible_count = visible_count;
    widget->data.menu.selected = 0;
    widget->data.menu.scroll = 0;
}

static bool menu_filter_begin_editing(tui_widget *widget)
{
    char *saved_query = tui_strdup(widget->data.menu.filter.query);

    if (saved_query == NULL) {
        return true;
    }
    free(widget->data.menu.filter.saved_query);
    widget->data.menu.filter.saved_query = saved_query;
    widget->data.menu.filter.saved_length =
        widget->data.menu.filter.length;
    widget->data.menu.filter.state = TUI_MENU_FILTER_EDITING;
    return true;
}

static void menu_filter_finish_editing(tui_widget *widget)
{
    free(widget->data.menu.filter.saved_query);
    widget->data.menu.filter.saved_query = NULL;
    widget->data.menu.filter.saved_length = 0;
    widget->data.menu.filter.state =
        widget->data.menu.filter.length == 0 ?
            TUI_MENU_FILTER_INACTIVE : TUI_MENU_FILTER_APPLIED;
}

static void menu_filter_cancel_editing(tui_widget *widget)
{
    memcpy(widget->data.menu.filter.query,
           widget->data.menu.filter.saved_query,
           widget->data.menu.filter.saved_length + 1);
    widget->data.menu.filter.length =
        widget->data.menu.filter.saved_length;
    menu_filter_finish_editing(widget);
    menu_apply_filter(widget);
}

static void menu_filter_clear(tui_widget *widget)
{
    widget->data.menu.filter.query[0] = '\0';
    widget->data.menu.filter.length = 0;
    menu_apply_filter(widget);
}

static bool menu_filter_insert(tui_widget *widget, unsigned int character)
{
    char *filter;
    size_t capacity;

    if (character < 32 || character > 126) {
        return false;
    }
    if (widget->data.menu.filter.length + 2 >
        widget->data.menu.filter.capacity) {
        if (widget->data.menu.filter.capacity > SIZE_MAX / 2) {
            return true;
        }
        capacity = widget->data.menu.filter.capacity * 2;
        filter = realloc(widget->data.menu.filter.query, capacity);
        if (filter == NULL) {
            return true;
        }
        widget->data.menu.filter.query = filter;
        widget->data.menu.filter.capacity = capacity;
    }
    widget->data.menu.filter.query[widget->data.menu.filter.length++] =
        (char)character;
    widget->data.menu.filter.query[widget->data.menu.filter.length] = '\0';
    menu_apply_filter(widget);
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
        size_t visible_count = tui_menu_visible_count(widget);
        bool filter_enabled = widget->data.menu.filter.terms != NULL;
        bool filter_editing =
            widget->data.menu.filter.state == TUI_MENU_FILTER_EDITING;

        if (filter_enabled) {
            if (!filter_editing &&
                event->type == TUI_EVENT_CHARACTER &&
                event->character == '/') {
                return menu_filter_begin_editing(widget);
            }
            if (filter_editing &&
                event->type == TUI_EVENT_CHARACTER) {
                return menu_filter_insert(widget, event->character);
            }
            if (filter_editing &&
                event->type == TUI_EVENT_BACKSPACE) {
                if (widget->data.menu.filter.length > 0) {
                    widget->data.menu.filter.query[
                        --widget->data.menu.filter.length] = '\0';
                    menu_apply_filter(widget);
                }
                return true;
            }
            if (filter_editing &&
                event->type == TUI_EVENT_CLEAR_LINE) {
                menu_filter_clear(widget);
                return true;
            }
            if (filter_editing &&
                event->type == TUI_EVENT_ENTER) {
                menu_filter_finish_editing(widget);
                return true;
            }
            if (filter_editing &&
                event->type == TUI_EVENT_ESCAPE) {
                menu_filter_cancel_editing(widget);
                return true;
            }
            if (!filter_editing &&
                widget->data.menu.filter.state ==
                    TUI_MENU_FILTER_APPLIED &&
                event->type == TUI_EVENT_ESCAPE) {
                menu_filter_clear(widget);
                widget->data.menu.filter.state =
                    TUI_MENU_FILTER_INACTIVE;
                return true;
            }
        }
        if (event->type == TUI_EVENT_UP) {
            if (widget->data.menu.selected > 0) {
                widget->data.menu.selected--;
            }
            return true;
        }
        if (event->type == TUI_EVENT_DOWN) {
            if (widget->data.menu.selected + 1 < visible_count) {
                widget->data.menu.selected++;
            }
            return true;
        }
        if (event->type == TUI_EVENT_HOME) {
            if (visible_count > 0) {
                widget->data.menu.selected = 0;
            }
            return true;
        }
        if (event->type == TUI_EVENT_END) {
            if (visible_count > 0) {
                widget->data.menu.selected = visible_count - 1;
            }
            return true;
        }
        if (event->type == TUI_EVENT_ENTER) {
            if (visible_count > 0 &&
                widget->data.menu.callback != NULL) {
                widget->data.menu.callback(app, widget,
                                           tui_menu_selected(widget),
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
        if (event->type == TUI_EVENT_CLEAR_LINE) {
            widget->data.input.value[0] = '\0';
            widget->data.input.length = 0;
            widget->data.input.cursor = 0;
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
