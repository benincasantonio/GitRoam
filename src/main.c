#include "app_ui.h"
#include "discovery.h"
#include "git.h"
#include "tui.h"
#include "worktree_cleanup.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define GITROAM_VERSION "0.1.0"

typedef struct {
    char *root;
    discovery_options discovery;
    const char **excluded_names;
    git_repository_list repositories;
} gitroam_state;

typedef struct {
    gitroam_state *state;
    size_t repository;
} repository_context;

typedef struct {
    gitroam_state *state;
    size_t repository;
    char *branch;
    size_t pop_after_open;
} destination_context;

typedef struct {
    const char *path;
} shell_context;

static int load_repositories(gitroam_state *state, char **error);
static tui_status build_repository_screen(tui_screen **out_screen,
                                          gitroam_state *state);

static void repository_context_destroy(void *opaque)
{
    free(opaque);
}

static void destination_context_destroy(void *opaque)
{
    destination_context *context = opaque;

    if (context != NULL) {
        free(context->branch);
        free(context);
    }
}

static void state_destroy(gitroam_state *state)
{
    if (state == NULL) {
        return;
    }
    free(state->root);
    free(state->excluded_names);
    git_repository_list_destroy(&state->repositories);
}

static repository_context *repository_context_create(gitroam_state *state,
                                                     size_t repository)
{
    repository_context *context = malloc(sizeof(*context));

    if (context != NULL) {
        context->state = state;
        context->repository = repository;
    }
    return context;
}

static bool repository_shortcuts(tui_app *app, tui_screen *screen,
                                 const tui_event *event, void *opaque)
{
    gitroam_state *state = opaque;
    tui_screen *replacement = NULL;
    char *error = NULL;

    (void)screen;
    if (event->type == TUI_EVENT_CHARACTER && event->character == 'r') {
        if (load_repositories(state, &error) != 0) {
            app_show_message(app, "Refresh failed",
                             error == NULL ? "Could not scan repositories."
                                           : error);
            free(error);
            return true;
        }
        if (build_repository_screen(&replacement, state) != TUI_OK ||
            tui_app_replace_screen(app, replacement) != TUI_OK) {
            tui_screen_destroy(replacement);
            app_show_message(app, "Refresh failed",
                             "Could not rebuild the repository list.");
        }
        return true;
    }
    return app_quit_shortcut(app, screen, event, opaque);
}

static int launch_shell(void *opaque)
{
    shell_context *context = opaque;
    const char *shell = getenv("SHELL");
    pid_t child;
    int status;

    if (shell == NULL || shell[0] == '\0') {
        shell = "/bin/sh";
    }
    child = fork();
    if (child < 0) {
        return -1;
    }
    if (child == 0) {
        if (chdir(context->path) != 0) {
            perror("gitroam: chdir");
            _exit(126);
        }
        execlp(shell, shell, "-i", (char *)NULL);
        perror("gitroam: shell");
        _exit(127);
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

static bool open_worktree(tui_app *app, const char *path)
{
    shell_context context = { path };
    int result = 0;
    tui_status status;

    status = tui_app_suspend(app, launch_shell, &context, &result);
    if (status != TUI_OK) {
        app_show_message(app, "Shell error", tui_status_string(status));
        return false;
    } else if (result < 0) {
        app_show_message(app, "Shell error", strerror(errno));
        return false;
    }
    return true;
}

static void destination_submitted(tui_app *app, tui_widget *widget,
                                  const char *value, void *opaque)
{
    destination_context *context = opaque;
    git_repository *repository =
        &context->state->repositories.items[context->repository];
    char *path = NULL;
    char *error = NULL;
    git_create_status status;
    size_t pop_count;

    (void)widget;
    if (value[0] == '\0') {
        app_show_message(app, "Invalid destination",
                         "The destination path cannot be empty.");
        return;
    }
    status = git_create_worktree(repository, context->branch, value, &path,
                                 &error);
    if (status != GIT_CREATE_OK && status != GIT_CREATE_EXISTING) {
        app_show_message(
            app, "Could not create worktree",
            error == NULL ? "Git worktree creation failed." : error);
        free(error);
        return;
    }
    free(error);
    if (path == NULL) {
        app_show_message(app, "Could not open worktree",
                         "Git did not return a worktree path.");
        return;
    }
    pop_count = context->pop_after_open;
    if (!open_worktree(app, path)) {
        free(path);
        return;
    }
    free(path);
    while (pop_count-- > 0 && tui_app_screen_count(app) > 1) {
        (void)tui_app_pop_screen(app);
    }
}

static void push_destination_input(tui_app *app, gitroam_state *state,
                                   size_t repository, const char *branch,
                                   size_t pop_after_open)
{
    git_repository *repo = &state->repositories.items[repository];
    destination_context *context = NULL;
    tui_screen *screen = NULL;
    tui_widget *input = NULL;
    char *destination = NULL;
    char *error = NULL;

    destination = git_default_worktree_path(repo, branch, &error);
    if (destination == NULL) {
        app_show_message(
            app, "Could not choose destination",
            error == NULL ? "Unable to determine the main worktree." : error);
        free(error);
        return;
    }
    context = calloc(1, sizeof(*context));
    if (context != NULL) {
        context->state = state;
        context->repository = repository;
        context->branch = app_string_copy(branch);
        context->pop_after_open = pop_after_open;
    }
    if (context == NULL || context->branch == NULL ||
        tui_screen_create(&screen, "Worktree destination", context,
                          destination_context_destroy) != TUI_OK) {
        destination_context_destroy(context);
        free(destination);
        app_show_message(app, "Out of memory",
                         "Could not create the input.");
        return;
    }
    context = NULL;
    if (tui_text_input_create(&input, "Worktree path", destination,
                              destination_submitted,
                              tui_screen_context(screen)) != TUI_OK ||
        app_screen_take_widget(screen, &input) != TUI_OK ||
        app_screen_add_status(
            screen, "Enter: create and open  Esc: back") != TUI_OK) {
        tui_widget_destroy(input);
        tui_screen_destroy(screen);
        free(destination);
        app_show_message(app, "Out of memory",
                         "Could not create the input.");
        return;
    }
    free(destination);
    if (tui_app_push_screen(app, screen) != TUI_OK) {
        tui_screen_destroy(screen);
        app_show_message(app, "Out of memory", "Could not open the input.");
    }
}

static void branch_submitted(tui_app *app, tui_widget *widget,
                             const char *value, void *opaque)
{
    repository_context *context = opaque;

    (void)widget;
    if (value[0] == '\0') {
        app_show_message(app, "Invalid branch",
                         "The branch name cannot be empty.");
        return;
    }
    push_destination_input(app, context->state, context->repository, value, 2);
}

static void push_branch_input(tui_app *app, gitroam_state *state,
                              size_t repository)
{
    repository_context *context =
        repository_context_create(state, repository);
    tui_screen *screen = NULL;
    tui_widget *input = NULL;

    if (context == NULL ||
        tui_screen_create(&screen, "Create worktree", context,
                          repository_context_destroy) != TUI_OK) {
        free(context);
        app_show_message(app, "Out of memory",
                         "Could not create the input.");
        return;
    }
    context = NULL;
    if (tui_text_input_create(&input, "Branch name", "", branch_submitted,
                              tui_screen_context(screen)) != TUI_OK ||
        app_screen_take_widget(screen, &input) != TUI_OK ||
        app_screen_add_status(screen, "Enter: continue  Esc: back") !=
            TUI_OK) {
        tui_widget_destroy(input);
        tui_screen_destroy(screen);
        app_show_message(app, "Out of memory",
                         "Could not create the input.");
        return;
    }
    if (tui_app_push_screen(app, screen) != TUI_OK) {
        tui_screen_destroy(screen);
        app_show_message(app, "Out of memory", "Could not open the input.");
    }
}

static tui_status build_worktree_screen(tui_screen **out_screen,
                                        gitroam_state *state,
                                        size_t repository);

static void worktree_selected(tui_app *app, tui_widget *widget,
                              size_t selected, void *opaque)
{
    repository_context *context = opaque;
    git_repository *repository =
        &context->state->repositories.items[context->repository];
    git_worktree_list list = { 0 };
    char *error = NULL;
    tui_screen *replacement = NULL;

    (void)widget;
    if (git_worktrees(repository, &list, &error) != 0) {
        app_show_message(app, "Could not list worktrees",
                         error == NULL ? "Git command failed." : error);
        free(error);
        return;
    }
    if (selected >= list.count) {
        git_worktree_list_destroy(&list);
        (void)tui_app_pop_screen(app);
        return;
    }
    open_worktree(app, list.items[selected].path);
    git_worktree_list_destroy(&list);
    if (build_worktree_screen(&replacement, context->state,
                              context->repository) == TUI_OK) {
        (void)tui_app_replace_screen(app, replacement);
    }
}

static tui_status build_worktree_screen(tui_screen **out_screen,
                                        gitroam_state *state,
                                        size_t repository_index)
{
    git_repository *repository =
        &state->repositories.items[repository_index];
    git_worktree_list list = { 0 };
    repository_context *context = NULL;
    tui_screen *screen = NULL;
    tui_widget *menu = NULL;
    char **labels = NULL;
    const char **label_view = NULL;
    char **search_terms = NULL;
    const char **search_view = NULL;
    char *error = NULL;
    size_t index;
    tui_status result = TUI_ERR_MEMORY;
    char title[256];

    if (git_worktrees(repository, &list, &error) != 0) {
        free(error);
        return TUI_ERR_STATE;
    }
    labels = calloc(list.count + 1, sizeof(*labels));
    label_view = calloc(list.count + 1, sizeof(*label_view));
    search_terms = calloc(list.count, sizeof(*search_terms));
    search_view = calloc(list.count, sizeof(*search_view));
    context = repository_context_create(state, repository_index);
    if (labels == NULL || label_view == NULL ||
        (list.count != 0 &&
         (search_terms == NULL || search_view == NULL)) ||
        context == NULL) {
        goto done;
    }
    for (index = 0; index < list.count; index++) {
        const char *branch = list.items[index].branch != NULL ?
                             list.items[index].branch : "(detached)";
        const char *state_label = list.items[index].locked ? " [locked]" :
                                  list.items[index].prunable ? " [prunable]" :
                                  "";
        size_t length = strlen(branch) + strlen(list.items[index].path) +
                        strlen(state_label) + 6;
        labels[index] = malloc(length);
        if (labels[index] == NULL) {
            goto done;
        }
        (void)snprintf(labels[index], length, "%s  %s%s", branch,
                       list.items[index].path, state_label);
        label_view[index] = labels[index];
        search_terms[index] = app_worktree_search_term(
            list.items[index].branch, list.items[index].path);
        search_view[index] = search_terms[index];
        if (search_terms[index] == NULL) {
            goto done;
        }
    }
    labels[list.count] = app_string_copy("Back");
    label_view[list.count] = labels[list.count];
    if (labels[list.count] == NULL) {
        goto done;
    }
    (void)snprintf(title, sizeof(title), "%s worktrees", repository->name);
    if (tui_screen_create(&screen, title, context,
                          repository_context_destroy) != TUI_OK) {
        goto done;
    }
    context = NULL;
    tui_screen_set_event_handler(screen, app_quit_shortcut, NULL);
    if (tui_menu_create(&menu, label_view, list.count + 1,
                        worktree_selected, tui_screen_context(screen)) !=
            TUI_OK ||
        (list.count != 0 &&
         tui_menu_enable_filter(menu, search_view, list.count) != TUI_OK) ||
        app_screen_take_widget(screen, &menu) != TUI_OK ||
        app_screen_add_status(
            screen,
            "/: filter  Arrows: move  Enter: open  "
            "Esc: clear/back  q: quit") !=
            TUI_OK) {
        goto done;
    }
    *out_screen = screen;
    screen = NULL;
    result = TUI_OK;

done:
    for (index = 0; index < list.count + 1; index++) {
        free(labels == NULL ? NULL : labels[index]);
        if (index < list.count) {
            free(search_terms == NULL ? NULL : search_terms[index]);
        }
    }
    free(labels);
    free(label_view);
    free(search_terms);
    free(search_view);
    free(context);
    tui_widget_destroy(menu);
    tui_screen_destroy(screen);
    git_worktree_list_destroy(&list);
    return result;
}

static void push_worktree_screen(tui_app *app, gitroam_state *state,
                                 size_t repository)
{
    tui_screen *screen = NULL;

    if (build_worktree_screen(&screen, state, repository) != TUI_OK) {
        app_show_message(app, "Could not list worktrees",
                         "Git worktree listing failed.");
        return;
    }
    if (tui_app_push_screen(app, screen) != TUI_OK) {
        tui_screen_destroy(screen);
        app_show_message(app, "Out of memory",
                         "Could not open the worktree list.");
    }
}

static void navigate_primary(tui_app *app, gitroam_state *state,
                             size_t repository_index)
{
    git_repository *repository =
        &state->repositories.items[repository_index];
    git_worktree_list list = { 0 };
    char *primary = NULL;
    char *error = NULL;
    size_t index;

    if (git_primary_branch(repository, &primary, &error) != 0) {
        app_show_message(app, "Primary branch",
                         error == NULL ? "No primary branch found." : error);
        free(error);
        return;
    }
    if (git_worktrees(repository, &list, &error) != 0) {
        app_show_message(app, "Could not list worktrees",
                         error == NULL ? "Git command failed." : error);
        free(error);
        free(primary);
        return;
    }
    for (index = 0; index < list.count; index++) {
        if (list.items[index].branch != NULL &&
            strcmp(list.items[index].branch, primary) == 0) {
            open_worktree(app, list.items[index].path);
            git_worktree_list_destroy(&list);
            free(primary);
            return;
        }
    }
    git_worktree_list_destroy(&list);
    push_destination_input(app, state, repository_index, primary, 1);
    free(primary);
}

static void repository_action_selected(tui_app *app, tui_widget *widget,
                                       size_t selected, void *opaque)
{
    repository_context *context = opaque;

    (void)widget;
    switch (selected) {
    case 0:
        navigate_primary(app, context->state, context->repository);
        break;
    case 1:
        push_worktree_screen(app, context->state, context->repository);
        break;
    case 2:
        push_branch_input(app, context->state, context->repository);
        break;
    case 3:
        worktree_cleanup_open(
            app, &context->state->repositories.items[context->repository]);
        break;
    default:
        (void)tui_app_pop_screen(app);
        break;
    }
}

static void push_repository_actions(tui_app *app, gitroam_state *state,
                                    size_t repository)
{
    static const char *actions[] = {
        "Navigate to primary branch",
        "Select worktree",
        "Create worktree",
        "Clean worktrees",
        "Back"
    };
    repository_context *context =
        repository_context_create(state, repository);
    tui_screen *screen = NULL;
    tui_widget *dialog = NULL;
    char title[256];

    if (context == NULL) {
        app_show_message(app, "Out of memory",
                         "Could not open repository actions.");
        return;
    }
    (void)snprintf(title, sizeof(title), "%s actions",
                   state->repositories.items[repository].name);
    if (tui_screen_create(&screen, title, context,
                          repository_context_destroy) != TUI_OK) {
        free(context);
        app_show_message(app, "Out of memory",
                         "Could not open repository actions.");
        return;
    }
    context = NULL;
    tui_screen_set_event_handler(screen, app_quit_shortcut, NULL);
    if (tui_action_dialog_create(&dialog,
                                 state->repositories.items[repository].path,
                                 actions, 5, repository_action_selected,
                                 tui_screen_context(screen)) != TUI_OK ||
        app_screen_take_widget(screen, &dialog) != TUI_OK ||
        app_screen_add_status(
            screen, "Arrows: move  Enter: select  Esc: back  q: quit") !=
            TUI_OK) {
        tui_widget_destroy(dialog);
        tui_screen_destroy(screen);
        app_show_message(app, "Out of memory",
                         "Could not open repository actions.");
        return;
    }
    if (tui_app_push_screen(app, screen) != TUI_OK) {
        tui_screen_destroy(screen);
        app_show_message(app, "Out of memory",
                         "Could not open repository actions.");
    }
}

static void repository_selected(tui_app *app, tui_widget *widget,
                                size_t selected, void *opaque)
{
    gitroam_state *state = opaque;

    (void)widget;
    if (selected < state->repositories.count) {
        push_repository_actions(app, state, selected);
    }
}

static int repository_compare(const void *left, const void *right)
{
    const git_repository *first = left;
    const git_repository *second = right;

    return strcmp(first->path, second->path);
}

static tui_status build_repository_screen(tui_screen **out_screen,
                                          gitroam_state *state)
{
    tui_screen *screen = NULL;
    tui_widget *menu = NULL;
    tui_widget *dialog = NULL;
    char **labels = NULL;
    const char **label_view = NULL;
    size_t index;
    tui_status result = TUI_ERR_MEMORY;

    if (tui_screen_create(&screen, "GitRoam", NULL, NULL) != TUI_OK) {
        return TUI_ERR_MEMORY;
    }
    tui_screen_set_event_handler(screen, repository_shortcuts, state);
    if (state->repositories.count == 0) {
        if (tui_message_dialog_create(&dialog,
                "No Git repositories were found beneath the scan root.",
                NULL, NULL) != TUI_OK ||
            app_screen_take_widget(screen, &dialog) != TUI_OK) {
            goto done;
        }
    } else {
        labels = calloc(state->repositories.count, sizeof(*labels));
        label_view = calloc(state->repositories.count, sizeof(*label_view));
        if (labels == NULL || label_view == NULL) {
            goto done;
        }
        for (index = 0; index < state->repositories.count; index++) {
            git_repository *repository = &state->repositories.items[index];
            size_t length = strlen(repository->name) +
                            strlen(repository->path) + 4;
            labels[index] = malloc(length);
            if (labels[index] == NULL) {
                goto done;
            }
            (void)snprintf(labels[index], length, "%s  %s",
                           repository->name, repository->path);
            label_view[index] = labels[index];
        }
        if (tui_menu_create(&menu, label_view, state->repositories.count,
                            repository_selected, state) != TUI_OK ||
            app_screen_take_widget(screen, &menu) != TUI_OK) {
            goto done;
        }
    }
    if (app_screen_add_status(
            screen,
            "Arrows: move  Enter: actions  r: refresh  Esc/q: quit") !=
            TUI_OK) {
        goto done;
    }
    *out_screen = screen;
    screen = NULL;
    result = TUI_OK;

done:
    for (index = 0; index < state->repositories.count; index++) {
        free(labels == NULL ? NULL : labels[index]);
    }
    free(labels);
    free(label_view);
    tui_widget_destroy(menu);
    tui_widget_destroy(dialog);
    tui_screen_destroy(screen);
    return result;
}

static int load_repositories(gitroam_state *state, char **error)
{
    git_repository_list replacement = { 0 };

    if (discover_repositories_with_options(state->root, &state->discovery,
                                           &replacement, error) != 0) {
        return -1;
    }
    qsort(replacement.items, replacement.count, sizeof(*replacement.items),
          repository_compare);
    git_repository_list_destroy(&state->repositories);
    state->repositories = replacement;
    return 0;
}

static void print_usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "usage: %s [OPTIONS] [ROOT]\n"
            "\n"
            "Options:\n"
            "  --exclude NAME          Skip directories with this name\n"
            "  --max-depth NUMBER      Limit recursive scanning depth\n"
            "  --no-default-excludes   Scan dependency/cache/build directories\n"
            "  -h, --help              Show this help\n"
            "  -V, --version           Show the version\n",
            program);
}

static int parse_depth(const char *value, unsigned int *depth)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed > UINT_MAX) {
        return -1;
    }
    *depth = (unsigned int)parsed;
    return 0;
}

int main(int argc, char **argv)
{
    gitroam_state state = { 0 };
    tui_app *app = NULL;
    tui_screen *screen = NULL;
    const char *root = NULL;
    char current[PATH_MAX];
    char *error = NULL;
    tui_status status;
    int exit_status = EXIT_FAILURE;
    int argument;

    discovery_options_init(&state.discovery);
    state.excluded_names = calloc((size_t)argc, sizeof(*state.excluded_names));
    if (state.excluded_names == NULL) {
        fprintf(stderr, "gitroam: out of memory\n");
        return EXIT_FAILURE;
    }
    for (argument = 1; argument < argc; argument++) {
        const char *value = argv[argument];

        if (strcmp(value, "-h") == 0 || strcmp(value, "--help") == 0) {
            print_usage(stdout, argv[0]);
            state_destroy(&state);
            return EXIT_SUCCESS;
        }
        if (strcmp(value, "-V") == 0 || strcmp(value, "--version") == 0) {
            printf("gitroam %s\n", GITROAM_VERSION);
            state_destroy(&state);
            return EXIT_SUCCESS;
        }
        if (strcmp(value, "--no-default-excludes") == 0) {
            state.discovery.use_default_excludes = false;
            continue;
        }
        if (strcmp(value, "--exclude") == 0) {
            if (++argument >= argc || argv[argument][0] == '\0') {
                fprintf(stderr, "gitroam: --exclude requires a name\n");
                print_usage(stderr, argv[0]);
                state_destroy(&state);
                return EXIT_FAILURE;
            }
            state.excluded_names[state.discovery.excluded_count++] =
                argv[argument];
            continue;
        }
        if (strncmp(value, "--exclude=", 10) == 0) {
            if (value[10] == '\0') {
                fprintf(stderr, "gitroam: --exclude requires a name\n");
                state_destroy(&state);
                return EXIT_FAILURE;
            }
            state.excluded_names[state.discovery.excluded_count++] =
                value + 10;
            continue;
        }
        if (strcmp(value, "--max-depth") == 0) {
            if (++argument >= argc ||
                parse_depth(argv[argument],
                            &state.discovery.max_depth) != 0) {
                fprintf(stderr,
                        "gitroam: --max-depth requires a non-negative number\n");
                state_destroy(&state);
                return EXIT_FAILURE;
            }
            continue;
        }
        if (strncmp(value, "--max-depth=", 12) == 0) {
            if (parse_depth(value + 12, &state.discovery.max_depth) != 0) {
                fprintf(stderr,
                        "gitroam: --max-depth requires a non-negative number\n");
                state_destroy(&state);
                return EXIT_FAILURE;
            }
            continue;
        }
        if (strcmp(value, "--") == 0) {
            if (++argument < argc) {
                root = argv[argument++];
            }
            if (argument != argc) {
                fprintf(stderr, "gitroam: only one ROOT may be provided\n");
                state_destroy(&state);
                return EXIT_FAILURE;
            }
            break;
        }
        if (value[0] == '-') {
            fprintf(stderr, "gitroam: unknown option: %s\n", value);
            print_usage(stderr, argv[0]);
            state_destroy(&state);
            return EXIT_FAILURE;
        }
        if (root != NULL) {
            fprintf(stderr, "gitroam: only one ROOT may be provided\n");
            state_destroy(&state);
            return EXIT_FAILURE;
        }
        root = value;
    }
    state.discovery.excluded_names = state.excluded_names;
    if (root == NULL) {
        root = getenv("GITROAM_ROOT");
    }
    if (root == NULL || root[0] == '\0') {
        if (getcwd(current, sizeof(current)) == NULL) {
            fprintf(stderr, "gitroam: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }
        root = current;
    }
    state.root = app_string_copy(root);
    if (state.root == NULL || load_repositories(&state, &error) != 0) {
        fprintf(stderr, "gitroam: %s\n",
                error == NULL ? "could not scan repositories" : error);
        free(error);
        state_destroy(&state);
        return EXIT_FAILURE;
    }
    status = tui_app_create(&app);
    if (status == TUI_OK) {
        status = build_repository_screen(&screen, &state);
    }
    if (status == TUI_OK) {
        status = tui_app_push_screen(app, screen);
        if (status == TUI_OK) {
            screen = NULL;
        }
    }
    if (status == TUI_OK) {
        status = tui_app_run(app);
    }
    tui_screen_destroy(screen);
    tui_app_destroy(app);
    if (status != TUI_OK) {
        fprintf(stderr, "gitroam: %s\n", tui_status_string(status));
    } else {
        exit_status = EXIT_SUCCESS;
    }
    state_destroy(&state);
    return exit_status;
}
