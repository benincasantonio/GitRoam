#include "git.h"
#include "process.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #condition);                                             \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    char root[PATH_MAX];
    char repository_path[PATH_MAX];
    char remote_path[PATH_MAX];
    char merged_path[PATH_MAX];
    char unmerged_path[PATH_MAX];
    char tracked_path[PATH_MAX];
    char dirty_path[PATH_MAX];
    char locked_path[PATH_MAX];
    char detached_path[PATH_MAX];
    char gone_path[PATH_MAX];
    char inspection_path[PATH_MAX];
    char stale_path[PATH_MAX];
    git_repository repository;
    char *error;
} cleanup_fixture;

static int run_ok(const char *const argv[])
{
    process_result result;
    int ok;

    if (process_run(argv, &result) != 0) {
        fprintf(stderr, "could not run %s: %s\n", argv[0], strerror(errno));
        return -1;
    }
    ok = result.exit_code == 0;
    if (!ok) {
        fprintf(stderr, "%s", result.stderr_data);
    }
    process_result_destroy(&result);
    return ok ? 0 : -1;
}

static int write_file(const char *path, const char *contents)
{
    FILE *file = fopen(path, "w");

    if (file == NULL) {
        return -1;
    }
    if (fputs(contents, file) == EOF || fclose(file) != 0) {
        return -1;
    }
    return 0;
}

static int fixture_path(char *destination, size_t size, const char *root,
                        const char *name)
{
    return snprintf(destination, size, "%s/%s", root, name) < (int)size ?
           0 : -1;
}

static int fixture_init(cleanup_fixture *fixture)
{
    char template[] = "/tmp/gitroam-cleanup-XXXXXX";
    char *temporary = mkdtemp(template);
    char primary_file[PATH_MAX];
    const char *init[] = {
        "git", "init", "-b", "main", fixture->repository_path, NULL
    };
    const char *email[] = {
        "git", "-C", fixture->repository_path, "config", "user.email",
        "test@example.com", NULL
    };
    const char *name[] = {
        "git", "-C", fixture->repository_path, "config", "user.name", "Test",
        NULL
    };
    const char *add[] = {
        "git", "-C", fixture->repository_path, "add", "file.txt", NULL
    };
    const char *commit[] = {
        "git", "-C", fixture->repository_path, "commit", "-m", "initial",
        NULL
    };
    const char *bare[] = {
        "git", "init", "--bare", "-b", "main", fixture->remote_path, NULL
    };
    const char *remote[] = {
        "git", "-C", fixture->repository_path, "remote", "add", "origin",
        fixture->remote_path, NULL
    };
    const char *push[] = {
        "git", "-C", fixture->repository_path, "push", "-u", "origin",
        "main", NULL
    };

    CHECK(temporary != NULL);
    CHECK(realpath(temporary, fixture->root) != NULL);
    CHECK(fixture_path(fixture->repository_path,
                       sizeof(fixture->repository_path), fixture->root,
                       "repository") == 0);
    CHECK(fixture_path(fixture->remote_path, sizeof(fixture->remote_path),
                       fixture->root, "origin.git") == 0);
    CHECK(fixture_path(fixture->merged_path, sizeof(fixture->merged_path),
                       fixture->root, "merged workspace") == 0);
    CHECK(fixture_path(fixture->unmerged_path,
                       sizeof(fixture->unmerged_path), fixture->root,
                       "unmerged workspace") == 0);
    CHECK(fixture_path(fixture->tracked_path,
                       sizeof(fixture->tracked_path), fixture->root,
                       "tracked workspace") == 0);
    CHECK(fixture_path(fixture->dirty_path, sizeof(fixture->dirty_path),
                       fixture->root, "dirty workspace") == 0);
    CHECK(fixture_path(fixture->locked_path, sizeof(fixture->locked_path),
                       fixture->root, "locked workspace") == 0);
    CHECK(fixture_path(fixture->detached_path,
                       sizeof(fixture->detached_path), fixture->root,
                       "detached workspace") == 0);
    CHECK(fixture_path(fixture->gone_path, sizeof(fixture->gone_path),
                       fixture->root, "gone workspace") == 0);
    CHECK(fixture_path(fixture->inspection_path,
                       sizeof(fixture->inspection_path), fixture->root,
                       "inspection workspace") == 0);
    CHECK(fixture_path(fixture->stale_path, sizeof(fixture->stale_path),
                       fixture->root, "stale workspace") == 0);
    CHECK(fixture_path(primary_file, sizeof(primary_file),
                       fixture->repository_path, "file.txt") == 0);
    CHECK(run_ok(init) == 0);
    CHECK(run_ok(email) == 0);
    CHECK(run_ok(name) == 0);
    CHECK(write_file(primary_file, "initial\n") == 0);
    CHECK(run_ok(add) == 0);
    CHECK(run_ok(commit) == 0);
    CHECK(run_ok(bare) == 0);
    CHECK(run_ok(remote) == 0);
    CHECK(run_ok(push) == 0);
    CHECK(git_repository_open(fixture->repository_path, &fixture->repository,
                              &fixture->error) == 0);
    return 0;
}

static int commit_worktree(const cleanup_fixture *fixture,
                           const char *branch, const char *path,
                           const char *filename)
{
    char file_path[PATH_MAX];
    const char *add_worktree[] = {
        "git", "-C", fixture->repository_path, "worktree", "add", "-b",
        branch, path, NULL
    };
    const char *add_file[] = {
        "git", "-C", path, "add", filename, NULL
    };
    const char *commit[] = {
        "git", "-C", path, "commit", "-m", branch, NULL
    };

    CHECK(fixture_path(file_path, sizeof(file_path), path, filename) == 0);
    CHECK(run_ok(add_worktree) == 0);
    CHECK(write_file(file_path, branch) == 0);
    CHECK(run_ok(add_file) == 0);
    CHECK(run_ok(commit) == 0);
    return 0;
}

static int create_ordinary_worktrees(cleanup_fixture *fixture)
{
    char dirty_file[PATH_MAX];
    const char *merge[] = {
        "git", "-C", fixture->repository_path, "merge", "--no-ff",
        "merged-feature", "-m", "merge feature", NULL
    };
    const char *add_dirty[] = {
        "git", "-C", fixture->repository_path, "worktree", "add", "-b",
        "dirty-feature", fixture->dirty_path, NULL
    };
    const char *add_locked[] = {
        "git", "-C", fixture->repository_path, "worktree", "add", "-b",
        "locked-feature", fixture->locked_path, NULL
    };
    const char *lock[] = {
        "git", "-C", fixture->repository_path, "worktree", "lock",
        fixture->locked_path, NULL
    };

    CHECK(commit_worktree(fixture, "merged-feature", fixture->merged_path,
                          "merged.txt") == 0);
    CHECK(run_ok(merge) == 0);
    CHECK(commit_worktree(fixture, "unmerged-feature",
                          fixture->unmerged_path, "unmerged.txt") == 0);
    CHECK(run_ok(add_dirty) == 0);
    CHECK(fixture_path(dirty_file, sizeof(dirty_file), fixture->dirty_path,
                       "untracked.txt") == 0);
    CHECK(write_file(dirty_file, "dirty\n") == 0);
    CHECK(run_ok(add_locked) == 0);
    CHECK(run_ok(lock) == 0);
    return 0;
}

static int create_detached_worktree(cleanup_fixture *fixture)
{
    char file_path[PATH_MAX];
    const char *add_worktree[] = {
        "git", "-C", fixture->repository_path, "worktree", "add", "--detach",
        fixture->detached_path, "main", NULL
    };
    const char *add_file[] = {
        "git", "-C", fixture->detached_path, "add", "detached.txt", NULL
    };
    const char *commit[] = {
        "git", "-C", fixture->detached_path, "commit", "-m",
        "detached commit", NULL
    };

    CHECK(fixture_path(file_path, sizeof(file_path), fixture->detached_path,
                       "detached.txt") == 0);
    CHECK(run_ok(add_worktree) == 0);
    CHECK(write_file(file_path, "detached\n") == 0);
    CHECK(run_ok(add_file) == 0);
    CHECK(run_ok(commit) == 0);
    return 0;
}

static int create_upstream_gone_worktree(cleanup_fixture *fixture)
{
    const char *push[] = {
        "git", "-C", fixture->gone_path, "push", "-u", "origin",
        "gone-feature", NULL
    };
    const char *delete_remote[] = {
        "git", "-C", fixture->gone_path, "push", "origin", "--delete",
        "gone-feature", NULL
    };

    CHECK(commit_worktree(fixture, "gone-feature", fixture->gone_path,
                          "gone.txt") == 0);
    CHECK(run_ok(push) == 0);
    CHECK(run_ok(delete_remote) == 0);
    return 0;
}

static int create_tracked_worktree(cleanup_fixture *fixture)
{
    const char *push[] = {
        "git", "-C", fixture->tracked_path, "push", "-u", "origin",
        "tracked-feature", NULL
    };

    CHECK(commit_worktree(fixture, "tracked-feature", fixture->tracked_path,
                          "tracked.txt") == 0);
    CHECK(run_ok(push) == 0);
    return 0;
}

static int create_invalid_worktrees(cleanup_fixture *fixture)
{
    char git_file[PATH_MAX];
    const char *add_inspection[] = {
        "git", "-C", fixture->repository_path, "worktree", "add", "-b",
        "inspection-feature", fixture->inspection_path, NULL
    };
    const char *add_stale[] = {
        "git", "-C", fixture->repository_path, "worktree", "add", "-b",
        "stale-feature", fixture->stale_path, NULL
    };
    const char *remove_stale[] = {
        "rm", "-rf", fixture->stale_path, NULL
    };

    CHECK(run_ok(add_inspection) == 0);
    CHECK(fixture_path(git_file, sizeof(git_file), fixture->inspection_path,
                       ".git") == 0);
    CHECK(write_file(git_file, "gitdir: /does/not/exist\n") == 0);
    CHECK(run_ok(add_stale) == 0);
    CHECK(run_ok(remove_stale) == 0);
    return 0;
}

static git_worktree_cleanup *find_cleanup(git_worktree_cleanup_list *list,
                                          const char *branch)
{
    size_t index;

    for (index = 0; index < list->count; index++) {
        if (list->items[index].branch != NULL &&
            strcmp(list->items[index].branch, branch) == 0) {
            return &list->items[index];
        }
    }
    return NULL;
}

static int verify_classifications(cleanup_fixture *fixture)
{
    git_worktree_cleanup_list list = { 0 };
    git_worktree_cleanup *item;
    bool found_detached = false;
    size_t index;

    CHECK(git_worktree_cleanup_scan(&fixture->repository, &list,
                                    &fixture->error) == 0);
#define CHECK_STATE(branch_name, expected_state)                             \
    do {                                                                     \
        item = find_cleanup(&list, branch_name);                             \
        CHECK(item != NULL);                                                 \
        CHECK(item->state == expected_state);                                \
    } while (0)
    CHECK_STATE("main", GIT_WORKTREE_CLEANUP_PRIMARY);
    CHECK_STATE("merged-feature", GIT_WORKTREE_CLEANUP_MERGED);
    CHECK(item->last_commit > (time_t)0);
    CHECK_STATE("unmerged-feature", GIT_WORKTREE_CLEANUP_LOCAL_UNMERGED);
    CHECK_STATE("tracked-feature", GIT_WORKTREE_CLEANUP_CLEAN_UNMERGED);
    CHECK_STATE("dirty-feature", GIT_WORKTREE_CLEANUP_DIRTY);
    CHECK_STATE("locked-feature", GIT_WORKTREE_CLEANUP_LOCKED);
    CHECK_STATE("gone-feature", GIT_WORKTREE_CLEANUP_UPSTREAM_GONE);
    CHECK_STATE("stale-feature", GIT_WORKTREE_CLEANUP_STALE_METADATA);
    CHECK_STATE("inspection-feature",
                GIT_WORKTREE_CLEANUP_INSPECTION_FAILED);
#undef CHECK_STATE
    for (index = 0; index < list.count; index++) {
        if (list.items[index].branch == NULL &&
            list.items[index].state ==
                GIT_WORKTREE_CLEANUP_DETACHED_UNMERGED) {
            found_detached = true;
        }
    }
    CHECK(found_detached);
    git_worktree_cleanup_list_destroy(&list);
    return 0;
}

static int verify_removal(cleanup_fixture *fixture)
{
    struct stat information;
    const char *branch_exists[] = {
        "git", "-C", fixture->repository_path, "show-ref", "--verify",
        "--quiet", "refs/heads/tracked-feature", NULL
    };

    CHECK(git_worktree_remove_safe(&fixture->repository,
                                   fixture->repository_path,
                                   &fixture->error) != 0);
    CHECK(strstr(fixture->error, "primary") != NULL);
    CHECK(git_worktree_remove_safe(&fixture->repository, fixture->dirty_path,
                                   &fixture->error) != 0);
    CHECK(strstr(fixture->error, "modified or untracked") != NULL);
    CHECK(git_worktree_remove_safe(&fixture->repository, fixture->locked_path,
                                   &fixture->error) != 0);
    CHECK(strstr(fixture->error, "locked") != NULL);
    CHECK(git_worktree_remove_safe(&fixture->repository,
                                   fixture->detached_path,
                                   &fixture->error) != 0);
    CHECK(strstr(fixture->error, "detached") != NULL);
    CHECK(git_worktree_remove_safe(&fixture->repository, fixture->stale_path,
                                   &fixture->error) != 0);
    CHECK(strstr(fixture->error, "pruning") != NULL);
    CHECK(git_worktree_remove_safe(&fixture->repository,
                                   fixture->inspection_path,
                                   &fixture->error) != 0);
    CHECK(strstr(fixture->error, "inspected safely") != NULL);
    CHECK(git_worktree_remove_safe(&fixture->repository,
                                   fixture->unmerged_path,
                                   &fixture->error) != 0);
    CHECK(strstr(fixture->error, "unpushed") != NULL);
    CHECK(stat(fixture->unmerged_path, &information) == 0);

    CHECK(git_worktree_remove_safe(&fixture->repository,
                                   fixture->tracked_path,
                                   &fixture->error) == 0);
    CHECK(stat(fixture->tracked_path, &information) != 0 && errno == ENOENT);
    CHECK(run_ok(branch_exists) == 0);
    CHECK(git_worktree_remove_safe(&fixture->repository, fixture->merged_path,
                                   &fixture->error) == 0);
    CHECK(stat(fixture->merged_path, &information) != 0 && errno == ENOENT);
    CHECK(git_worktree_remove_safe(&fixture->repository, fixture->gone_path,
                                   &fixture->error) == 0);
    CHECK(stat(fixture->gone_path, &information) != 0 && errno == ENOENT);
    return 0;
}

static int verify_pruning(cleanup_fixture *fixture)
{
    git_worktree_cleanup_list list = { 0 };

    CHECK(git_worktree_prune(&fixture->repository, &fixture->error) == 0);
    CHECK(git_worktree_cleanup_scan(&fixture->repository, &list,
                                    &fixture->error) == 0);
    CHECK(find_cleanup(&list, "stale-feature") == NULL);
    git_worktree_cleanup_list_destroy(&list);
    return 0;
}

static int fixture_destroy(cleanup_fixture *fixture)
{
    const char *remove_root[] = { "rm", "-rf", fixture->root, NULL };

    git_repository_destroy(&fixture->repository);
    free(fixture->error);
    return run_ok(remove_root);
}

int main(void)
{
    cleanup_fixture fixture = { 0 };

    if (fixture_init(&fixture) != 0 ||
        create_ordinary_worktrees(&fixture) != 0 ||
        create_detached_worktree(&fixture) != 0 ||
        create_upstream_gone_worktree(&fixture) != 0 ||
        create_tracked_worktree(&fixture) != 0 ||
        create_invalid_worktrees(&fixture) != 0 ||
        verify_classifications(&fixture) != 0 ||
        verify_removal(&fixture) != 0 ||
        verify_pruning(&fixture) != 0 ||
        fixture_destroy(&fixture) != 0) {
        return EXIT_FAILURE;
    }
    puts("test-cleanup: ok");
    return EXIT_SUCCESS;
}
