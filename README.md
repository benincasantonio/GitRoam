# GitRoam

GitRoam is a small C11 terminal application for discovering Git
repositories and working with their linked worktrees. Its interface is built
on a reusable, Git-independent ncurses library included as `libtui.a`.

## Build

Requirements:

- A C11 compiler
- Git
- Make
- ncurses (preferably discoverable as `ncursesw` or `ncurses` by
  `pkg-config`)

```sh
make
```

This produces:

- `gitroam` — the repository/worktree browser
- `libtui.a` — the reusable TUI library
- `tui-demo` — a small non-Git example

Run the tests with:

```sh
make test
make asan
```

## Usage

```sh
./gitroam [OPTIONS] [ROOT]
```

### Command-line options

| Option | Description | Default |
| --- | --- | --- |
| `[ROOT]` | Directory to search for Git repositories. | `GITROAM_ROOT`, then the current directory |
| `--exclude NAME` | Skip directories whose name exactly matches `NAME`. May be repeated. | No additional exclusions |
| `--max-depth NUMBER` | Limit recursive scanning depth. The scan root is depth `0`. | `128` |
| `--no-default-excludes` | Include dependency, cache, and generated-output directories that are normally skipped. | Default exclusions enabled |
| `-h`, `--help` | Display command-line help and exit. | — |
| `-V`, `--version` | Display the GitRoam version and exit. | — |

For large directory trees, the scan can be constrained:

```sh
./gitroam --max-depth 6 --exclude archives ~/Documents
./gitroam --no-default-excludes ~/Documents
```

The scan root is selected in this order:

1. The optional command-line argument
2. `GITROAM_ROOT`
3. The current directory

If the root is inside a Git repository, only that repository is opened.
Otherwise GitRoam recursively scans the root without following symbolic
links. Repositories sharing the same common Git directory are deduplicated.
Repository metadata is collected with one Git command per discovered
repository; worktree details are loaded only after a repository is selected.

By default, the scanner skips common dependency, cache, and generated-output
directories:

```text
node_modules  bower_components  .npm  .pnpm-store  .yarn  .bun
vendor  deps  third_party  external
.venv  venv  __pycache__  site-packages  .pytest_cache
.mypy_cache  .ruff_cache  .tox  .nox  .direnv
target  .cargo  .rustup  .gradle  .m2  .ivy2  .nuget
.bundle  .gem  build  dist  out  coverage  .cache
.parcel-cache  .vite  .next  .nuxt  .svelte-kit  .turbo
.angular  .expo  .serverless
.build  Pods  Carthage  DerivedData
.terraform  .dart_tool  .pub-cache  cmake-build-*
Library  .Trash
```

Use `--no-default-excludes` when repositories inside those directories should
also be found. `--exclude NAME` adds another exact directory name to skip and
may be repeated. `--max-depth NUMBER` limits recursion; the scan root has
depth zero.

Controls:

- Up/Down: move selection
- Enter: select or confirm
- Escape: go back
- `r`: refresh the repository list
- `q`: quit from menu screens

Selecting a repository provides actions to open its primary branch, select an
existing worktree, or create a worktree. The primary branch is resolved from
`origin/HEAD`, then `main`, then `master`.

New worktrees default to a sibling of the main worktree named
`<repository>-<branch>`. Relative destination paths are resolved beneath the
main worktree's parent directory. Existing local branches are attached,
unambiguous remote-only branches become tracking branches, and otherwise a new
branch is created from the primary branch.

Opening a worktree suspends the TUI and starts `$SHELL -i` there. Exiting the
shell returns to GitRoam.

## Reusable TUI library

The public API is [include/tui.h](include/tui.h). It contains no ncurses types
and provides:

- Application lifecycle and terminal suspension
- A screen stack
- Menus and action dialogs
- Text inputs and message dialogs
- Status bars
- Keyboard and resize events
- Callback contexts

Ownership rules:

- `tui_app_push_screen` and `tui_app_replace_screen` take ownership of a
  screen on success.
- `tui_screen_add_widget` takes ownership of a widget on success.
- Menu labels, dialog text, prompts, and input values are copied by the
  library.
- Widget callback contexts are borrowed and never freed by the library.
- A screen context can optionally supply a destructor.

Applications link against `libtui.a` and ncurses:

```sh
cc -Iinclude application.c libtui.a -lncurses
```

See [examples/tui_demo.c](examples/tui_demo.c) for a complete example. The
backend interface in `libtui/tui_backend.h` permits injecting a fake terminal
for deterministic tests.

## License

GitRoam is released under the BSD 3-Clause License. See [LICENSE](LICENSE) for
the full text.
