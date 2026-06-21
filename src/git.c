#include "git.h"

#include "git_internal.h"
#include "process.h"

#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char *git_internal_string_copy(const char *value)
{
    size_t length;
    char *copy;

    if (value == NULL) {
        return NULL;
    }
    length = strlen(value);
    copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, value, length + 1);
    }
    return copy;
}

static char *duplicate_range(const char *value, size_t length)
{
    char *copy = malloc(length + 1);

    if (copy != NULL) {
        memcpy(copy, value, length);
        copy[length] = '\0';
    }
    return copy;
}

static char *repository_display_path(const char *top, const char *common)
{
    size_t length = strlen(common);

    if (length > 5 && strcmp(common + length - 5, "/.git") == 0) {
        return duplicate_range(common, length - 5);
    }
    return git_internal_string_copy(top);
}

static void trim_output(char *value)
{
    size_t length;

    if (value == NULL) {
        return;
    }
    length = strlen(value);
    while (length > 0 &&
           (value[length - 1] == '\n' || value[length - 1] == '\r')) {
        value[--length] = '\0';
    }
}

void git_internal_set_error(char **error, const char *message)
{
    if (error == NULL) {
        return;
    }
    free(*error);
    *error = git_internal_string_copy(message == NULL || message[0] == '\0' ?
                              "Git command failed" : message);
    if (*error != NULL) {
        trim_output(*error);
    }
}

int git_internal_run(const git_repository *repository,
                     const char *const arguments[], process_result *result)
{
    const char *argv[32];
    size_t index = 0;
    size_t argument = 0;

    argv[index++] = "git";
    if (repository != NULL) {
        argv[index++] = "-C";
        argv[index++] = repository->path;
    }
    while (arguments[argument] != NULL && index + 1 < 32) {
        argv[index++] = arguments[argument++];
    }
    if (arguments[argument] != NULL) {
        errno = E2BIG;
        return -1;
    }
    argv[index] = NULL;
    return process_run(argv, result);
}

int git_internal_command_text(const git_repository *repository,
                              const char *const arguments[], char **output,
                              char **error)
{
    process_result result;

    if (git_internal_run(repository, arguments, &result) != 0) {
        git_internal_set_error(error, strerror(errno));
        return -1;
    }
    if (result.exit_code != 0) {
        git_internal_set_error(error, result.stderr_data);
        process_result_destroy(&result);
        return -1;
    }
    trim_output(result.stdout_data);
    *output = result.stdout_data;
    result.stdout_data = NULL;
    process_result_destroy(&result);
    return 0;
}

void git_repository_destroy(git_repository *repository)
{
    if (repository == NULL) {
        return;
    }
    free(repository->path);
    free(repository->common_dir);
    free(repository->name);
    memset(repository, 0, sizeof(*repository));
}

int git_repository_copy(git_repository *destination,
                        const git_repository *source)
{
    git_repository copy = { 0 };

    if (destination == NULL || source == NULL) {
        return -1;
    }
    copy.path = git_internal_string_copy(source->path);
    copy.common_dir = git_internal_string_copy(source->common_dir);
    copy.name = git_internal_string_copy(source->name);
    if ((source->path != NULL && copy.path == NULL) ||
        (source->common_dir != NULL && copy.common_dir == NULL) ||
        (source->name != NULL && copy.name == NULL)) {
        git_repository_destroy(&copy);
        return -1;
    }
    *destination = copy;
    return 0;
}

void git_repository_list_destroy(git_repository_list *list)
{
    size_t index;

    if (list == NULL) {
        return;
    }
    for (index = 0; index < list->count; index++) {
        git_repository_destroy(&list->items[index]);
    }
    free(list->items);
    free(list->index_keys);
    memset(list, 0, sizeof(*list));
}

int git_repository_open(const char *path, git_repository *repository,
                        char **error)
{
    /* One Git process returns all metadata needed by the repository list. */
    static const char *metadata_arguments[] = {
        "rev-parse",
        "--is-bare-repository",
        "--show-toplevel",
        "--path-format=absolute",
        "--git-common-dir",
        NULL
    };
    git_repository temporary = { 0 };
    git_repository probe = { 0 };
    char *metadata = NULL;
    char *bare;
    char *top;
    char *common;
    char *separator;
    char *name_copy;

    if (path == NULL || repository == NULL) {
        git_internal_set_error(error, "Invalid repository path");
        return -1;
    }
    probe.path = (char *)path;
    if (git_internal_command_text(&probe, metadata_arguments, &metadata, error) != 0) {
        return -1;
    }
    bare = metadata;
    separator = strchr(bare, '\n');
    if (separator == NULL) {
        free(metadata);
        git_internal_set_error(error, "Unexpected Git metadata output");
        return -1;
    }
    *separator = '\0';
    top = separator + 1;
    separator = strchr(top, '\n');
    if (separator == NULL) {
        free(metadata);
        git_internal_set_error(error, "Unexpected Git metadata output");
        return -1;
    }
    *separator = '\0';
    common = separator + 1;
    if (common[0] == '\0' || strchr(common, '\n') != NULL) {
        free(metadata);
        git_internal_set_error(error, "Unexpected Git metadata output");
        return -1;
    }
    if (strcmp(bare, "true") == 0) {
        free(metadata);
        git_internal_set_error(error, "Bare repositories are not supported");
        return -1;
    }
    temporary.path = repository_display_path(top, common);
    temporary.common_dir = git_internal_string_copy(common);
    free(metadata);
    if (temporary.path == NULL || temporary.common_dir == NULL) {
        git_repository_destroy(&temporary);
        git_internal_set_error(error, "Out of memory");
        return -1;
    }
    name_copy = git_internal_string_copy(temporary.path);
    if (name_copy != NULL) {
        temporary.name = git_internal_string_copy(basename(name_copy));
        free(name_copy);
    }
    if (temporary.name == NULL) {
        git_repository_destroy(&temporary);
        git_internal_set_error(error, "Out of memory");
        return -1;
    }
    *repository = temporary;
    return 0;
}

static size_t common_dir_hash(const char *value)
{
    const unsigned char *byte = (const unsigned char *)value;
    size_t hash = (size_t)1469598103934665603ULL;

    while (*byte != '\0') {
        hash ^= (size_t)*byte++;
        hash *= (size_t)1099511628211ULL;
    }
    return hash;
}

static int repository_index_resize(git_repository_list *list,
                                   size_t new_capacity)
{
    /* Open addressing keeps duplicate checks close to O(1) per repository. */
    char **keys;
    size_t index;

    keys = calloc(new_capacity, sizeof(*keys));
    if (keys == NULL) {
        return -1;
    }
    for (index = 0; index < list->count; index++) {
        size_t slot = common_dir_hash(list->items[index].common_dir) &
                      (new_capacity - 1);

        while (keys[slot] != NULL) {
            slot = (slot + 1) & (new_capacity - 1);
        }
        keys[slot] = list->items[index].common_dir;
    }
    free(list->index_keys);
    list->index_keys = keys;
    list->index_capacity = new_capacity;
    return 0;
}

int git_repository_list_add(git_repository_list *list,
                            git_repository *repository)
{
    git_repository *items;
    size_t capacity;
    size_t slot;

    if (list == NULL || repository == NULL ||
        repository->common_dir == NULL) {
        return -1;
    }
    if (list->index_capacity == 0 &&
        repository_index_resize(list, 16) != 0) {
        return -1;
    }
    if ((list->count + 1) * 10 >= list->index_capacity * 7 &&
        repository_index_resize(list, list->index_capacity * 2) != 0) {
        return -1;
    }
    slot = common_dir_hash(repository->common_dir) &
           (list->index_capacity - 1);
    while (list->index_keys[slot] != NULL) {
        if (strcmp(list->index_keys[slot], repository->common_dir) == 0) {
            git_repository_destroy(repository);
            return 0;
        }
        slot = (slot + 1) & (list->index_capacity - 1);
    }
    if (list->count == list->capacity) {
        capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        items = realloc(list->items, capacity * sizeof(*items));
        if (items == NULL) {
            return -1;
        }
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count] = *repository;
    list->index_keys[slot] = repository->common_dir;
    list->count++;
    memset(repository, 0, sizeof(*repository));
    return 0;
}

static void worktree_destroy(git_worktree *worktree)
{
    free(worktree->path);
    free(worktree->head);
    free(worktree->branch);
    memset(worktree, 0, sizeof(*worktree));
}

void git_worktree_list_destroy(git_worktree_list *list)
{
    size_t index;

    if (list == NULL) {
        return;
    }
    for (index = 0; index < list->count; index++) {
        worktree_destroy(&list->items[index]);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int append_worktree(git_worktree_list *list, git_worktree *worktree)
{
    git_worktree *items = realloc(list->items,
                                  (list->count + 1) * sizeof(*items));

    if (items == NULL) {
        return -1;
    }
    list->items = items;
    list->items[list->count++] = *worktree;
    memset(worktree, 0, sizeof(*worktree));
    return 0;
}

int git_worktrees_parse(const char *data, size_t length,
                        git_worktree_list *list)
{
    size_t offset = 0;
    git_worktree current = { 0 };

    if (data == NULL || list == NULL) {
        return -1;
    }
    memset(list, 0, sizeof(*list));
    while (offset < length) {
        size_t token_length = strnlen(data + offset, length - offset);
        const char *token = data + offset;

        if (token_length == length - offset) {
            goto failure;
        }
        offset += token_length + 1;
        if (token_length == 0) {
            if (current.path != NULL && append_worktree(list, &current) != 0) {
                goto failure;
            }
            continue;
        }
        if (token_length > 9 && memcmp(token, "worktree ", 9) == 0) {
            if (current.path != NULL && append_worktree(list, &current) != 0) {
                goto failure;
            }
            current.path = duplicate_range(token + 9, token_length - 9);
        } else if (token_length > 5 && memcmp(token, "HEAD ", 5) == 0) {
            free(current.head);
            current.head = duplicate_range(token + 5, token_length - 5);
        } else if (token_length > 7 && memcmp(token, "branch ", 7) == 0) {
            const char *branch = token + 7;
            size_t branch_length = token_length - 7;
            const char prefix[] = "refs/heads/";

            if (branch_length >= sizeof(prefix) - 1 &&
                memcmp(branch, prefix, sizeof(prefix) - 1) == 0) {
                branch += sizeof(prefix) - 1;
                branch_length -= sizeof(prefix) - 1;
            }
            free(current.branch);
            current.branch = duplicate_range(branch, branch_length);
        } else if (token_length == 8 &&
                   memcmp(token, "detached", 8) == 0) {
            current.detached = true;
        } else if (token_length >= 6 &&
                   memcmp(token, "locked", 6) == 0) {
            current.locked = true;
        } else if (token_length >= 8 &&
                   memcmp(token, "prunable", 8) == 0) {
            current.prunable = true;
        }
        if ((current.path == NULL && token_length > 9 &&
             memcmp(token, "worktree ", 9) == 0) ||
            (token_length > 5 && memcmp(token, "HEAD ", 5) == 0 &&
             current.head == NULL) ||
            (token_length > 7 && memcmp(token, "branch ", 7) == 0 &&
             current.branch == NULL)) {
            goto failure;
        }
    }
    if (current.path != NULL && append_worktree(list, &current) != 0) {
        goto failure;
    }
    return 0;

failure:
    worktree_destroy(&current);
    git_worktree_list_destroy(list);
    return -1;
}

int git_worktrees(const git_repository *repository, git_worktree_list *list,
                  char **error)
{
    static const char *arguments[] = {
        "worktree", "list", "--porcelain", "-z", NULL
    };
    process_result result;
    int parse_result;

    if (git_internal_run(repository, arguments, &result) != 0) {
        git_internal_set_error(error, strerror(errno));
        return -1;
    }
    if (result.exit_code != 0) {
        git_internal_set_error(error, result.stderr_data);
        process_result_destroy(&result);
        return -1;
    }
    parse_result = git_worktrees_parse(result.stdout_data,
                                       result.stdout_length, list);
    process_result_destroy(&result);
    if (parse_result != 0) {
        git_internal_set_error(error, "Could not parse git worktree output");
    }
    return parse_result;
}

static bool ref_exists(const git_repository *repository, const char *ref)
{
    const char *arguments[] = {
        "show-ref", "--verify", "--quiet", ref, NULL
    };
    process_result result;
    bool exists = false;

    if (git_internal_run(repository, arguments, &result) == 0) {
        exists = result.exit_code == 0;
        process_result_destroy(&result);
    }
    return exists;
}

static char *local_ref(const char *branch)
{
    size_t length = strlen(branch) + sizeof("refs/heads/");
    char *ref = malloc(length);

    if (ref != NULL) {
        (void)snprintf(ref, length, "refs/heads/%s", branch);
    }
    return ref;
}

static char *remote_ref(const char *remote_branch)
{
    size_t length = strlen(remote_branch) + sizeof("refs/remotes/");
    char *ref = malloc(length);

    if (ref != NULL) {
        (void)snprintf(ref, length, "refs/remotes/%s", remote_branch);
    }
    return ref;
}

int git_primary_branch(const git_repository *repository, char **branch,
                       char **error)
{
    static const char *origin_arguments[] = {
        "symbolic-ref", "--quiet", "--short", "refs/remotes/origin/HEAD", NULL
    };
    char *origin = NULL;
    char *ref;

    if (repository == NULL || branch == NULL) {
        git_internal_set_error(error, "Invalid repository");
        return -1;
    }
    if (git_internal_command_text(repository, origin_arguments, &origin, NULL) == 0) {
        char *separator = strchr(origin, '/');
        *branch = git_internal_string_copy(separator == NULL ? origin : separator + 1);
        free(origin);
        if (*branch == NULL) {
            git_internal_set_error(error, "Out of memory");
            return -1;
        }
        return 0;
    }
    ref = local_ref("main");
    if (ref != NULL && ref_exists(repository, ref)) {
        free(ref);
        *branch = git_internal_string_copy("main");
        return *branch == NULL ? -1 : 0;
    }
    free(ref);
    ref = remote_ref("origin/main");
    if (ref != NULL && ref_exists(repository, ref)) {
        free(ref);
        *branch = git_internal_string_copy("main");
        return *branch == NULL ? -1 : 0;
    }
    free(ref);
    ref = local_ref("master");
    if (ref != NULL && ref_exists(repository, ref)) {
        free(ref);
        *branch = git_internal_string_copy("master");
        return *branch == NULL ? -1 : 0;
    }
    free(ref);
    ref = remote_ref("origin/master");
    if (ref != NULL && ref_exists(repository, ref)) {
        free(ref);
        *branch = git_internal_string_copy("master");
        return *branch == NULL ? -1 : 0;
    }
    free(ref);
    git_internal_set_error(
        error, "No primary branch found (origin/HEAD, main, or master)");
    return -1;
}

char *git_default_worktree_path(const git_repository *repository,
                                const char *branch, char **error)
{
    git_worktree_list list = { 0 };
    char *parent_copy = NULL;
    char *parent;
    char *safe_branch = NULL;
    char *result = NULL;
    size_t index;
    size_t length;

    if (repository == NULL || branch == NULL ||
        git_worktrees(repository, &list, error) != 0 || list.count == 0) {
        return NULL;
    }
    parent_copy = git_internal_string_copy(list.items[0].path);
    safe_branch = git_internal_string_copy(branch);
    if (parent_copy == NULL || safe_branch == NULL) {
        git_internal_set_error(error, "Out of memory");
        goto done;
    }
    parent = dirname(parent_copy);
    for (index = 0; safe_branch[index] != '\0'; index++) {
        if (safe_branch[index] == '/') {
            safe_branch[index] = '-';
        }
    }
    length = strlen(parent) + strlen(repository->name) +
             strlen(safe_branch) + 3;
    result = malloc(length);
    if (result != NULL) {
        (void)snprintf(result, length, "%s/%s-%s", parent, repository->name,
                       safe_branch);
    } else {
        git_internal_set_error(error, "Out of memory");
    }

done:
    free(parent_copy);
    free(safe_branch);
    git_worktree_list_destroy(&list);
    return result;
}

static int matching_remote(const git_repository *repository,
                           const char *branch, char **match, size_t *count,
                           char **error)
{
    static const char *arguments[] = {
        "for-each-ref", "--format=%(refname:short)", "refs/remotes", NULL
    };
    char *output = NULL;
    char *line;
    char *save = NULL;
    size_t branch_length = strlen(branch);

    *count = 0;
    *match = NULL;
    if (git_internal_command_text(repository, arguments, &output, error) != 0) {
        return -1;
    }
    for (line = strtok_r(output, "\n", &save);
         line != NULL;
         line = strtok_r(NULL, "\n", &save)) {
        size_t line_length = strlen(line);
        if (line_length >= 5 &&
            strcmp(line + line_length - 5, "/HEAD") == 0) {
            continue;
        }
        if (line_length > branch_length + 1 &&
            line[line_length - branch_length - 1] == '/' &&
            strcmp(line + line_length - branch_length, branch) == 0) {
            char *copy = git_internal_string_copy(line);
            if (copy == NULL) {
                free(output);
                free(*match);
                git_internal_set_error(error, "Out of memory");
                return -1;
            }
            free(*match);
            *match = copy;
            (*count)++;
        }
    }
    free(output);
    return 0;
}

static int primary_start_ref(const git_repository *repository, char **start,
                             char **error)
{
    char *primary = NULL;
    char *ref;

    if (git_primary_branch(repository, &primary, error) != 0) {
        return -1;
    }
    ref = local_ref(primary);
    if (ref != NULL && ref_exists(repository, ref)) {
        *start = primary;
        free(ref);
        return 0;
    }
    free(ref);
    {
        size_t length = strlen(primary) + sizeof("origin/");
        *start = malloc(length);
        if (*start != NULL) {
            (void)snprintf(*start, length, "origin/%s", primary);
        }
    }
    free(primary);
    if (*start == NULL) {
        git_internal_set_error(error, "Out of memory");
        return -1;
    }
    return 0;
}

static char *resolve_destination(const git_repository *repository,
                                 const char *destination, char **error)
{
    git_worktree_list list = { 0 };
    char *path_copy;
    char *parent;
    char *resolved;
    size_t length;

    if (destination[0] == '/') {
        return git_internal_string_copy(destination);
    }
    if (git_worktrees(repository, &list, error) != 0 || list.count == 0) {
        git_worktree_list_destroy(&list);
        return NULL;
    }
    path_copy = git_internal_string_copy(list.items[0].path);
    if (path_copy == NULL) {
        git_worktree_list_destroy(&list);
        git_internal_set_error(error, "Out of memory");
        return NULL;
    }
    parent = dirname(path_copy);
    length = strlen(parent) + strlen(destination) + 2;
    resolved = malloc(length);
    if (resolved != NULL) {
        (void)snprintf(resolved, length, "%s/%s", parent, destination);
    } else {
        git_internal_set_error(error, "Out of memory");
    }
    free(path_copy);
    git_worktree_list_destroy(&list);
    return resolved;
}

git_create_status git_create_worktree(const git_repository *repository,
                                      const char *branch,
                                      const char *destination,
                                      char **resolved_path, char **error)
{
    const char *validate_arguments[] = {
        "check-ref-format", "--branch", branch, NULL
    };
    process_result result;
    git_worktree_list list = { 0 };
    char *ref = NULL;
    char *remote = NULL;
    char *start = NULL;
    char *destination_path = NULL;
    size_t remote_count = 0;
    size_t index;
    const char *arguments[9];
    size_t argument = 0;

    if (resolved_path != NULL) {
        *resolved_path = NULL;
    }
    if (repository == NULL || branch == NULL || destination == NULL) {
        git_internal_set_error(error, "Invalid worktree request");
        return GIT_CREATE_FAILED;
    }
    if (git_internal_run(repository, validate_arguments, &result) != 0) {
        git_internal_set_error(error, strerror(errno));
        return GIT_CREATE_FAILED;
    }
    if (result.exit_code != 0) {
        git_internal_set_error(error, result.stderr_data);
        process_result_destroy(&result);
        return GIT_CREATE_INVALID_BRANCH;
    }
    process_result_destroy(&result);
    if (git_worktrees(repository, &list, error) != 0) {
        return GIT_CREATE_FAILED;
    }
    for (index = 0; index < list.count; index++) {
        if (list.items[index].branch != NULL &&
            strcmp(list.items[index].branch, branch) == 0) {
            if (resolved_path != NULL) {
                *resolved_path = git_internal_string_copy(list.items[index].path);
            }
            git_worktree_list_destroy(&list);
            return GIT_CREATE_EXISTING;
        }
    }
    git_worktree_list_destroy(&list);
    destination_path = resolve_destination(repository, destination, error);
    if (destination_path == NULL) {
        return GIT_CREATE_FAILED;
    }
    ref = local_ref(branch);
    if (ref == NULL) {
        git_internal_set_error(error, "Out of memory");
        free(destination_path);
        return GIT_CREATE_FAILED;
    }
    arguments[argument++] = "worktree";
    arguments[argument++] = "add";
    if (ref_exists(repository, ref)) {
        arguments[argument++] = destination_path;
        arguments[argument++] = branch;
    } else {
        if (matching_remote(repository, branch, &remote, &remote_count,
                            error) != 0) {
            free(ref);
            free(destination_path);
            return GIT_CREATE_FAILED;
        }
        if (remote_count > 1) {
            free(ref);
            free(remote);
            free(destination_path);
            git_internal_set_error(error, "Branch exists on more than one remote");
            return GIT_CREATE_AMBIGUOUS_REMOTE;
        }
        arguments[argument++] = "--track";
        arguments[argument++] = "-b";
        arguments[argument++] = branch;
        arguments[argument++] = destination_path;
        if (remote_count == 1) {
            arguments[argument++] = remote;
        } else {
            argument -= 4;
            arguments[argument++] = "-b";
            arguments[argument++] = branch;
            arguments[argument++] = destination;
            if (primary_start_ref(repository, &start, error) != 0) {
                free(ref);
                free(remote);
                free(destination_path);
                return GIT_CREATE_FAILED;
            }
            arguments[argument++] = start;
        }
    }
    arguments[argument] = NULL;
    if (git_internal_run(repository, arguments, &result) != 0) {
        git_internal_set_error(error, strerror(errno));
        free(ref);
        free(remote);
        free(start);
        free(destination_path);
        return GIT_CREATE_FAILED;
    }
    if (result.exit_code != 0) {
        git_internal_set_error(error, result.stderr_data);
        process_result_destroy(&result);
        free(ref);
        free(remote);
        free(start);
        free(destination_path);
        return GIT_CREATE_FAILED;
    }
    process_result_destroy(&result);
    if (resolved_path != NULL) {
        char absolute[PATH_MAX];
        if (realpath(destination_path, absolute) != NULL) {
            *resolved_path = git_internal_string_copy(absolute);
        } else {
            *resolved_path = git_internal_string_copy(destination_path);
        }
    }
    free(ref);
    free(remote);
    free(start);
    free(destination_path);
    return GIT_CREATE_OK;
}
