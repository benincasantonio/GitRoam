#include "tui_backend.h"

#include <curses.h>
#include <stdlib.h>

typedef struct {
    bool active;
    bool colors;
} ncurses_context;

static tui_status backend_init(void *opaque)
{
    ncurses_context *context = opaque;

    if (initscr() == NULL) {
        return TUI_ERR_TERMINAL;
    }
    context->active = true;
    (void)cbreak();
    (void)noecho();
    (void)keypad(stdscr, TRUE);
    (void)curs_set(0);
    context->colors = has_colors();
    if (context->colors) {
        (void)start_color();
        (void)use_default_colors();
        (void)init_pair(1, COLOR_BLACK, COLOR_CYAN);
        (void)init_pair(2, COLOR_CYAN, -1);
        (void)init_pair(3, COLOR_RED, -1);
    }
    return TUI_OK;
}

static void backend_shutdown(void *opaque)
{
    ncurses_context *context = opaque;

    if (context->active) {
        (void)endwin();
        context->active = false;
    }
}

static void backend_clear(void *opaque)
{
    (void)opaque;
    (void)erase();
}

static void backend_size(void *opaque, int *rows, int *columns)
{
    (void)opaque;
    getmaxyx(stdscr, *rows, *columns);
}

static attr_t style_attributes(ncurses_context *context, unsigned int style)
{
    attr_t attributes = A_NORMAL;

    if ((style & TUI_STYLE_SELECTED) != 0U) {
        attributes |= context->colors ? COLOR_PAIR(1) : A_REVERSE;
    }
    if ((style & TUI_STYLE_TITLE) != 0U) {
        attributes |= A_BOLD;
        if (context->colors) {
            attributes |= COLOR_PAIR(2);
        }
    }
    if ((style & TUI_STYLE_DIM) != 0U) {
        attributes |= A_DIM;
    }
    if ((style & TUI_STYLE_ERROR) != 0U) {
        attributes |= A_BOLD;
        if (context->colors) {
            attributes |= COLOR_PAIR(3);
        }
    }
    return attributes;
}

static void backend_draw_text(void *opaque, int row, int column,
                              int max_width, const char *text,
                              unsigned int style)
{
    ncurses_context *context = opaque;
    int rows;
    int columns;
    attr_t attributes;

    getmaxyx(stdscr, rows, columns);
    if (row < 0 || row >= rows || column < 0 || column >= columns ||
        max_width <= 0 || text == NULL) {
        return;
    }
    if (max_width > columns - column) {
        max_width = columns - column;
    }
    attributes = style_attributes(context, style);
    (void)attron(attributes);
    (void)mvaddnstr(row, column, text, max_width);
    (void)attroff(attributes);
}

static void backend_draw_hline(void *opaque, int row, int column,
                               int width, unsigned int style)
{
    ncurses_context *context = opaque;
    attr_t attributes = style_attributes(context, style);

    if (width <= 0) {
        return;
    }
    (void)attron(attributes);
    (void)mvhline(row, column, ACS_HLINE, width);
    (void)attroff(attributes);
}

static void backend_present(void *opaque)
{
    (void)opaque;
    (void)refresh();
}

static tui_status backend_read_event(void *opaque, tui_event *event)
{
    int key;

    (void)opaque;
    key = getch();
    event->character = 0;
    switch (key) {
    case KEY_UP:
        event->type = TUI_EVENT_UP;
        break;
    case KEY_DOWN:
        event->type = TUI_EVENT_DOWN;
        break;
    case KEY_LEFT:
        event->type = TUI_EVENT_LEFT;
        break;
    case KEY_RIGHT:
        event->type = TUI_EVENT_RIGHT;
        break;
    case KEY_HOME:
        event->type = TUI_EVENT_HOME;
        break;
    case KEY_END:
        event->type = TUI_EVENT_END;
        break;
    case KEY_BACKSPACE:
    case 127:
    case 8:
        event->type = TUI_EVENT_BACKSPACE;
        break;
    case KEY_DC:
        event->type = TUI_EVENT_DELETE;
        break;
    case KEY_RESIZE:
        event->type = TUI_EVENT_RESIZE;
        break;
    case '\n':
    case '\r':
    case KEY_ENTER:
        event->type = TUI_EVENT_ENTER;
        break;
    case 27:
        event->type = TUI_EVENT_ESCAPE;
        break;
    case ERR:
        return TUI_ERR_TERMINAL;
    default:
        if (key >= 32 && key <= 126) {
            event->type = TUI_EVENT_CHARACTER;
            event->character = (unsigned int)key;
        } else {
            event->type = TUI_EVENT_NONE;
        }
        break;
    }
    return TUI_OK;
}

static tui_status backend_suspend(void *opaque)
{
    ncurses_context *context = opaque;

    if (!context->active) {
        return TUI_ERR_STATE;
    }
    (void)def_prog_mode();
    (void)endwin();
    return TUI_OK;
}

static tui_status backend_resume(void *opaque)
{
    ncurses_context *context = opaque;

    if (!context->active) {
        return TUI_ERR_STATE;
    }
    (void)reset_prog_mode();
    (void)refresh();
    (void)keypad(stdscr, TRUE);
    (void)curs_set(0);
    return TUI_OK;
}

static void backend_destroy(void *opaque)
{
    free(opaque);
}

tui_backend *tui_ncurses_backend_create(void)
{
    tui_backend *backend = calloc(1, sizeof(*backend));
    ncurses_context *context = calloc(1, sizeof(*context));

    if (backend == NULL || context == NULL) {
        free(backend);
        free(context);
        return NULL;
    }
    backend->context = context;
    backend->init = backend_init;
    backend->shutdown = backend_shutdown;
    backend->clear = backend_clear;
    backend->size = backend_size;
    backend->draw_text = backend_draw_text;
    backend->draw_hline = backend_draw_hline;
    backend->present = backend_present;
    backend->read_event = backend_read_event;
    backend->suspend = backend_suspend;
    backend->resume = backend_resume;
    backend->destroy = backend_destroy;
    return backend;
}
