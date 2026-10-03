# bee

One command per project: a repository, a container, a session, an agent.

## Why

Running many projects in parallel is only affordable if starting one is close to
free and returning to one costs no re-reading. Every unit of ceremony between
having an idea and being inside a session working on it is a place where the
idea is lost. bee removes that ceremony, and the design follows from it more
than from any technical constraint.

## Depth is a dial, and it ratchets

Two kinds of project have to share one command.

A **quick idea** must survive capture. If creating one demands a full interview,
half-formed ideas never get created at all — the tool would be filtering for
patience rather than for merit. So `bee new` asks three questions, none of them
open-ended, and the whole thing is over in well under a minute.

A **project being taken seriously** needs real thinking recorded: why now, where
v0.1 stops, what it will not do, what "working" means. That thinking is
expensive, it benefits from a conversation, and it is wasted on an idea that
turns out to be dead.

The resolution is that depth ratchets rather than being chosen up front. A fresh
`docs/PROJECT.md` carries the description and the raw notes; the seeded piece of
work asks for the rest. A project that never gets past that cost one minute, and that
is the point rather than a shortcoming.

## Nothing declares its own state

An earlier design had each project record a depth level in its own docs. That
was dropped before it grew: a label a project writes about itself can be wrong
the moment anything changes, and nothing was ever going to check it.

Everything worth knowing is already a fact in the repository. Whether the only
commit is still the scaffold, whether the tree is dirty, whether anything is
unpushed, which piece of work is marked next — all of it is derived by looking, never by
being told. Derived state cannot drift, and it costs no discipline to maintain.

This has a consequence that shapes the whole program: because no state is held
authoritatively in memory, two bee processes can act at once without
coordinating. That is what makes the split below cheap.

## One library, two faces

`libbee` holds every operation. `bee` is a command that links it. `bee serve`
is the same binary answering HTTP so the same operations are reachable from a
phone.

The alternative — a daemon that owns the logic, with thin clients calling it —
was rejected. It makes the service mandatory for terminal use, invents a
protocol to version, and stops a newcomer from cloning the repository and simply
running the thing. Here the command works with nothing else installed, offline,
and the server is additive.

The remote face is HTML over HTTP rather than a native client because the client
then already exists on every device, with nothing to install, and because HTML
outlives the GUI toolkits that would otherwise have to be kept working for as
long as this project lasts.

## It does not depend on a forge

Git is git. A project's durable home is a bare repository on a machine its owner
controls, reachable over ssh, and that is all a remote has ever needed to be.
bee integrates with no hosting provider, and the work lives in the repository
as files under `work/` rather than in a service's database — which also means it arrives
through review, is diffable, and travels with a clone.

This is not only a question of ownership. A quota, an outage, or a policy change
somewhere else should not be able to stop work on a machine that is sitting
right there.

## What is deterministic and what is not

`bee new` asks only what changes bytes on disk: name, one-line description, and
toolchain profile. No model runs. Deriving the profile from prose was considered
and rejected — it means either a model inside `bee new`, which reintroduces the
cost this design exists to avoid, or a keyword matcher that fails silently and
is worse than a keypress.

Everything soft is derived later, by an agent, from non-empty documents. The
first session never rewrites scaffolding that `bee new` just wrote, because
`bee new` never guesses.

## You only pay for what you use

Idle costs nothing: the command runs and exits, and the server need not be
running at all for the command to work. Where that is not enough, the fix is a
budget that fails a build rather than an intention that decays — minimality
cannot be proven, but it can be measured and enforced.

The ordering matters more than the micro-optimisation. Work that is never read
is the largest waste available, and deleting it beats making it faster.

## Boundaries

bee knows what a project, a session, a piece of work and an agent are. Nothing else
does, which is why it exists.

It is **not** a scheduler. Sharing machines between people is a different
product with different owners, and there is prior art for it. bee submits work;
it does not decide whose work wins.

It is **not** an inference platform. Which agent runs inside a session is a
choice bee carries, not a service bee provides.

It is single-user. Serving more than one person means acquiring authentication,
policy and users, and that is a different program.

## No vendor is named in a scaffold

A project carries `AGENTS.md`, not a file named after one company's agent. The
instructions are identical whichever agent reads them, and the knowledge they
point at lives in `docs/`, so nothing about a scaffolded project assumes which
model is running. A check enforces the absence rather than the intention.

This is the same rule as the one below, applied to vendors instead of machines.

## Nothing private lives here

No hostname, account, address or path belonging to a particular machine appears
in this repository, and a check enforces it. Everything machine-specific is
configuration, read from outside. The repository is meant to be auditable by
someone who has none of the infrastructure it runs on.

## Profiles

The axis is how serious the work is, not which language it is in.

| profile   | for                                           |
|-----------|-----------------------------------------------|
| `cpp`     | cmake and clang                               |
| `scratch` | python and node together, exploratory ideas   |
| `none`    | notes and docs, no build                      |

`scratch` carries both runtimes deliberately, so an idea does not have to pick a
language at the moment it is captured.

## Scaffolding lives here

There is no separate template repository. bee writes the files itself from
`templates/`, which is what lets each profile emit its own scaffold instead of
cloning one language's and immediately overwriting it.
