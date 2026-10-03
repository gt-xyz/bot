# Development

## Layout

    src/libbee/          every operation; both faces link this
    src/bee/             the command
    templates/common/    files every project gets, with {{placeholders}}
    templates/<profile>/ files that differ by profile, same placeholders
    test/check.sh        the checks

## Building

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ./test/check.sh

C++23, needing `std::expected` and `std::format`: GCC 13 or clang 18 with
libstdc++ 13 is the floor. `std::print` is deliberately avoided because it would
raise that floor past the current Ubuntu LTS for no benefit.

There are no third-party dependencies. Subprocesses are spawned directly rather
than through a library, because the only thing needed is "run this, capture
that," and the libraries that exist earn their keep on timeouts and
bidirectional pipes that nothing here wants. Reconsider when that stops being
true, not before.

## Checks

`./test/check.sh` is the whole verification story and takes the build directory
as its only argument. It runs the unit tests, then scaffolds every profile into
a temporary directory and asserts the result: expected files present, no
`{{placeholder}}` left unsubstituted, `devcontainer.json` valid JSON, a README, a vendor-neutral `AGENTS.md`, a first
piece of work seeded and marked next, `none` shipping no tests, unknown profiles refused.

It needs no network and no particular machine. That is a requirement rather than
a convenience — someone auditing this should be able to run it having none of
the infrastructure it was written on.

## The testing seam

`bee scaffold <directory> <name> <profile> <description> [notes-file]` writes
template files and stops — no git, no remote, no container. It exists so the
interesting half of `bee new` is testable without side effects, and it is why
the checks need no network. `BEE_TEMPLATES` points it at a template tree.

## Configuration

`~/.config/bee/config`, as `key = value` lines:

    projects     directory holding working copies
    templates    directory holding the template tree
    remote-host  ssh destination for bare repositories  (optional)
    remote-root  directory on that host                 (optional)

Unknown keys are refused rather than ignored, because a silently misspelled key
is a setting that appears to work.

Without a remote, `bee new` still creates a project and commits it locally. This
is what lets the checks run anywhere, and it means losing the remote degrades
bee rather than breaking it.

Nothing here belongs in the repository. A check asserts that no tracked file
contains an address, and that no scaffolded project does either.

## Project names

A name is restricted to letters, digits, dashes and underscores, starting
alphanumeric. It is not escaped, because it becomes an argument to a shell on
the far side of ssh and no quoting is correct for every remote shell. Refusing
the input is the only defence that holds.

## Adding a profile

1. `templates/<profile>/` with at least `docs/DEVELOPMENT.md` naming its real
   checks.
2. Add the name to `knownProfiles` in `src/libbee/project.cpp`, with its summary
   and image.
3. Extend the loop in `test/check.sh` and assert whatever is specific to it.

## Placeholders

`render` substitutes `{{name}}`, `{{description}}`, `{{profile}}`, `{{image}}`
and `{{notes}}` by literal replacement, so template files are otherwise inert.
Another tool's `${{ ... }}` survives untouched, since only those five keys are
replaced and an unknown key is copied through rather than emptied.
