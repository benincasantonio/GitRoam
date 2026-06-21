#ifndef GITROAM_GIT_H
#define GITROAM_GIT_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef struct {
    char *path;
    char *common_dir;
    char *name;
} git_repository;

typedef struct {
    git_repository *items;
    size_t count;
    size_t capacity;
    char **index_keys;
    size_t index_capacity;
} git_repository_list;

typedef struct {
    char *path;
    char *head;
    char *branch;
    bool detached;
    bool locked;
    bool prunable;
} git_worktree;

typedef struct {
    git_worktree *items;
    size_t count;
    size_t capacity;
} git_worktree_list;

typedef enum {
    GIT_WORKTREE_CLEANUP_PRIMARY = 0,
    GIT_WORKTREE_CLEANUP_LOCKED,
    GIT_WORKTREE_CLEANUP_DIRTY,
    GIT_WORKTREE_CLEANUP_STALE_METADATA,
    GIT_WORKTREE_CLEANUP_MERGED,
    GIT_WORKTREE_CLEANUP_UPSTREAM_GONE,
    GIT_WORKTREE_CLEANUP_CLEAN_UNMERGED,
    GIT_WORKTREE_CLEANUP_LOCAL_UNMERGED,
    GIT_WORKTREE_CLEANUP_DETACHED_UNMERGED,
    GIT_WORKTREE_CLEANUP_INSPECTION_FAILED
} git_worktree_cleanup_state;

typedef struct {
    char *path;
    char *branch;
    char *upstream;
    git_worktree_cleanup_state state;
    time_t last_commit;
} git_worktree_cleanup;

typedef struct {
    git_worktree_cleanup *items;
    size_t count;
} git_worktree_cleanup_list;

typedef enum {
    GIT_CREATE_OK = 0,
    GIT_CREATE_EXISTING,
    GIT_CREATE_INVALID_BRANCH,
    GIT_CREATE_AMBIGUOUS_REMOTE,
    GIT_CREATE_FAILED
} git_create_status;

void git_repository_destroy(git_repository *repository);
int git_repository_copy(git_repository *destination,
                        const git_repository *source);
void git_repository_list_destroy(git_repository_list *list);
int git_repository_open(const char *path, git_repository *repository,
                        char **error);
int git_repository_list_add(git_repository_list *list,
                            git_repository *repository);

int git_worktrees(const git_repository *repository, git_worktree_list *list,
                  char **error);
int git_worktrees_parse(const char *data, size_t length,
                        git_worktree_list *list);
void git_worktree_list_destroy(git_worktree_list *list);
int git_worktree_cleanup_scan(const git_repository *repository,
                              git_worktree_cleanup_list *list, char **error);
void git_worktree_cleanup_list_destroy(git_worktree_cleanup_list *list);
bool git_worktree_cleanup_is_removable(git_worktree_cleanup_state state);
int git_worktree_remove_safe(const git_repository *repository,
                             const char *path, char **error);
int git_worktree_prune(const git_repository *repository, char **error);

int git_primary_branch(const git_repository *repository, char **branch,
                       char **error);
char *git_default_worktree_path(const git_repository *repository,
                                const char *branch, char **error);
git_create_status git_create_worktree(const git_repository *repository,
                                      const char *branch,
                                      const char *destination,
                                      char **resolved_path, char **error);

#endif
