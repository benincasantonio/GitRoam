#include "discovery.h"
#include "git.h"
#include "process.h"

#include <errno.h>
#include <limits.h>
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

static int test_worktree_parser(void)
{
    static const char data[] =
        "worktree /tmp/main\0"
        "HEAD 111111\0"
        "branch refs/heads/main\0"
        "\0"
        "worktree /tmp/feature path\0"
        "HEAD 222222\0"
        "detached\0"
        "locked reason\0"
        "\0";
    git_worktree_list list = { 0 };

    CHECK(git_worktrees_parse(data, sizeof(data) - 1, &list) == 0);
    CHECK(list.count == 2);
    CHECK(strcmp(list.items[0].path, "/tmp/main") == 0);
    CHECK(strcmp(list.items[0].branch, "main") == 0);
    CHECK(strcmp(list.items[1].path, "/tmp/feature path") == 0);
    CHECK(list.items[1].detached);
    CHECK(list.items[1].locked);
    git_worktree_list_destroy(&list);
    return 0;
}

static int test_repository_dedup_index(void)
{
    git_repository_list list = { 0 };
    size_t index;

    for (index = 0; index < 40; index++) {
        git_repository repository = { 0 };
        char path[64];
        char common[64];
        char name[32];

        CHECK(snprintf(path, sizeof(path), "/tmp/repository-%zu", index) <
              (int)sizeof(path));
        CHECK(snprintf(common, sizeof(common), "/tmp/common-%zu", index) <
              (int)sizeof(common));
        CHECK(snprintf(name, sizeof(name), "repository-%zu", index) <
              (int)sizeof(name));
        repository.path = strdup(path);
        repository.common_dir = strdup(common);
        repository.name = strdup(name);
        CHECK(repository.path != NULL);
        CHECK(repository.common_dir != NULL);
        CHECK(repository.name != NULL);
        CHECK(git_repository_list_add(&list, &repository) == 0);
    }
    {
        git_repository duplicate = {
            strdup("/tmp/duplicate"),
            strdup("/tmp/common-17"),
            strdup("duplicate")
        };

        CHECK(duplicate.path != NULL);
        CHECK(duplicate.common_dir != NULL);
        CHECK(duplicate.name != NULL);
        CHECK(git_repository_list_add(&list, &duplicate) == 0);
        CHECK(duplicate.path == NULL);
    }
    CHECK(list.count == 40);
    git_repository_list_destroy(&list);
    return 0;
}

static int test_repository_copy(void)
{
    git_repository source = {
        strdup("/tmp/source"),
        strdup("/tmp/source/.git"),
        strdup("source")
    };
    git_repository copy = { 0 };

    CHECK(source.path != NULL && source.common_dir != NULL &&
          source.name != NULL);
    CHECK(git_repository_copy(&copy, &source) == 0);
    CHECK(copy.path != source.path && copy.common_dir != source.common_dir &&
          copy.name != source.name);
    CHECK(strcmp(copy.path, source.path) == 0);
    CHECK(strcmp(copy.common_dir, source.common_dir) == 0);
    CHECK(strcmp(copy.name, source.name) == 0);
    /* The copy must outlive the source independently. */
    git_repository_destroy(&source);
    CHECK(strcmp(copy.path, "/tmp/source") == 0);
    git_repository_destroy(&copy);
    CHECK(git_repository_copy(NULL, &copy) != 0);
    return 0;
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

static int integration_test(void)
{
    char template[] = "/tmp/gitroam-test-XXXXXX";
    char *root = mkdtemp(template);
    char repository_path[PATH_MAX];
    char file_path[PATH_MAX];
    char remote_path[PATH_MAX];
    char occupied[PATH_MAX];
    char marker[PATH_MAX];
    char remote_workspace[PATH_MAX];
    char dependency_parent[PATH_MAX];
    char dependency_repository[PATH_MAX];
    char nested_parent[PATH_MAX];
    char nested_child[PATH_MAX];
    char nested_repository[PATH_MAX];
    const char *init_arguments[] = {
        "git", "init", "-b", "main", repository_path, NULL
    };
    const char *config_email[] = {
        "git", "-C", repository_path, "config", "user.email",
        "test@example.com", NULL
    };
    const char *config_name[] = {
        "git", "-C", repository_path, "config", "user.name", "Test", NULL
    };
    const char *add_arguments[] = {
        "git", "-C", repository_path, "add", "file.txt", NULL
    };
    const char *commit_arguments[] = {
        "git", "-C", repository_path, "commit", "-m", "initial", NULL
    };
    const char *bare_arguments[] = {
        "git", "init", "--bare", "-b", "main", remote_path, NULL
    };
    const char *remote_add[] = {
        "git", "-C", repository_path, "remote", "add", "origin",
        remote_path, NULL
    };
    const char *push_main[] = {
        "git", "-C", repository_path, "push", "-u", "origin", "main", NULL
    };
    const char *branch_remote[] = {
        "git", "-C", repository_path, "branch", "remote-only", NULL
    };
    const char *push_remote[] = {
        "git", "-C", repository_path, "push", "origin", "remote-only", NULL
    };
    const char *delete_remote_local[] = {
        "git", "-C", repository_path, "branch", "-D", "remote-only", NULL
    };
    git_repository repository = { 0 };
    git_repository linked_repository = { 0 };
    git_repository_list discovered = { 0 };
    git_repository_list unpruned = { 0 };
    git_repository_list shallow = { 0 };
    git_worktree_list worktrees = { 0 };
    discovery_options options;
    char *primary = NULL;
    char *default_path = NULL;
    char *created_path = NULL;
    char *existing_path = NULL;
    char *error = NULL;
    git_create_status create_status;

    CHECK(root != NULL);
    CHECK(snprintf(repository_path, sizeof(repository_path), "%s/repo space",
                   root) < (int)sizeof(repository_path));
    CHECK(snprintf(remote_path, sizeof(remote_path), "%s/origin.git", root) <
          (int)sizeof(remote_path));
    CHECK(run_ok(init_arguments) == 0);
    CHECK(run_ok(config_email) == 0);
    CHECK(run_ok(config_name) == 0);
    CHECK(snprintf(file_path, sizeof(file_path), "%s/file.txt",
                   repository_path) < (int)sizeof(file_path));
    CHECK(write_file(file_path, "content\n") == 0);
    CHECK(run_ok(add_arguments) == 0);
    CHECK(run_ok(commit_arguments) == 0);
    CHECK(run_ok(bare_arguments) == 0);
    CHECK(run_ok(remote_add) == 0);
    CHECK(run_ok(push_main) == 0);

    CHECK(git_repository_open(repository_path, &repository, &error) == 0);
    CHECK(strcmp(repository.name, "repo space") == 0);
    CHECK(git_primary_branch(&repository, &primary, &error) == 0);
    CHECK(strcmp(primary, "main") == 0);
    free(primary);
    primary = NULL;
    CHECK(git_worktrees(&repository, &worktrees, &error) == 0);
    CHECK(worktrees.count == 1);
    CHECK(strcmp(worktrees.items[0].branch, "main") == 0);
    git_worktree_list_destroy(&worktrees);

    default_path = git_default_worktree_path(&repository, "feature/one",
                                             &error);
    CHECK(default_path != NULL);
    CHECK(strstr(default_path, "repo space-feature-one") != NULL);
    create_status = git_create_worktree(&repository, "feature/one",
                                        default_path, &created_path, &error);
    CHECK(create_status == GIT_CREATE_OK);
    CHECK(created_path != NULL);
    CHECK(git_repository_open(created_path, &linked_repository, &error) == 0);
    CHECK(strcmp(linked_repository.path, repository.path) == 0);
    CHECK(strcmp(linked_repository.common_dir, repository.common_dir) == 0);
    create_status = git_create_worktree(&repository, "feature/one",
                                        "/tmp/unused-gitroam-path",
                                        &existing_path, &error);
    CHECK(create_status == GIT_CREATE_EXISTING);
    CHECK(existing_path != NULL);
    CHECK(strcmp(created_path, existing_path) == 0);

    CHECK(run_ok(branch_remote) == 0);
    CHECK(run_ok(push_remote) == 0);
    CHECK(run_ok(delete_remote_local) == 0);
    CHECK(snprintf(remote_workspace, sizeof(remote_workspace),
                   "%s/remote workspace", root) <
          (int)sizeof(remote_workspace));
    create_status = git_create_worktree(&repository, "remote-only",
                                        "remote workspace", NULL, &error);
    CHECK(create_status == GIT_CREATE_OK);
    {
        struct stat workspace_information;
        CHECK(stat(remote_workspace, &workspace_information) == 0);
        CHECK(S_ISDIR(workspace_information.st_mode));
    }

    CHECK(snprintf(occupied, sizeof(occupied), "%s/occupied", root) <
          (int)sizeof(occupied));
    CHECK(mkdir(occupied, 0700) == 0);
    CHECK(snprintf(marker, sizeof(marker), "%s/marker", occupied) <
          (int)sizeof(marker));
    CHECK(write_file(marker, "keep\n") == 0);
    create_status = git_create_worktree(&repository, "blocked-branch",
                                        occupied, NULL, &error);
    CHECK(create_status == GIT_CREATE_FAILED);

    CHECK(discover_repositories(root, &discovered, &error) == 0);
    CHECK(discovered.count == 1);
    CHECK(strcmp(discovered.items[0].common_dir, repository.common_dir) == 0);

    CHECK(snprintf(dependency_parent, sizeof(dependency_parent),
                   "%s/node_modules", root) <
          (int)sizeof(dependency_parent));
    CHECK(mkdir(dependency_parent, 0700) == 0);
    CHECK(snprintf(dependency_repository, sizeof(dependency_repository),
                   "%s/dependency-repo", dependency_parent) <
          (int)sizeof(dependency_repository));
    {
        const char *dependency_init[] = {
            "git", "init", "-b", "main", dependency_repository, NULL
        };
        CHECK(run_ok(dependency_init) == 0);
    }

    CHECK(snprintf(nested_parent, sizeof(nested_parent), "%s/nested", root) <
          (int)sizeof(nested_parent));
    CHECK(mkdir(nested_parent, 0700) == 0);
    CHECK(snprintf(nested_child, sizeof(nested_child), "%s/child",
                   nested_parent) < (int)sizeof(nested_child));
    CHECK(mkdir(nested_child, 0700) == 0);
    CHECK(snprintf(nested_repository, sizeof(nested_repository),
                   "%s/deep-repo", nested_child) <
          (int)sizeof(nested_repository));
    {
        const char *nested_init[] = {
            "git", "init", "-b", "main", nested_repository, NULL
        };
        CHECK(run_ok(nested_init) == 0);
    }

    git_repository_list_destroy(&discovered);
    CHECK(discover_repositories(root, &discovered, &error) == 0);
    CHECK(discovered.count == 2);

    discovery_options_init(&options);
    options.use_default_excludes = false;
    CHECK(discover_repositories_with_options(root, &options, &unpruned,
                                             &error) == 0);
    CHECK(unpruned.count == 3);

    discovery_options_init(&options);
    options.max_depth = 1;
    CHECK(discover_repositories_with_options(root, &options, &shallow,
                                             &error) == 0);
    CHECK(shallow.count == 1);

    git_repository_list_destroy(&discovered);
    git_repository_list_destroy(&unpruned);
    git_repository_list_destroy(&shallow);
    git_repository_destroy(&linked_repository);
    git_repository_destroy(&repository);
    free(default_path);
    free(created_path);
    free(existing_path);
    free(error);
    {
        const char *cleanup[] = { "rm", "-rf", root, NULL };
        CHECK(run_ok(cleanup) == 0);
    }
    return 0;
}

int main(void)
{
    if (test_worktree_parser() != 0 ||
        test_repository_dedup_index() != 0 ||
        test_repository_copy() != 0 ||
        integration_test() != 0) {
        return EXIT_FAILURE;
    }
    puts("test-git: ok");
    return EXIT_SUCCESS;
}
