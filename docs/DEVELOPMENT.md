# Development

## Layout

    src/libbot/          every operation; both faces link this
    src/bot/             the command
    templates/common/    files every project gets, with {{placeholders}}
    templates/<profile>/ files that differ by profile, same placeholders
    images/session/      the image every session runs in
    images/proxy/        the image of a session's proxy
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

`cmake --install build` puts the command in the prefix's `bin` and the
templates and image definitions in its `share/bot`, which is where the command
looks for them unless the config says otherwise.

## Checks

`./test/check.sh` is the whole verification story for the code and takes the
build directory as its only argument. It runs the unit tests, then:

- scaffolds every profile and asserts the result: expected files present, no
  `{{placeholder}}` left unsubstituted, a README, a vendor-neutral `AGENTS.md`,
  no mention of a vendor or of this tool, nothing that tells a container what
  to mount, a first piece of work seeded and marked next, `none` shipping no
  tests, unknown profiles refused;
- creates a project and starts a session against a stand-in for the container
  runtime, and asserts exactly what the session was given: its own network,
  two mounts, no capability, nothing from the owner's home;
- runs `bot check` against that stand-in, which contains nothing, and fails
  unless `bot check` fails and names what leaked;
- has a session commit, plant a hook and hostile git configuration, and then
  asserts the branch was published, `main` did not move and nothing planted ran;
- starts a session that takes messages, and asserts it is refused where
  `bot check` fails and that its inbox is mounted read-only;
- runs the adapter in `images/session` against a stand-in agent and reads its
  output back as a transcript;
- reads the web face through a pipe, as nobody, as its owner and as someone
  else, and asserts that only its owner's own forms are acted on;
- holds the library and the command to a line budget.

It needs no network, no container runtime and no particular machine. That is a
requirement rather than a convenience — someone auditing this should be able to
run it having none of the infrastructure it was written on.

What it cannot show is that a real runtime enforces what was asked of it. That
is `bot check`, on the host: see `docs/HOST.md`.

## The testing seams

`bot scaffold <directory> <name> <profile> <description> [notes-file]` writes
template files and stops — no git, no remote, no container. It exists so the
interesting half of `bot init` is testable without side effects.
`BOT_TEMPLATES` points it at a template tree.

`runtime` in the config names the container runtime. Every use of it goes
through that one key, which is what lets the checks stand a script in its place.

## Configuration

`~/.config/bot/config`, as `key = value` lines:

    sessions     directory holding one directory per session
    remote-root  directory holding the bare repositories
    remote-host  ssh destination, if they are on another machine    (optional)
    agent        the command a session runs
    agent-env    file of NAME=value lines for the agent's credential (optional)
    agent-home   directory copied into each session's home           (optional)
    allow        host names a session may reach, separated by spaces (optional)
    probe        addresses `bot check` always tries, beyond its own  (optional)
    owner        who the web face obeys; without it, it only reads   (optional)
    whois        command that prints who is at an address            (optional)
    runtime      container runtime; `podman` unless set              (optional)
    templates    template tree, if not the installed one             (optional)
    images       image definitions, if not the installed ones        (optional)

Unknown keys are refused rather than ignored, because a silently misspelled key
is a setting that appears to work. An entry in `allow` that is not a plain host
name is refused too: it becomes a line in the proxy's filter, and a pattern or
an address there would be a hole.

Nothing here belongs in the repository. A check asserts that no tracked file
contains an address, and that no scaffolded project does either.

## A session

`bot up <project>` makes a directory under `sessions`, named for the project
and the time:

    tree/     a fresh clone, on a branch named after the session, with no remote
    home/     the agent's home: a copy of `agent-home`, or empty
    proxy/    the proxy's configuration and the names it will connect to
    log       what the session printed

and starts two containers and a network, all named after the session. The
session's container is on an internal network with no gateway and no resolver.
Its proxy is on that network and on one that leads out. The whole of what the
session is given is one command line, built by `session_arguments` in
`src/libbot/session.cpp`, and `bot check` starts its probe through the same
function, so what is proven is what runs.

`bot run <project> [message]` starts a session with no terminal. It has a
third mount, read-only:

    inbox/    messages, one file each: 0001, 0002, ...

and runs the image's adapter, `agent-loop`, which is the whole of what bot
knows about any agent: a message in the inbox is one turn, and the turn comes
back on standard output as records of a kind and a text. The runtime writes
that output to `log`, and the transcript is read from there. A message is sent
by writing the next file, which is all `bot say` and the web face's form do,
so sending needs no process to be running. The session cannot write its inbox,
and a turn is under way exactly when more messages have been sent than turns
have ended; neither is recorded anywhere.

`bot run` runs `bot check` first and starts nothing if it fails.

The adapter does not have its agent ask permission for what it does. What a
session may do is settled by the container, which holds whether or not the
agent asks.

`bot stop` publishes. It fetches the session's commits out of `tree/` into a
repository the session never saw and pushes from there, as a branch named
after the session. Git is never run inside `tree/` once the session has
started, because the session could write that repository's hooks and
configuration.

The board reads `main` of each bare repository and the list of containers.
Nothing is stored about a session beyond its directory.

## The web face

`bot serve` answers one request on standard input and exits, so there is no
listening code: a socket unit listens and starts it per connection.

With no `owner` it answers `GET` and nothing else. With one, it acts, and so:

- Every request must be the owner's. Who is asking is the one thing a request
  cannot claim for itself, the address it came from, which the socket unit
  hands over. `whois` is run with that address and must print `owner`.
- A form is acted on only if its `Origin` is this site and this site's name is
  this machine's host name or an address. A page somewhere else cannot press
  the buttons, and neither can a name somewhere else that resolves here.

A session's page reloads itself while a turn is under way and has no form
then, because a reload would empty it; `?write` is the same page standing
still.

## Project names

A name is restricted to letters, digits, dashes and underscores, starting
alphanumeric. It is not escaped, because it becomes an argument to a shell on
the far side of ssh and no quoting is correct for every remote shell. Refusing
the input is the only defence that holds.

## Adding a profile

1. `templates/<profile>/` with at least `docs/DEVELOPMENT.md` naming its real
   checks.
2. Add the name to `knownProfiles` in `src/libbot/project.cpp`, with its summary.
3. Extend the loop in `test/check.sh` and assert whatever is specific to it.

A profile is starter files only. Every session runs in the one image, so a
toolchain a profile needs is added to `images/session/Containerfile`.

## Placeholders

`render` substitutes `{{name}}`, `{{description}}`, `{{profile}}` and
`{{notes}}` by literal replacement, so template files are otherwise inert.
Another tool's `${{ ... }}` survives untouched, since only those four keys are
replaced and an unknown key is copied through rather than emptied.
