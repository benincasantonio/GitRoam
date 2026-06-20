#ifndef GITROAM_WORKTREE_CLEANUP_H
#define GITROAM_WORKTREE_CLEANUP_H

#include "git.h"
#include "tui.h"

void worktree_cleanup_open(tui_app *app,
                           const git_repository *repository);

#endif
