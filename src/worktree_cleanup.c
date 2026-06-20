#include "worktree_cleanup.h"

#include "app_ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    /* Owned deep copy so the screen never depends on the lifetime of the
     * repository-list entry it was opened from (that array can be
     * reallocated by a refresh on the base screen). */
    git_repository repository;
    git_worktree_cleanup_list worktrees;
    bool has_stale_metadata;
} cleanup_context;

typedef enum {
    CLEANUP_REMOVE_WORKTREE = 0,
    CLEANUP_PRUNE_METADATA
} cleanup_action;

typedef struct {
    const git_repository *repository;
    cleanup_action action;
    char *path;
    bool branch_retained;
} confirmation_context;

static tui_status create_cleanup_screen(tui_screen **out_screen,
                                        const git_repository *repository);

static void cleanup_context_destroy(void *opaque)
{
    cleanup_context *context = opaque;

    if (context != NULL) {
        git_worktree_cleanup_list_destroy(&context->worktrees);
        git_repository_destroy(&context->repository);
        free(context);
    }
}

static void confirmation_context_destroy(void *opaque)
{
    confirmation_context *context = opaque;

    if (context != NULL) {
        free(context->path);
        free(context);
    }
}

static const char *state_label(git_worktree_cleanup_state state)
{
    switch (state) {
    case GIT_WORKTREE_CLEANUP_PRIMARY:
        return "protected: primary";
    case GIT_WORKTREE_CLEANUP_LOCKED:
        return "protected: locked";
    case GIT_WORKTREE_CLEANUP_DIRTY:
        return "protected: dirty";
    case GIT_WORKTREE_CLEANUP_STALE_METADATA:
        return "stale metadata";
    case GIT_WORKTREE_CLEANUP_MERGED:
        return "clean: merged";
    case GIT_WORKTREE_CLEANUP_UPSTREAM_GONE:
        return "clean: upstream gone";
    case GIT_WORKTREE_CLEANUP_CLEAN_UNMERGED:
        return "clean: unmerged";
    case GIT_WORKTREE_CLEANUP_UNPUSHED:
        return "protected: unpushed commits";
    case GIT_WORKTREE_CLEANUP_DETACHED_UNMERGED:
        return "protected: detached, unmerged";
    case GIT_WORKTREE_CLEANUP_INSPECTION_FAILED:
        return "protected: inspection failed";
    }
    return "protected";
}

static const char *protected_message(git_worktree_cleanup_state state)
{
    switch (state) {
    case GIT_WORKTREE_CLEANUP_PRIMARY:
        return "The primary worktree is always protected.";
    case GIT_WORKTREE_CLEANUP_LOCKED:
        return "Unlock this worktree before considering removal.";
    case GIT_WORKTREE_CLEANUP_DIRTY:
        return "This worktree has modified or untracked files.";
    case GIT_WORKTREE_CLEANUP_STALE_METADATA:
        return "The directory is missing. Prune its stale Git metadata.";
    case GIT_WORKTREE_CLEANUP_DETACHED_UNMERGED:
        return "This detached worktree contains an unmerged commit.";
    case GIT_WORKTREE_CLEANUP_INSPECTION_FAILED:
        return "GitRoam could not inspect this worktree safely.";
    case GIT_WORKTREE_CLEANUP_UNPUSHED:
        return "This branch has commits that have not been pushed upstream.";
    case GIT_WORKTREE_CLEANUP_MERGED:
    case GIT_WORKTREE_CLEANUP_UPSTREAM_GONE:
    case GIT_WORKTREE_CLEANUP_CLEAN_UNMERGED:
        break;
    }
    return "This worktree is protected.";
}

static void format_commit_age(time_t commit_time, char *buffer, size_t size)
{
    time_t now = time(NULL);
    long long days;

    if (commit_time <= (time_t)0 || now == (time_t)-1) {
        (void)snprintf(buffer, size, "last commit unknown");
        return;
    }
    days = commit_time >= now ? 0 :
           (long long)((now - commit_time) / (time_t)(24 * 60 * 60));
    if (days == 0) {
        (void)snprintf(buffer, size, "last commit today");
    } else {
        (void)snprintf(buffer, size, "last commit %lldd ago", days);
    }
}

static char *worktree_label(const git_worktree_cleanup *worktree)
{
    const char *branch = worktree->branch == NULL ?
                         "(detached)" : worktree->branch;
    const char *label = state_label(worktree->state);
    char age[64];
    char *result;
    size_t length;

    format_commit_age(worktree->last_commit, age, sizeof(age));
    length = strlen(branch) + strlen(label) + strlen(age) +
             strlen(worktree->path) + 12;
    result = malloc(length);
    if (result != NULL) {
        (void)snprintf(result, length, "%s  [%s; %s]  %s",
                       branch, label, age, worktree->path);
    }
    return result;
}

static void free_labels(char **labels, size_t count)
{
    size_t index;

    for (index = 0; index < count; index++) {
        free(labels == NULL ? NULL : labels[index]);
    }
    free(labels);
}

static void show_refresh_result(tui_app *app,
                                const git_repository *repository,
                                const char *title, const char *message)
{
    tui_screen *replacement = NULL;

    if (create_cleanup_screen(&replacement, repository) != TUI_OK) {
        (void)tui_app_pop_screen(app);
        app_show_message(app, title,
                         "The action succeeded, but the cleanup list "
                         "could not be refreshed.");
        return;
    }
    (void)tui_app_pop_screen(app);
    if (tui_app_replace_screen(app, replacement) != TUI_OK) {
        tui_screen_destroy(replacement);
        app_show_message(app, title,
                         "The action succeeded, but the cleanup list "
                         "could not be refreshed.");
        return;
    }
    app_show_message(app, title, message);
}

static void confirmation_selected(tui_app *app, tui_widget *widget,
                                  size_t selected, void *opaque)
{
    confirmation_context *context = opaque;
    char *error = NULL;

    (void)widget;
    if (selected != 0) {
        (void)tui_app_pop_screen(app);
        return;
    }
    if (context->action == CLEANUP_PRUNE_METADATA) {
        if (git_worktree_prune(context->repository, &error) != 0) {
            app_show_message(app, "Metadata not pruned",
                             error == NULL ?
                                 "Git worktree prune failed." : error);
            free(error);
            return;
        }
        show_refresh_result(app, context->repository, "Metadata pruned",
                            "Stale worktree metadata was removed.");
        return;
    }
    if (git_worktree_remove_safe(context->repository, context->path,
                                 &error) != 0) {
        app_show_message(app, "Workspace not removed",
                         error == NULL ?
                             "The workspace is no longer removable." : error);
        free(error);
        return;
    }
    show_refresh_result(
        app, context->repository, "Workspace removed",
        context->branch_retained ?
            "The workspace was removed; its branch was retained." :
            "The detached workspace was removed.");
}

static void push_confirmation(tui_app *app, confirmation_context *context,
                              const char *title, const char *message,
                              const char *action)
{
    const char *actions[] = { action, "Cancel" };
    tui_screen *screen = NULL;
    tui_widget *dialog = NULL;

    if (tui_screen_create(&screen, title, context,
                          confirmation_context_destroy) != TUI_OK) {
        confirmation_context_destroy(context);
        app_show_message(app, "Out of memory",
                         "Could not prepare the cleanup action.");
        return;
    }
    context = NULL;
    tui_screen_set_event_handler(screen, app_quit_shortcut, NULL);
    if (tui_action_dialog_create(&dialog, message, actions, 2,
                                 confirmation_selected,
                                 tui_screen_context(screen)) != TUI_OK ||
        app_screen_take_widget(screen, &dialog) != TUI_OK ||
        app_screen_add_status(
            screen, "Enter: select  Esc: cancel  q: quit") != TUI_OK) {
        tui_widget_destroy(dialog);
        tui_screen_destroy(screen);
        app_show_message(app, "Out of memory",
                         "Could not prepare the cleanup action.");
        return;
    }
    if (tui_app_push_screen(app, screen) != TUI_OK) {
        tui_screen_destroy(screen);
        app_show_message(app, "Out of memory",
                         "Could not open the cleanup confirmation.");
    }
}

static void confirm_prune(tui_app *app, const git_repository *repository)
{
    confirmation_context *context = calloc(1, sizeof(*context));

    if (context == NULL) {
        app_show_message(app, "Out of memory",
                         "Could not prepare metadata pruning.");
        return;
    }
    context->repository = repository;
    context->action = CLEANUP_PRUNE_METADATA;
    push_confirmation(app, context, "Confirm metadata pruning",
                      "Remove metadata for missing worktree directories?",
                      "Prune stale metadata");
}

static void confirm_removal(tui_app *app, const git_repository *repository,
                            const git_worktree_cleanup *worktree)
{
    confirmation_context *context = calloc(1, sizeof(*context));
    const char *branch = worktree->branch == NULL ?
                         "detached HEAD" : worktree->branch;
    char message[512];

    if (context != NULL) {
        context->repository = repository;
        context->action = CLEANUP_REMOVE_WORKTREE;
        context->path = app_string_copy(worktree->path);
        context->branch_retained = worktree->branch != NULL;
    }
    if (context == NULL || context->path == NULL) {
        confirmation_context_destroy(context);
        app_show_message(app, "Out of memory",
                         "Could not prepare workspace removal.");
        return;
    }
    switch (worktree->state) {
    case GIT_WORKTREE_CLEANUP_CLEAN_UNMERGED:
        (void)snprintf(message, sizeof(message),
                       "Remove unmerged workspace %s? Branch retained.",
                       branch);
        break;
    case GIT_WORKTREE_CLEANUP_UPSTREAM_GONE:
        (void)snprintf(message, sizeof(message),
                       "Remove %s? Upstream is gone; branch retained.",
                       branch);
        break;
    default:
        if (worktree->branch == NULL) {
            (void)snprintf(message, sizeof(message),
                           "Remove merged detached workspace?");
        } else {
            (void)snprintf(message, sizeof(message),
                           "Remove merged workspace %s? Branch retained.",
                           branch);
        }
        break;
    }
    {
        size_t used = strlen(message);

        (void)snprintf(message + used, sizeof(message) - used,
                       " Ignored files in the folder are deleted.");
    }
    push_confirmation(app, context, "Confirm workspace removal", message,
                      "Remove workspace");
}

static void cleanup_selected(tui_app *app, tui_widget *widget,
                             size_t selected, void *opaque)
{
    cleanup_context *context = opaque;

    (void)widget;
    if (selected < context->worktrees.count) {
        git_worktree_cleanup *worktree = &context->worktrees.items[selected];

        if (git_worktree_cleanup_is_removable(worktree->state)) {
            confirm_removal(app, &context->repository, worktree);
        } else if (worktree->state ==
                   GIT_WORKTREE_CLEANUP_STALE_METADATA) {
            confirm_prune(app, &context->repository);
        } else {
            app_show_message(app, "Workspace protected",
                             protected_message(worktree->state));
        }
        return;
    }
    if (context->has_stale_metadata &&
        selected == context->worktrees.count) {
        confirm_prune(app, &context->repository);
        return;
    }
    (void)tui_app_pop_screen(app);
}

static tui_status create_cleanup_screen(tui_screen **out_screen,
                                        const git_repository *repository)
{
    cleanup_context *context = NULL;
    tui_screen *screen = NULL;
    tui_widget *menu = NULL;
    char **labels = NULL;
    const char **label_view = NULL;
    char *error = NULL;
    size_t item_count = 0;
    size_t index;
    size_t next;
    tui_status result = TUI_ERR_MEMORY;
    char title[256];

    context = calloc(1, sizeof(*context));
    if (context == NULL) {
        return TUI_ERR_MEMORY;
    }
    if (git_repository_copy(&context->repository, repository) != 0) {
        cleanup_context_destroy(context);
        return TUI_ERR_MEMORY;
    }
    if (git_worktree_cleanup_scan(&context->repository, &context->worktrees,
                                  &error) != 0) {
        free(error);
        cleanup_context_destroy(context);
        return TUI_ERR_STATE;
    }
    for (index = 0; index < context->worktrees.count; index++) {
        if (context->worktrees.items[index].state ==
            GIT_WORKTREE_CLEANUP_STALE_METADATA) {
            context->has_stale_metadata = true;
            break;
        }
    }
    item_count = context->worktrees.count +
                 (context->has_stale_metadata ? 1 : 0) + 1;
    labels = calloc(item_count, sizeof(*labels));
    label_view = calloc(item_count, sizeof(*label_view));
    if (labels == NULL || label_view == NULL) {
        goto done;
    }
    for (index = 0; index < context->worktrees.count; index++) {
        labels[index] = worktree_label(&context->worktrees.items[index]);
        label_view[index] = labels[index];
        if (labels[index] == NULL) {
            goto done;
        }
    }
    next = context->worktrees.count;
    if (context->has_stale_metadata) {
        labels[next] = app_string_copy("Prune stale metadata");
        label_view[next] = labels[next];
        if (labels[next++] == NULL) {
            goto done;
        }
    }
    labels[next] = app_string_copy("Back");
    label_view[next] = labels[next];
    if (labels[next] == NULL) {
        goto done;
    }
    (void)snprintf(title, sizeof(title), "%s cleanup", repository->name);
    if (tui_screen_create(&screen, title, context,
                          cleanup_context_destroy) != TUI_OK) {
        goto done;
    }
    context = NULL;
    tui_screen_set_event_handler(screen, app_quit_shortcut, NULL);
    if (tui_menu_create(&menu, label_view, item_count, cleanup_selected,
                        tui_screen_context(screen)) != TUI_OK ||
        app_screen_take_widget(screen, &menu) != TUI_OK ||
        app_screen_add_status(
            screen, "Enter: inspect/remove  Esc: back  q: quit") != TUI_OK) {
        goto done;
    }
    *out_screen = screen;
    screen = NULL;
    result = TUI_OK;

done:
    free_labels(labels, item_count);
    free(label_view);
    cleanup_context_destroy(context);
    tui_widget_destroy(menu);
    tui_screen_destroy(screen);
    return result;
}

void worktree_cleanup_open(tui_app *app,
                           const git_repository *repository)
{
    tui_screen *screen = NULL;

    if (create_cleanup_screen(&screen, repository) != TUI_OK) {
        app_show_message(app, "Could not inspect workspaces",
                         "Git worktree cleanup analysis failed.");
        return;
    }
    if (tui_app_push_screen(app, screen) != TUI_OK) {
        tui_screen_destroy(screen);
        app_show_message(app, "Out of memory",
                         "Could not open the cleanup list.");
    }
}
