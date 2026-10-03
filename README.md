# bee

One command per project: a repository, a container, a session, an agent.

## Start here

- `docs/PROJECT.md` — what this is, why it exists, and what it deliberately is not
- `docs/DEVELOPMENT.md` — how to build it and how it is verified
- `work/` — what happens next; the one marked `next: true` is the one to pick up

Everything above is meant to be read. Anything beginning with a dot is
configuration for a tool, not for you.

## Building

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ./test/check.sh build

C++23, no third-party dependencies. The checks need no network and no
particular machine, so you can audit this having none of the infrastructure it
was written on.

## Configuring

`~/.config/bee/config`, as `key = value` lines; `docs/DEVELOPMENT.md` lists
them. Nothing machine-specific belongs in this repository, and a check enforces
that.
