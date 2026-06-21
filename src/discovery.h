#ifndef GITROAM_DISCOVERY_H
#define GITROAM_DISCOVERY_H

#include "git.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    unsigned int max_depth;
    bool use_default_excludes;
    const char *const *excluded_names;
    size_t excluded_count;
} discovery_options;

void discovery_options_init(discovery_options *options);
int discover_repositories(const char *root, git_repository_list *list,
                          char **error);
int discover_repositories_with_options(const char *root,
                                       const discovery_options *options,
                                       git_repository_list *list,
                                       char **error);

#endif
