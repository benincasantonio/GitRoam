#include "discovery.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* These directories are normally generated or managed by package tools. */
static const char *const default_excluded_names[] = {
    "node_modules",
    "bower_components",
    ".npm",
    ".pnpm-store",
    ".yarn",
    ".bun",
    "vendor",
    "deps",
    "third_party",
    "external",
    ".venv",
    "venv",
    "__pycache__",
    "site-packages",
    ".pytest_cache",
    ".mypy_cache",
    ".ruff_cache",
    ".tox",
    ".nox",
    ".direnv",
    "target",
    ".cargo",
    ".rustup",
    ".gradle",
    ".m2",
    ".ivy2",
    ".nuget",
    ".bundle",
    ".gem",
    "build",
    "dist",
    "out",
    "coverage",
    ".cache",
    ".parcel-cache",
    ".vite",
    ".next",
    ".nuxt",
    ".svelte-kit",
    ".turbo",
    ".angular",
    ".expo",
    ".serverless",
    ".build",
    "Pods",
    "Carthage",
    "DerivedData",
    ".terraform",
    ".dart_tool",
    ".pub-cache",
    "Library",
    ".Trash"
};

static char *copy_string(const char *value)
{
    size_t length = strlen(value);
    char *copy = malloc(length + 1);

    if (copy != NULL) {
        memcpy(copy, value, length + 1);
    }
    return copy;
}

static void set_error(char **error, const char *message)
{
    if (error != NULL) {
        free(*error);
        *error = copy_string(message);
    }
}

static bool has_git_marker(const char *path)
{
    char marker[PATH_MAX];
    struct stat information;

    if (snprintf(marker, sizeof(marker), "%s/.git", path) >=
        (int)sizeof(marker)) {
        return false;
    }
    return lstat(marker, &information) == 0 &&
           (S_ISDIR(information.st_mode) || S_ISREG(information.st_mode));
}

static bool looks_like_bare_repository(const char *path)
{
    const char *name = strrchr(path, '/');
    size_t name_length;
    char marker[PATH_MAX];
    struct stat information;

    name = name == NULL ? path : name + 1;
    name_length = strlen(name);
    if (name_length < 4 || strcmp(name + name_length - 4, ".git") != 0) {
        return false;
    }
    if (snprintf(marker, sizeof(marker), "%s/HEAD", path) >=
            (int)sizeof(marker) ||
        lstat(marker, &information) != 0 ||
        !S_ISREG(information.st_mode)) {
        return false;
    }
    if (snprintf(marker, sizeof(marker), "%s/objects", path) >=
            (int)sizeof(marker) ||
        lstat(marker, &information) != 0 ||
        !S_ISDIR(information.st_mode)) {
        return false;
    }
    if (snprintf(marker, sizeof(marker), "%s/refs", path) >=
            (int)sizeof(marker) ||
        lstat(marker, &information) != 0 ||
        !S_ISDIR(information.st_mode)) {
        return false;
    }
    return true;
}

void discovery_options_init(discovery_options *options)
{
    if (options == NULL) {
        return;
    }
    options->max_depth = 128;
    options->use_default_excludes = true;
    options->excluded_names = NULL;
    options->excluded_count = 0;
}

static bool name_is_excluded(const char *name,
                             const discovery_options *options)
{
    size_t index;

    if (options->use_default_excludes) {
        for (index = 0;
             index < sizeof(default_excluded_names) /
                     sizeof(default_excluded_names[0]);
             index++) {
            if (strcmp(name, default_excluded_names[index]) == 0) {
                return true;
            }
        }
        if (strncmp(name, "cmake-build-", 12) == 0) {
            return true;
        }
    }
    for (index = 0; index < options->excluded_count; index++) {
        if (strcmp(name, options->excluded_names[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool entry_is_directory(const char *path, const struct dirent *entry)
{
#if defined(DT_DIR) && defined(DT_UNKNOWN)
    /*
     * Most local filesystems provide the entry type for free. Only call
     * lstat when the filesystem reports that the type is unknown.
     */
    if (entry->d_type == DT_DIR) {
        return true;
    }
    if (entry->d_type != DT_UNKNOWN) {
        return false;
    }
#endif
    {
        char child[PATH_MAX];
        struct stat information;

        if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >=
            (int)sizeof(child)) {
            return false;
        }
        return lstat(child, &information) == 0 &&
               S_ISDIR(information.st_mode) &&
               !S_ISLNK(information.st_mode);
    }
}

typedef struct {
    char *path;
    unsigned int depth;
} scan_entry;

typedef struct {
    scan_entry *items;
    size_t count;
    size_t capacity;
} scan_stack;

static void scan_stack_destroy(scan_stack *stack)
{
    size_t index;

    for (index = 0; index < stack->count; index++) {
        free(stack->items[index].path);
    }
    free(stack->items);
    memset(stack, 0, sizeof(*stack));
}

static int scan_stack_push(scan_stack *stack, const char *path,
                           unsigned int depth)
{
    scan_entry *items;
    char *path_copy;
    size_t capacity;

    if (stack->count == stack->capacity) {
        if (stack->capacity > SIZE_MAX / 2) {
            return -1;
        }
        capacity = stack->capacity == 0 ? 16 : stack->capacity * 2;
        if (capacity > SIZE_MAX / sizeof(*items)) {
            return -1;
        }
        items = realloc(stack->items, capacity * sizeof(*items));
        if (items == NULL) {
            return -1;
        }
        stack->items = items;
        stack->capacity = capacity;
    }
    path_copy = copy_string(path);
    if (path_copy == NULL) {
        return -1;
    }
    stack->items[stack->count].path = path_copy;
    stack->items[stack->count].depth = depth;
    stack->count++;
    return 0;
}

static int scan_directories(const char *path, git_repository_list *list,
                            char **error,
                            const discovery_options *options)
{
    scan_stack stack = { 0 };

    if (scan_stack_push(&stack, path, 0) != 0) {
        set_error(error, "Out of memory");
        return -1;
    }
    while (stack.count > 0) {
        scan_entry current = stack.items[--stack.count];
        DIR *directory;
        struct dirent *entry;

        /* Stop descending as soon as a repository root is reached. */
        if (has_git_marker(current.path)) {
            git_repository repository = { 0 };
            char *open_error = NULL;

            if (git_repository_open(current.path, &repository,
                                    &open_error) == 0 &&
                git_repository_list_add(list, &repository) != 0) {
                git_repository_destroy(&repository);
                free(open_error);
                free(current.path);
                scan_stack_destroy(&stack);
                set_error(error, "Out of memory");
                return -1;
            }
            free(open_error);
            free(current.path);
            continue;
        }
        if (looks_like_bare_repository(current.path) ||
            current.depth >= options->max_depth) {
            free(current.path);
            continue;
        }
        directory = opendir(current.path);
        if (directory == NULL) {
            free(current.path);
            continue;
        }
        while ((entry = readdir(directory)) != NULL) {
            char child[PATH_MAX];

            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0 ||
                strcmp(entry->d_name, ".git") == 0 ||
                name_is_excluded(entry->d_name, options)) {
                continue;
            }
            if (!entry_is_directory(current.path, entry)) {
                continue;
            }
            if (snprintf(child, sizeof(child), "%s/%s", current.path,
                         entry->d_name) >= (int)sizeof(child)) {
                continue;
            }
            if (scan_stack_push(&stack, child, current.depth + 1) != 0) {
                (void)closedir(directory);
                free(current.path);
                scan_stack_destroy(&stack);
                set_error(error, "Out of memory");
                return -1;
            }
        }
        (void)closedir(directory);
        free(current.path);
    }
    scan_stack_destroy(&stack);
    return 0;
}

int discover_repositories(const char *root, git_repository_list *list,
                          char **error)
{
    discovery_options options;

    discovery_options_init(&options);
    return discover_repositories_with_options(root, &options, list, error);
}

int discover_repositories_with_options(const char *root,
                                       const discovery_options *options,
                                       git_repository_list *list,
                                       char **error)
{
    char absolute[PATH_MAX];
    git_repository containing = { 0 };
    char *ignored_error = NULL;
    struct stat information;

    if (root == NULL || options == NULL || list == NULL) {
        set_error(error, "Invalid scan root");
        return -1;
    }
    memset(list, 0, sizeof(*list));
    if (realpath(root, absolute) == NULL ||
        stat(absolute, &information) != 0 ||
        !S_ISDIR(information.st_mode)) {
        set_error(error, strerror(errno));
        return -1;
    }
    if (git_repository_open(absolute, &containing, &ignored_error) == 0) {
        free(ignored_error);
        return git_repository_list_add(list, &containing);
    }
    free(ignored_error);
    if (scan_directories(absolute, list, error, options) != 0) {
        git_repository_list_destroy(list);
        return -1;
    }
    return 0;
}
