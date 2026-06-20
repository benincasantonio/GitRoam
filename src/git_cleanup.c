#include "git.h"

#include "git_internal.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char *ref;
    char *upstream;
    time_t last_commit;
    bool merged;
    bool has_unpushed_commits;
} branch_record;

typedef struct {
    branch_record *branches;
    size_t branch_count;
    char *refs;
} repository_snapshot;

static int command_status(const git_repository *repository,
                          const char *const arguments[], int *exit_code,
                          char **error)
{
    process_result result;

    if (git_internal_run(repository, arguments, &result) != 0) {
        git_internal_set_error(error, strerror(errno));
        return -1;
    }
    *exit_code = result.exit_code;
    if (result.exit_code > 1) {
        git_internal_set_error(error, result.stderr_data);
        process_result_destroy(&result);
        return -1;
    }
    process_result_destroy(&result);
    return 0;
}

static int worktree_is_dirty(const char *path, bool *dirty, char **error)
{
    static const char *arguments[] = {
        "status", "--porcelain=v1", "-z", "--untracked-files=normal", NULL
    };
    git_repository worktree = { 0 };
    process_result result;

    worktree.path = (char *)path;
    if (git_internal_run(&worktree, arguments, &result) != 0) {
        git_internal_set_error(error, strerror(errno));
        return -1;
    }
    if (result.exit_code != 0) {
        git_internal_set_error(error, result.stderr_data);
        process_result_destroy(&result);
        return -1;
    }
    *dirty = result.stdout_length != 0;
    process_result_destroy(&result);
    return 0;
}

static int parse_timestamp(const char *value, time_t *timestamp)
{
    char *end = NULL;
    long long parsed;
    time_t converted;

    errno = 0;
    parsed = strtoll(value, &end, 10);
    converted = (time_t)parsed;
    if (errno != 0 || end == value || *end != '\0' ||
        (long long)converted != parsed) {
        return -1;
    }
    *timestamp = converted;
    return 0;
}

static int detached_commit_time(const git_repository *repository,
                                const char *head, time_t *timestamp,
                                char **error)
{
    const char *arguments[] = {
        "show", "-s", "--format=%ct", head, NULL
    };
    char *output = NULL;
    int status;

    *timestamp = (time_t)0;
    if (head == NULL) {
        return 0;
    }
    if (git_internal_command_text(repository, arguments, &output, error) !=
        0) {
        return -1;
    }
    status = parse_timestamp(output, timestamp);
    free(output);
    if (status != 0) {
        git_internal_set_error(error, "Unexpected Git commit timestamp");
    }
    return status;
}

static bool output_has_line(const char *output, const char *value)
{
    const char *line = output;
    size_t value_length = strlen(value);

    while (line != NULL && *line != '\0') {
        const char *end = strchr(line, '\n');
        size_t length = end == NULL ? strlen(line) : (size_t)(end - line);

        if (length == value_length &&
            memcmp(line, value, value_length) == 0) {
            return true;
        }
        line = end == NULL ? NULL : end + 1;
    }
    return false;
}

static bool snapshot_has_ref(const repository_snapshot *snapshot,
                             const char *ref)
{
    const char *line = snapshot->refs;
    size_t ref_length = strlen(ref);

    while (line != NULL && *line != '\0') {
        const char *end = strchr(line, '\n');
        const char *separator = strchr(line, ' ');
        size_t line_length = end == NULL ? strlen(line) :
                             (size_t)(end - line);

        if (separator != NULL && separator < line + line_length &&
            (size_t)(line + line_length - separator - 1) == ref_length &&
            memcmp(separator + 1, ref, ref_length) == 0) {
            return true;
        }
        line = end == NULL ? NULL : end + 1;
    }
    return false;
}

static void branch_record_destroy(branch_record *record)
{
    free(record->ref);
    free(record->upstream);
    memset(record, 0, sizeof(*record));
}

static void snapshot_destroy(repository_snapshot *snapshot)
{
    size_t index;

    for (index = 0; index < snapshot->branch_count; index++) {
        branch_record_destroy(&snapshot->branches[index]);
    }
    free(snapshot->branches);
    free(snapshot->refs);
    memset(snapshot, 0, sizeof(*snapshot));
}

static size_t line_count(const char *output)
{
    size_t count = 0;
    const char *cursor;

    if (output[0] == '\0') {
        return 0;
    }
    count = 1;
    for (cursor = output; *cursor != '\0'; cursor++) {
        if (*cursor == '\n') {
            count++;
        }
    }
    return count;
}

static int load_branches(const git_repository *repository,
                         repository_snapshot *snapshot, char **error)
{
    static const char *arguments[] = {
        "for-each-ref",
        "--format=%(refname)%09%(upstream)%09%(upstream:trackshort)%09"
        "%(committerdate:unix)",
        "refs/heads",
        NULL
    };
    char *output = NULL;
    char *line;
    char *next;
    size_t index = 0;

    if (git_internal_command_text(repository, arguments, &output, error) !=
        0) {
        return -1;
    }
    snapshot->branch_count = line_count(output);
    snapshot->branches = calloc(snapshot->branch_count,
                                sizeof(*snapshot->branches));
    if (snapshot->branch_count != 0 && snapshot->branches == NULL) {
        free(output);
        git_internal_set_error(error, "Out of memory");
        return -1;
    }
    for (line = output; line != NULL && *line != '\0'; line = next) {
        branch_record *record = &snapshot->branches[index++];
        char *first_tab;
        char *second_tab;
        char *third_tab;

        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        first_tab = strchr(line, '\t');
        second_tab = first_tab == NULL ? NULL : strchr(first_tab + 1, '\t');
        third_tab = second_tab == NULL ? NULL : strchr(second_tab + 1, '\t');
        if (first_tab == NULL || second_tab == NULL || third_tab == NULL) {
            free(output);
            git_internal_set_error(error,
                                   "Unexpected Git branch metadata");
            return -1;
        }
        *first_tab = '\0';
        *second_tab = '\0';
        *third_tab = '\0';
        record->ref = git_internal_string_copy(line);
        if (first_tab[1] != '\0') {
            record->upstream = git_internal_string_copy(first_tab + 1);
        }
        record->has_unpushed_commits =
            strchr(second_tab + 1, '>') != NULL;
        if (record->ref == NULL ||
            (first_tab[1] != '\0' && record->upstream == NULL)) {
            free(output);
            git_internal_set_error(error, "Out of memory");
            return -1;
        }
        if (parse_timestamp(third_tab + 1, &record->last_commit) != 0) {
            free(output);
            git_internal_set_error(error,
                                   "Unexpected Git branch metadata");
            return -1;
        }
    }
    free(output);
    return 0;
}

static branch_record *find_branch(repository_snapshot *snapshot,
                                  const char *branch)
{
    static const char prefix[] = "refs/heads/";
    size_t index;

    for (index = 0; index < snapshot->branch_count; index++) {
        const char *ref = snapshot->branches[index].ref;

        if (strncmp(ref, prefix, sizeof(prefix) - 1) == 0 &&
            strcmp(ref + sizeof(prefix) - 1, branch) == 0) {
            return &snapshot->branches[index];
        }
    }
    return NULL;
}

static int primary_ref(const git_repository *repository,
                       repository_snapshot *snapshot, char **ref,
                       char **error)
{
    char *branch = NULL;
    branch_record *record;
    size_t length;

    if (git_primary_branch(repository, &branch, error) != 0) {
        return -1;
    }
    record = find_branch(snapshot, branch);
    if (record != NULL) {
        *ref = git_internal_string_copy(record->ref);
    } else {
        length = strlen(branch) + sizeof("refs/remotes/origin/");
        *ref = malloc(length);
        if (*ref != NULL) {
            (void)snprintf(*ref, length, "refs/remotes/origin/%s", branch);
        }
    }
    free(branch);
    if (*ref == NULL) {
        git_internal_set_error(error, "Out of memory");
        return -1;
    }
    return 0;
}

static int mark_merged_branches(const git_repository *repository,
                                repository_snapshot *snapshot,
                                const char *primary, char **error)
{
    const char *arguments[5];
    char *merged_option;
    char *output = NULL;
    size_t length = strlen(primary) + sizeof("--merged=");
    size_t index;

    merged_option = malloc(length);
    if (merged_option == NULL) {
        git_internal_set_error(error, "Out of memory");
        return -1;
    }
    (void)snprintf(merged_option, length, "--merged=%s", primary);
    arguments[0] = "for-each-ref";
    arguments[1] = "--format=%(refname)";
    arguments[2] = merged_option;
    arguments[3] = "refs/heads";
    arguments[4] = NULL;
    if (git_internal_command_text(repository, arguments, &output, error) !=
        0) {
        free(merged_option);
        return -1;
    }
    free(merged_option);
    for (index = 0; index < snapshot->branch_count; index++) {
        snapshot->branches[index].merged =
            output_has_line(output, snapshot->branches[index].ref);
    }
    free(output);
    return 0;
}

static int load_refs(const git_repository *repository,
                     repository_snapshot *snapshot, char **error)
{
    static const char *arguments[] = {
        "show-ref", NULL
    };

    return git_internal_command_text(repository, arguments, &snapshot->refs,
                                     error);
}

static int snapshot_load(const git_repository *repository,
                         repository_snapshot *snapshot, char **primary,
                         char **error)
{
    if (load_branches(repository, snapshot, error) != 0 ||
        primary_ref(repository, snapshot, primary, error) != 0 ||
        mark_merged_branches(repository, snapshot, *primary, error) != 0 ||
        load_refs(repository, snapshot, error) != 0) {
        free(*primary);
        *primary = NULL;
        snapshot_destroy(snapshot);
        return -1;
    }
    return 0;
}

static int detached_is_merged(const git_repository *repository,
                              const char *head, const char *primary,
                              bool *merged, char **error)
{
    const char *arguments[] = {
        "merge-base", "--is-ancestor", head, primary, NULL
    };
    int status;

    if (head == NULL ||
        command_status(repository, arguments, &status, error) != 0) {
        return -1;
    }
    *merged = status == 0;
    return 0;
}

static void cleanup_item_destroy(git_worktree_cleanup *item)
{
    free(item->path);
    free(item->branch);
    free(item->upstream);
    memset(item, 0, sizeof(*item));
}

void git_worktree_cleanup_list_destroy(git_worktree_cleanup_list *list)
{
    size_t index;

    if (list == NULL) {
        return;
    }
    for (index = 0; index < list->count; index++) {
        cleanup_item_destroy(&list->items[index]);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int cleanup_item_copy(git_worktree_cleanup *item,
                             const git_worktree *worktree)
{
    item->path = git_internal_string_copy(worktree->path);
    item->branch = git_internal_string_copy(worktree->branch);
    if (item->path == NULL ||
        (worktree->branch != NULL && item->branch == NULL)) {
        cleanup_item_destroy(item);
        return -1;
    }
    return 0;
}

static int set_commit_time(const git_repository *repository,
                           repository_snapshot *snapshot,
                           const git_worktree *worktree,
                           git_worktree_cleanup *item, char **error)
{
    branch_record *record;

    if (worktree->branch != NULL) {
        record = find_branch(snapshot, worktree->branch);
        if (record == NULL) {
            git_internal_set_error(error,
                                   "Worktree branch metadata is missing");
            return -1;
        }
        item->last_commit = record->last_commit;
        return 0;
    }
    return detached_commit_time(repository, worktree->head,
                                &item->last_commit, error);
}

static void inspect_worktree(const git_repository *repository,
                             repository_snapshot *snapshot,
                             const git_worktree *worktree,
                             const char *primary,
                             git_worktree_cleanup *item)
{
    branch_record *branch = worktree->branch == NULL ?
                            NULL : find_branch(snapshot, worktree->branch);
    bool dirty = false;
    bool merged = false;
    char *ignored_error = NULL;

    if (worktree_is_dirty(worktree->path, &dirty, &ignored_error) != 0 ||
        set_commit_time(repository, snapshot, worktree, item,
                        &ignored_error) != 0) {
        goto inspection_failed;
    }
    if (dirty) {
        item->state = GIT_WORKTREE_CLEANUP_DIRTY;
        return;
    }
    if (branch != NULL) {
        merged = branch->merged;
    } else if (detached_is_merged(repository, worktree->head, primary,
                                  &merged, &ignored_error) != 0) {
        goto inspection_failed;
    }
    if (merged) {
        item->state = GIT_WORKTREE_CLEANUP_MERGED;
        return;
    }
    if (worktree->detached || worktree->branch == NULL) {
        item->state = GIT_WORKTREE_CLEANUP_DETACHED_UNMERGED;
        return;
    }
    if (branch == NULL) {
        goto inspection_failed;
    }
    if (branch->upstream == NULL) {
        item->state = GIT_WORKTREE_CLEANUP_UNPUSHED;
        return;
    }
    item->upstream = git_internal_string_copy(branch->upstream);
    if (item->upstream == NULL) {
        goto inspection_failed;
    }
    if (!snapshot_has_ref(snapshot, branch->upstream)) {
        item->state = GIT_WORKTREE_CLEANUP_UPSTREAM_GONE;
        return;
    }
    if (branch->has_unpushed_commits) {
        item->state = GIT_WORKTREE_CLEANUP_UNPUSHED;
        return;
    }
    item->state = GIT_WORKTREE_CLEANUP_CLEAN_UNMERGED;
    return;

inspection_failed:
    free(ignored_error);
    free(item->upstream);
    item->upstream = NULL;
    item->state = GIT_WORKTREE_CLEANUP_INSPECTION_FAILED;
}

int git_worktree_cleanup_scan(const git_repository *repository,
                              git_worktree_cleanup_list *list, char **error)
{
    git_worktree_list worktrees = { 0 };
    git_worktree_cleanup_list result = { 0 };
    repository_snapshot snapshot = { 0 };
    char *primary = NULL;
    size_t index;

    if (repository == NULL || list == NULL) {
        git_internal_set_error(error, "Invalid cleanup request");
        return -1;
    }
    memset(list, 0, sizeof(*list));
    if (git_worktrees(repository, &worktrees, error) != 0 ||
        snapshot_load(repository, &snapshot, &primary, error) != 0) {
        git_worktree_list_destroy(&worktrees);
        return -1;
    }
    result.items = calloc(worktrees.count, sizeof(*result.items));
    if (worktrees.count != 0 && result.items == NULL) {
        git_internal_set_error(error, "Out of memory");
        goto failure;
    }
    result.count = worktrees.count;
    for (index = 0; index < worktrees.count; index++) {
        git_worktree *worktree = &worktrees.items[index];
        git_worktree_cleanup *item = &result.items[index];

        if (cleanup_item_copy(item, worktree) != 0) {
            git_internal_set_error(error, "Out of memory");
            goto failure;
        }
        if (index == 0) {
            (void)set_commit_time(repository, &snapshot, worktree, item, NULL);
            item->state = GIT_WORKTREE_CLEANUP_PRIMARY;
        } else if (worktree->locked) {
            (void)set_commit_time(repository, &snapshot, worktree, item, NULL);
            item->state = GIT_WORKTREE_CLEANUP_LOCKED;
        } else if (worktree->prunable) {
            (void)set_commit_time(repository, &snapshot, worktree, item, NULL);
            item->state = GIT_WORKTREE_CLEANUP_STALE_METADATA;
        } else {
            inspect_worktree(repository, &snapshot, worktree, primary, item);
        }
    }
    free(primary);
    snapshot_destroy(&snapshot);
    git_worktree_list_destroy(&worktrees);
    *list = result;
    return 0;

failure:
    free(primary);
    snapshot_destroy(&snapshot);
    git_worktree_list_destroy(&worktrees);
    git_worktree_cleanup_list_destroy(&result);
    return -1;
}

bool git_worktree_cleanup_is_removable(git_worktree_cleanup_state state)
{
    return state == GIT_WORKTREE_CLEANUP_MERGED ||
           state == GIT_WORKTREE_CLEANUP_UPSTREAM_GONE ||
           state == GIT_WORKTREE_CLEANUP_CLEAN_UNMERGED;
}

static const char *blocked_removal_message(
    git_worktree_cleanup_state state)
{
    switch (state) {
    case GIT_WORKTREE_CLEANUP_PRIMARY:
        return "The primary worktree cannot be removed";
    case GIT_WORKTREE_CLEANUP_LOCKED:
        return "The worktree is locked";
    case GIT_WORKTREE_CLEANUP_DIRTY:
        return "The worktree has modified or untracked files";
    case GIT_WORKTREE_CLEANUP_STALE_METADATA:
        return "Use worktree pruning for stale metadata";
    case GIT_WORKTREE_CLEANUP_UNPUSHED:
        return "The branch has unpushed commits";
    case GIT_WORKTREE_CLEANUP_DETACHED_UNMERGED:
        return "The detached worktree contains an unmerged commit";
    case GIT_WORKTREE_CLEANUP_INSPECTION_FAILED:
        return "The worktree could not be inspected safely";
    case GIT_WORKTREE_CLEANUP_MERGED:
    case GIT_WORKTREE_CLEANUP_UPSTREAM_GONE:
    case GIT_WORKTREE_CLEANUP_CLEAN_UNMERGED:
        break;
    }
    return "The worktree cannot be removed safely";
}

int git_worktree_remove_safe(const git_repository *repository,
                             const char *path, char **error)
{
    git_worktree_cleanup_list list = { 0 };
    process_result result;
    size_t index;
    int status = -1;

    if (repository == NULL || path == NULL || path[0] == '\0') {
        git_internal_set_error(error, "Invalid worktree removal request");
        return -1;
    }
    if (git_worktree_cleanup_scan(repository, &list, error) != 0) {
        return -1;
    }
    for (index = 0; index < list.count; index++) {
        git_worktree_cleanup *item = &list.items[index];

        if (strcmp(item->path, path) != 0) {
            continue;
        }
        if (!git_worktree_cleanup_is_removable(item->state)) {
            git_internal_set_error(error,
                                   blocked_removal_message(item->state));
            goto done;
        }
        {
            const char *arguments[] = {
                "worktree", "remove", item->path, NULL
            };

            if (git_internal_run(repository, arguments, &result) != 0) {
                git_internal_set_error(error, strerror(errno));
                goto done;
            }
        }
        if (result.exit_code != 0) {
            git_internal_set_error(error, result.stderr_data);
            process_result_destroy(&result);
            goto done;
        }
        process_result_destroy(&result);
        status = 0;
        goto done;
    }
    git_internal_set_error(error, "The worktree no longer exists");

done:
    git_worktree_cleanup_list_destroy(&list);
    return status;
}

int git_worktree_prune(const git_repository *repository, char **error)
{
    static const char *arguments[] = {
        "worktree", "prune", "--expire", "now", NULL
    };
    process_result result;

    if (repository == NULL) {
        git_internal_set_error(error, "Invalid worktree prune request");
        return -1;
    }
    if (git_internal_run(repository, arguments, &result) != 0) {
        git_internal_set_error(error, strerror(errno));
        return -1;
    }
    if (result.exit_code != 0) {
        git_internal_set_error(error, result.stderr_data);
        process_result_destroy(&result);
        return -1;
    }
    process_result_destroy(&result);
    return 0;
}
