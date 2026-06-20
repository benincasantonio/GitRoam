#ifndef TUI_BACKEND_H
#define TUI_BACKEND_H

#include "tui.h"

enum {
    TUI_STYLE_NORMAL = 0,
    TUI_STYLE_SELECTED = 1 << 0,
    TUI_STYLE_TITLE = 1 << 1,
    TUI_STYLE_DIM = 1 << 2,
    TUI_STYLE_ERROR = 1 << 3
};

typedef struct tui_backend {
    void *context;
    tui_status (*init)(void *context);
    void (*shutdown)(void *context);
    void (*clear)(void *context);
    void (*size)(void *context, int *rows, int *columns);
    void (*draw_text)(void *context, int row, int column, int max_width,
                      const char *text, unsigned int style);
    void (*draw_hline)(void *context, int row, int column, int width,
                       unsigned int style);
    void (*present)(void *context);
    tui_status (*read_event)(void *context, tui_event *event);
    tui_status (*suspend)(void *context);
    tui_status (*resume)(void *context);
    void (*destroy)(void *context);
} tui_backend;

tui_status tui_app_create_with_backend(tui_app **out_app,
                                       tui_backend *backend);
tui_backend *tui_ncurses_backend_create(void);

#endif
