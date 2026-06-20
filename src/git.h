#ifndef GITROAM_GIT_H
#define GITROAM_GIT_H

#include <stdbool.h>
#include <stddef.h>

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
} git_worktree_list;

typedef enum {
    GIT_CREATE_OK = 0,
    GIT_CREATE_EXISTING,
    GIT_CREATE_INVALID_BRANCH,
    GIT_CREATE_AMBIGUOUS_REMOTE,
    GIT_CREATE_FAILED
} git_create_status;

void git_repository_destroy(git_repository *repository);
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

int git_primary_branch(const git_repository *repository, char **branch,
                       char **error);
char *git_default_worktree_path(const git_repository *repository,
                                const char *branch, char **error);
git_create_status git_create_worktree(const git_repository *repository,
                                      const char *branch,
                                      const char *destination,
                                      char **resolved_path, char **error);

#endif
