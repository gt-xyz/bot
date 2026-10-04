# bot

One program on one machine you own: projects, and contained sessions of an
agent on them, managed from any device on your network.

This is the rewrite of bee. It keeps bee's reasons and changes three things,
each with a work note: the tool is `bot` and a running agent is a session
(0007); projects and sessions are separate things, and a session gets its own
clone (0007); a session sees its project and nothing else, proven by a check
rather than remembered (0008). The sections below were written for bee and
still hold unless a note says otherwise.

## Scope is a budget

bee's own rule, applied to its successor: a budget that fails a build beats
an intention that decays. The work notes are the whole scope.

- No feature without a work note, and a note says what it does not do.
- One binary, one library, files on disk, state derived. No database, no
  daemon, no queue, no plugin system.
- One adapter, the hosted model's CLI, until a second provider is actually
  wanted. The seam (0013) exists; the second adapter does not.
- One script in the web face, written here and small enough to read: no
  framework, no build step, nothing loaded from anywhere else, and a policy
  header under which the browser runs nothing but it. The pages work without
  it. A web terminal and a push relay are existing tools run beside it, never
  code here.
- A line budget on the library and the server, enforced by `test/check.sh`,
  raised only by a commit that says why.

## Why

Running many projects in parallel is only affordable if starting one is close to
free and returning to one costs no re-reading. Every unit of ceremony between
having an idea and being inside a session working on it is a place where the
idea is lost. bot removes that ceremony, and the design follows from it more
than from any technical constraint.

## Depth is a dial, and it ratchets

Two kinds of project have to share one command.

A **quick idea** must survive capture. If creating one demands a full interview,
half-formed ideas never get created at all — the tool would be filtering for
patience rather than for merit. So `bot init` asks three questions, none of them
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
authoritatively in memory, two bot processes can act at once without
coordinating. That is what makes the split below cheap.

## One library, two faces

`libbot` holds every operation. `bot` is a command that links it. `bot serve`
is the same binary answering HTTP so the same operations are reachable from a
phone.

`bot serve` answers one request and exits. Something else listens and hands it
the connection, so there is no listening code here and nothing running between
requests.

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
bot integrates with no hosting provider, and the work lives in the repository
as files under `work/` rather than in a service's database — which also means it arrives
through review, is diffable, and travels with a clone.

This is not only a question of ownership. A quota, an outage, or a policy change
somewhere else should not be able to stop work on a machine that is sitting
right there.

## What is deterministic and what is not

`bot init` asks only what changes bytes on disk: name, one-line description, and
toolchain profile. No model runs. Deriving the profile from prose was considered
and rejected — it means either a model inside `bot init`, which reintroduces the
cost this design exists to avoid, or a keyword matcher that fails silently and
is worse than a keypress.

Everything soft is derived later, by an agent, from non-empty documents. The
first session never rewrites scaffolding that `bot init` just wrote, because
`bot init` never guesses.

## You only pay for what you use

Idle costs nothing: the command runs and exits, and the server need not be
running at all for the command to work. Where that is not enough, the fix is a
budget that fails a build rather than an intention that decays — minimality
cannot be proven, but it can be measured and enforced.

The ordering matters more than the micro-optimisation. Work that is never read
is the largest waste available, and deleting it beats making it faster.

## Boundaries

bot knows what a project, a session, a piece of work and an agent are. Nothing else
does, which is why it exists.

It is **not** a scheduler. Sharing machines between people is a different
product with different owners, and there is prior art for it. bot submits work;
it does not decide whose work wins.

It is **not** an inference platform. Which agent runs inside a session is a
choice bot carries, not a service bot provides.

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

## A session is contained, and that is proven

bee gave every container its owner's credentials, agent state and network, and
nothing noticed. So the boundary here is not a list of intentions. A session is
given its own clone, its own home and a network that holds only its proxy; the
proxy connects to the names its owner allowed and nothing else. `bot check`
starts a session through the same code and tries, from inside, everything a
session must not be able to do. It is the proof, and it is run on the machine
that matters.

The session holds no credential for the remote and has no route to it. What it
commits is fetched out of its clone and pushed as a branch named after it, by
`bot stop`, without git ever running inside a tree the session could write.

## One image

Every session runs in the same image. A session cannot install into it, which
is the point of the boundary above, so the image carries the common toolchains
and a project writes down what it needs beyond them.

## Profiles

A profile chooses the files a new project starts with, and nothing else.

| profile   | for                                           |
|-----------|-----------------------------------------------|
| `cpp`     | cmake and clang                               |
| `scratch` | python and node together, exploratory ideas   |
| `none`    | notes and docs, no build                      |

`scratch` carries both runtimes deliberately, so an idea does not have to pick a
language at the moment it is captured.

## Scaffolding lives here

There is no separate template repository. bot writes the files itself from
`templates/`, which is what lets each profile emit its own scaffold instead of
cloning one language's and immediately overwriting it.
