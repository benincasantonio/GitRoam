#include "tui_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #condition);                                             \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    tui_event *events;
    size_t event_count;
    size_t next_event;
    int init_count;
    int shutdown_count;
    char selected_line[128];
    bool saw_filter_q;
    bool saw_no_matches;
} fake_context;

static tui_status fake_init(void *opaque)
{
    fake_context *context = opaque;
    context->init_count++;
    return TUI_OK;
}

static void fake_shutdown(void *opaque)
{
    fake_context *context = opaque;
    context->shutdown_count++;
}

static void fake_clear(void *opaque)
{
    (void)opaque;
}

static void fake_size(void *opaque, int *rows, int *columns)
{
    (void)opaque;
    *rows = 8;
    *columns = 50;
}

static void fake_draw_text(void *opaque, int row, int column, int width,
                           const char *text, unsigned int style)
{
    fake_context *context = opaque;

    (void)row;
    (void)column;
    (void)width;
    if (strcmp(text, "Filter: q") == 0) {
        context->saw_filter_q = true;
    }
    if (strcmp(text, "No matching items") == 0) {
        context->saw_no_matches = true;
    }
    if ((style & TUI_STYLE_SELECTED) != 0U) {
        (void)snprintf(context->selected_line,
                       sizeof(context->selected_line), "%s", text);
    }
}

static void fake_draw_hline(void *opaque, int row, int column, int width,
                            unsigned int style)
{
    (void)opaque;
    (void)row;
    (void)column;
    (void)width;
    (void)style;
}

static void fake_present(void *opaque)
{
    (void)opaque;
}

static tui_status fake_read_event(void *opaque, tui_event *event)
{
    fake_context *context = opaque;

    if (context->next_event >= context->event_count) {
        event->type = TUI_EVENT_QUIT;
        event->character = 0;
    } else {
        *event = context->events[context->next_event++];
    }
    return TUI_OK;
}

static tui_status fake_suspend(void *opaque)
{
    (void)opaque;
    return TUI_OK;
}

static tui_status fake_resume(void *opaque)
{
    (void)opaque;
    return TUI_OK;
}

static tui_backend *fake_backend(fake_context *context)
{
    tui_backend *backend = calloc(1, sizeof(*backend));

    if (backend == NULL) {
        return NULL;
    }
    backend->context = context;
    backend->init = fake_init;
    backend->shutdown = fake_shutdown;
    backend->clear = fake_clear;
    backend->size = fake_size;
    backend->draw_text = fake_draw_text;
    backend->draw_hline = fake_draw_hline;
    backend->present = fake_present;
    backend->read_event = fake_read_event;
    backend->suspend = fake_suspend;
    backend->resume = fake_resume;
    return backend;
}

typedef struct {
    size_t selected;
    int calls;
} menu_result;

static void menu_callback(tui_app *app, tui_widget *widget, size_t selected,
                          void *opaque)
{
    menu_result *result = opaque;

    (void)widget;
    result->selected = selected;
    result->calls++;
    tui_app_stop(app);
}

static int test_menu_navigation(void)
{
    static const char *labels[] = { "one", "two", "three", "four", "five" };
    tui_event events[] = {
        { TUI_EVENT_UP, 0 },
        { TUI_EVENT_DOWN, 0 },
        { TUI_EVENT_DOWN, 0 },
        { TUI_EVENT_DOWN, 0 },
        { TUI_EVENT_DOWN, 0 },
        { TUI_EVENT_DOWN, 0 },
        { TUI_EVENT_ENTER, 0 }
    };
    fake_context context = {
        .events = events,
        .event_count = sizeof(events) / sizeof(events[0])
    };
    menu_result result = { 0 };
    tui_app *app = NULL;
    tui_screen *screen = NULL;
    tui_widget *menu = NULL;

    CHECK(tui_app_create_with_backend(&app, fake_backend(&context)) == TUI_OK);
    CHECK(tui_screen_create(&screen, "menu", NULL, NULL) == TUI_OK);
    CHECK(tui_menu_create(&menu, labels, 5, menu_callback, &result) == TUI_OK);
    CHECK(tui_screen_add_widget(screen, menu) == TUI_OK);
    menu = NULL;
    CHECK(tui_app_push_screen(app, screen) == TUI_OK);
    screen = NULL;
    CHECK(tui_app_run(app) == TUI_OK);
    CHECK(result.calls == 1);
    CHECK(result.selected == 4);
    CHECK(strstr(context.selected_line, "five") != NULL);
    tui_app_destroy(app);
    CHECK(context.init_count == 1);
    CHECK(context.shutdown_count == 1);
    return 0;
}

static int run_filtered_menu(tui_event *events, size_t event_count,
                             size_t searchable_count,
                             menu_result *result, fake_context *context)
{
    static const char *labels[] = {
        "Feature Alpha", "Feature Quick", "Detached worktree", "Back"
    };
    static const char *search_terms[] = {
        "feature/alpha project-one",
        "feature/quick project-two",
        "detached scratch",
        "back"
    };
    tui_app *app = NULL;
    tui_screen *screen = NULL;
    tui_widget *menu = NULL;

    context->events = events;
    context->event_count = event_count;
    CHECK(tui_app_create_with_backend(&app, fake_backend(context)) == TUI_OK);
    CHECK(tui_screen_create(&screen, "filtered menu", NULL, NULL) == TUI_OK);
    CHECK(tui_menu_create(&menu, labels, 4, menu_callback, result) == TUI_OK);
    CHECK(tui_menu_enable_filter(menu, search_terms,
                                 searchable_count) == TUI_OK);
    CHECK(tui_screen_add_widget(screen, menu) == TUI_OK);
    menu = NULL;
    CHECK(tui_app_push_screen(app, screen) == TUI_OK);
    screen = NULL;
    CHECK(tui_app_run(app) == TUI_OK);
    tui_app_destroy(app);
    CHECK(context->init_count == 1);
    CHECK(context->shutdown_count == 1);
    return 0;
}

static int test_menu_filter_matching(void)
{
    tui_event events[] = {
        { TUI_EVENT_CHARACTER, 'P' },
        { TUI_EVENT_CHARACTER, 'R' },
        { TUI_EVENT_CHARACTER, 'O' },
        { TUI_EVENT_CHARACTER, 'J' },
        { TUI_EVENT_CHARACTER, 'E' },
        { TUI_EVENT_CHARACTER, 'C' },
        { TUI_EVENT_CHARACTER, 'T' },
        { TUI_EVENT_CHARACTER, '-' },
        { TUI_EVENT_CHARACTER, 'T' },
        { TUI_EVENT_CHARACTER, 'W' },
        { TUI_EVENT_CHARACTER, 'O' },
        { TUI_EVENT_ENTER, 0 }
    };
    fake_context context = { 0 };
    menu_result result = { 0 };

    CHECK(run_filtered_menu(
              events, sizeof(events) / sizeof(events[0]), 3,
              &result, &context) == 0);
    CHECK(result.calls == 1);
    CHECK(result.selected == 1);
    CHECK(strstr(context.selected_line, "Feature Quick") != NULL);
    return 0;
}

static int test_menu_filter_editing(void)
{
    tui_event events[] = {
        { TUI_EVENT_CHARACTER, 'z' },
        { TUI_EVENT_BACKSPACE, 0 },
        { TUI_EVENT_CHARACTER, 'D' },
        { TUI_EVENT_CHARACTER, 'E' },
        { TUI_EVENT_CHARACTER, 'T' },
        { TUI_EVENT_ENTER, 0 }
    };
    fake_context context = { 0 };
    menu_result result = { 0 };

    CHECK(run_filtered_menu(
              events, sizeof(events) / sizeof(events[0]), 3,
              &result, &context) == 0);
    CHECK(context.saw_no_matches);
    CHECK(result.calls == 1);
    CHECK(result.selected == 2);
    return 0;
}

static int test_menu_filter_escape_and_q(void)
{
    tui_event events[] = {
        { TUI_EVENT_CHARACTER, 'q' },
        { TUI_EVENT_ESCAPE, 0 },
        { TUI_EVENT_ENTER, 0 }
    };
    fake_context context = { 0 };
    menu_result result = { 0 };

    CHECK(run_filtered_menu(
              events, sizeof(events) / sizeof(events[0]), 3,
              &result, &context) == 0);
    CHECK(context.saw_filter_q);
    CHECK(result.calls == 1);
    CHECK(result.selected == 0);
    return 0;
}

static int test_menu_filter_pinned_and_empty_results(void)
{
    tui_event pinned_events[] = {
        { TUI_EVENT_CHARACTER, 'z' },
        { TUI_EVENT_ENTER, 0 }
    };
    tui_event empty_events[] = {
        { TUI_EVENT_CHARACTER, 'z' }
    };
    fake_context pinned_context = { 0 };
    fake_context empty_context = { 0 };
    menu_result pinned_result = { 0 };
    menu_result empty_result = { 0 };

    CHECK(run_filtered_menu(
              pinned_events,
              sizeof(pinned_events) / sizeof(pinned_events[0]), 3,
              &pinned_result, &pinned_context) == 0);
    CHECK(pinned_context.saw_no_matches);
    CHECK(pinned_result.calls == 1);
    CHECK(pinned_result.selected == 3);

    CHECK(run_filtered_menu(
              empty_events,
              sizeof(empty_events) / sizeof(empty_events[0]), 4,
              &empty_result, &empty_context) == 0);
    CHECK(empty_context.saw_no_matches);
    CHECK(empty_result.calls == 0);
    return 0;
}

typedef struct {
    char value[64];
    int calls;
} input_result;

static void input_callback(tui_app *app, tui_widget *widget,
                           const char *value, void *opaque)
{
    input_result *result = opaque;

    (void)widget;
    (void)snprintf(result->value, sizeof(result->value), "%s", value);
    result->calls++;
    tui_app_stop(app);
}

static int test_input_editing(void)
{
    tui_event events[] = {
        { TUI_EVENT_LEFT, 0 },
        { TUI_EVENT_CHARACTER, 'b' },
        { TUI_EVENT_END, 0 },
        { TUI_EVENT_BACKSPACE, 0 },
        { TUI_EVENT_CHARACTER, 'c' },
        { TUI_EVENT_ENTER, 0 }
    };
    fake_context context = {
        .events = events,
        .event_count = sizeof(events) / sizeof(events[0])
    };
    input_result result = { { 0 }, 0 };
    tui_app *app = NULL;
    tui_screen *screen = NULL;
    tui_widget *input = NULL;

    CHECK(tui_app_create_with_backend(&app, fake_backend(&context)) == TUI_OK);
    CHECK(tui_screen_create(&screen, "input", NULL, NULL) == TUI_OK);
    CHECK(tui_text_input_create(&input, "value", "ac", input_callback,
                                &result) == TUI_OK);
    CHECK(tui_screen_add_widget(screen, input) == TUI_OK);
    input = NULL;
    CHECK(tui_app_push_screen(app, screen) == TUI_OK);
    screen = NULL;
    CHECK(tui_app_run(app) == TUI_OK);
    CHECK(result.calls == 1);
    CHECK(strcmp(result.value, "abc") == 0);
    tui_app_destroy(app);
    return 0;
}

static void count_destroy(void *opaque)
{
    int *count = opaque;
    (*count)++;
}

static int test_screen_stack_and_ownership(void)
{
    fake_context context = { 0 };
    tui_app *app = NULL;
    tui_screen *first = NULL;
    tui_screen *second = NULL;
    tui_screen *replacement = NULL;
    int destroyed = 0;

    CHECK(tui_app_create_with_backend(&app, fake_backend(&context)) == TUI_OK);
    CHECK(tui_screen_create(&first, "first", &destroyed,
                            count_destroy) == TUI_OK);
    CHECK(tui_screen_create(&second, "second", &destroyed,
                            count_destroy) == TUI_OK);
    CHECK(tui_screen_create(&replacement, "replacement", &destroyed,
                            count_destroy) == TUI_OK);
    CHECK(tui_app_push_screen(app, first) == TUI_OK);
    first = NULL;
    CHECK(tui_app_push_screen(app, second) == TUI_OK);
    second = NULL;
    CHECK(tui_app_screen_count(app) == 2);
    CHECK(tui_app_replace_screen(app, replacement) == TUI_OK);
    replacement = NULL;
    CHECK(destroyed == 1);
    CHECK(tui_app_pop_screen(app) == TUI_OK);
    CHECK(destroyed == 2);
    CHECK(tui_app_screen_count(app) == 1);
    tui_app_destroy(app);
    CHECK(destroyed == 3);
    return 0;
}

int main(void)
{
    if (test_menu_navigation() != 0 ||
        test_menu_filter_matching() != 0 ||
        test_menu_filter_editing() != 0 ||
        test_menu_filter_escape_and_q() != 0 ||
        test_menu_filter_pinned_and_empty_results() != 0 ||
        test_input_editing() != 0 ||
        test_screen_stack_and_ownership() != 0) {
        return EXIT_FAILURE;
    }
    puts("test-tui: ok");
    return EXIT_SUCCESS;
}
