#include "tui_internal.h"

#include <stdio.h>
#include <string.h>

static void draw_box(tui_backend *backend, int top, int left,
                     int height, int width)
{
    int row;

    if (height < 2 || width < 2) {
        return;
    }
    backend->draw_text(backend->context, top, left, width, "+",
                       TUI_STYLE_DIM);
    backend->draw_hline(backend->context, top, left + 1, width - 2,
                        TUI_STYLE_DIM);
    backend->draw_text(backend->context, top, left + width - 1, 1, "+",
                       TUI_STYLE_DIM);
    for (row = top + 1; row < top + height - 1; row++) {
        backend->draw_text(backend->context, row, left, 1, "|",
                           TUI_STYLE_DIM);
        backend->draw_text(backend->context, row, left + width - 1, 1, "|",
                           TUI_STYLE_DIM);
    }
    backend->draw_text(backend->context, top + height - 1, left, 1, "+",
                       TUI_STYLE_DIM);
    backend->draw_hline(backend->context, top + height - 1, left + 1,
                        width - 2, TUI_STYLE_DIM);
    backend->draw_text(backend->context, top + height - 1,
                       left + width - 1, 1, "+", TUI_STYLE_DIM);
}

static void render_menu(tui_backend *backend, tui_widget *widget,
                        int top, int left, int height, int width)
{
    size_t index;
    size_t item_count = tui_menu_visible_count(widget);
    size_t visible;

    if (widget->data.menu.filter.terms != NULL) {
        char filter_line[1024];
        tui_menu_filter_state state = widget->data.menu.filter.state;

        if (state == TUI_MENU_FILTER_EDITING) {
            (void)snprintf(filter_line, sizeof(filter_line),
                           "[FILTER] %s_", widget->data.menu.filter.query);
        } else if (state == TUI_MENU_FILTER_APPLIED) {
            (void)snprintf(filter_line, sizeof(filter_line), "Filter: %s",
                           widget->data.menu.filter.query);
        } else {
            (void)snprintf(filter_line, sizeof(filter_line),
                           "Filter: press / to search");
        }
        backend->draw_text(backend->context, top, left, width, filter_line,
                           TUI_STYLE_TITLE);
        if (state == TUI_MENU_FILTER_EDITING) {
            backend->draw_text(
                backend->context, top + 1, left, width,
                "Enter: apply  Esc: cancel  Ctrl-U: clear",
                TUI_STYLE_DIM);
        }
        top += 2;
        height -= 2;
        if (widget->data.menu.filter.length > 0 &&
            widget->data.menu.filter.match_count == 0) {
            backend->draw_text(backend->context, top, left, width,
                               "No matching items", TUI_STYLE_DIM);
            top++;
            height--;
        }
    }
    visible = height > 0 ? (size_t)height : 0;

    if (widget->data.menu.selected < widget->data.menu.scroll) {
        widget->data.menu.scroll = widget->data.menu.selected;
    }
    if (visible > 0 &&
        widget->data.menu.selected >= widget->data.menu.scroll + visible) {
        widget->data.menu.scroll =
            widget->data.menu.selected - visible + 1;
    }
    for (index = 0; index < visible; index++) {
        size_t item = widget->data.menu.scroll + index;
        char prefix[4] = "   ";
        char line[1024];
        unsigned int style = TUI_STYLE_NORMAL;

        if (item >= item_count) {
            break;
        }
        if (item == widget->data.menu.selected) {
            memcpy(prefix, " > ", 4);
            style = TUI_STYLE_SELECTED;
        }
        (void)snprintf(line, sizeof(line), "%s%s", prefix,
                       widget->data.menu.labels[
                           tui_menu_visible_item(widget, item)]);
        backend->draw_text(backend->context, top + (int)index, left, width,
                           line, style);
    }
}

static void render_action_dialog(tui_backend *backend, tui_widget *widget,
                                 int rows, int columns)
{
    int width = columns > 64 ? 64 : columns - 2;
    int height = (int)widget->data.menu.count + 5;
    int top;
    int left;

    if (width < 20) {
        width = columns;
    }
    if (height > rows) {
        height = rows;
    }
    top = (rows - height) / 2;
    left = (columns - width) / 2;
    draw_box(backend, top, left, height, width);
    backend->draw_text(backend->context, top + 1, left + 2, width - 4,
                       widget->data.menu.message, TUI_STYLE_TITLE);
    render_menu(backend, widget, top + 3, left + 1, height - 4, width - 2);
}

static void render_input(tui_backend *backend, tui_widget *widget,
                         int rows, int columns)
{
    int width = columns > 72 ? 72 : columns - 2;
    int top = (rows - 7) / 2;
    int left;
    char line[1024];
    size_t cursor = widget->data.input.cursor;

    if (width < 20) {
        width = columns;
    }
    left = (columns - width) / 2;
    draw_box(backend, top, left, 7, width);
    backend->draw_text(backend->context, top + 1, left + 2, width - 4,
                       widget->data.input.prompt, TUI_STYLE_TITLE);
    backend->draw_text(backend->context, top + 3, left + 2, width - 4,
                       widget->data.input.value, TUI_STYLE_SELECTED);
    if (cursor < sizeof(line) - 2) {
        (void)snprintf(line, sizeof(line), "%*s^", (int)cursor, "");
        backend->draw_text(backend->context, top + 4, left + 2, width - 4,
                           line, TUI_STYLE_DIM);
    }
    backend->draw_text(backend->context, top + 5, left + 2, width - 4,
                       "Enter: confirm  Esc: cancel", TUI_STYLE_DIM);
}

static void render_message(tui_backend *backend, tui_widget *widget,
                           int rows, int columns)
{
    int width = columns > 72 ? 72 : columns - 2;
    int top = (rows - 7) / 2;
    int left;

    if (width < 20) {
        width = columns;
    }
    left = (columns - width) / 2;
    draw_box(backend, top, left, 7, width);
    backend->draw_text(backend->context, top + 2, left + 2, width - 4,
                       widget->data.dialog.message, TUI_STYLE_NORMAL);
    backend->draw_text(backend->context, top + 4, left + 2, width - 4,
                       "Enter or Esc: close", TUI_STYLE_DIM);
}

void tui_render(tui_app *app)
{
    tui_backend *backend = app->backend;
    tui_screen *screen = tui_app_current_screen(app);
    tui_widget *primary = NULL;
    tui_widget *status = NULL;
    int rows = 0;
    int columns = 0;
    size_t index;

    backend->size(backend->context, &rows, &columns);
    backend->clear(backend->context);
    if (screen == NULL || rows <= 0 || columns <= 0) {
        backend->present(backend->context);
        return;
    }
    backend->draw_text(backend->context, 0, 0, columns, screen->title,
                       TUI_STYLE_TITLE);
    if (rows > 1) {
        backend->draw_hline(backend->context, 1, 0, columns, TUI_STYLE_DIM);
    }
    for (index = 0; index < screen->widget_count; index++) {
        if (screen->widgets[index]->type == TUI_WIDGET_STATUS_BAR) {
            status = screen->widgets[index];
        } else {
            primary = screen->widgets[index];
        }
    }
    if (status != NULL && rows > 2) {
        backend->draw_hline(backend->context, rows - 2, 0, columns,
                            TUI_STYLE_DIM);
        backend->draw_text(backend->context, rows - 1, 0, columns,
                           status->data.status.text, TUI_STYLE_DIM);
    }
    if (primary != NULL) {
        int bottom = status == NULL ? rows : rows - 2;
        switch (primary->type) {
        case TUI_WIDGET_MENU:
            render_menu(backend, primary, 3, 1, bottom - 4, columns - 2);
            break;
        case TUI_WIDGET_ACTION_DIALOG:
            render_action_dialog(backend, primary, bottom, columns);
            break;
        case TUI_WIDGET_TEXT_INPUT:
            render_input(backend, primary, bottom, columns);
            break;
        case TUI_WIDGET_MESSAGE_DIALOG:
            render_message(backend, primary, bottom, columns);
            break;
        case TUI_WIDGET_STATUS_BAR:
            break;
        }
    }
    backend->present(backend->context);
}
