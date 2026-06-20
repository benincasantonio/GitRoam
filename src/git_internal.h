#ifndef GITROAM_GIT_INTERNAL_H
#define GITROAM_GIT_INTERNAL_H

#include "git.h"
#include "process.h"

char *git_internal_string_copy(const char *value);
void git_internal_set_error(char **error, const char *message);
int git_internal_run(const git_repository *repository,
                     const char *const arguments[], process_result *result);
int git_internal_command_text(const git_repository *repository,
                              const char *const arguments[], char **output,
                              char **error);

#endif
