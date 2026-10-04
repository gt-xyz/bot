# bot

Projects, and contained sessions of an agent on them, on one machine you own.

## Start here

- `docs/PROJECT.md` — what this is, why it exists, and what it deliberately is not
- `docs/DEVELOPMENT.md` — how to build it and how it is verified
- `docs/HOST.md` — setting up the machine it runs on, and proving a session is contained there
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

`~/.config/bot/config`, as `key = value` lines; `docs/DEVELOPMENT.md` lists
them. Nothing machine-specific belongs in this repository, and a check enforces
that.
