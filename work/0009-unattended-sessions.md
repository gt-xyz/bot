---
title: bot run, an unattended session
---

`bot up` starts a session with you in it. `bot run <project>` starts one that
reads `AGENTS.md`, takes the piece of work marked `next`, works on a branch,
opens a pull request, writes its log, and exits. You are not there, and that
is the feature: several sessions on several projects, each ending in something
reviewable. And because every session is the same thing, you can attach to an
unattended one at any time, watch, and type.

- A fresh clone from the remote, on a branch named after the work note, never
  `main`. The ruleset blocks `main` anyway; this is manners, not the guarantee.
- Every commit carries the trailer naming the model, so the authorship bars on
  the owner's site stay honest and a later reader knows what wrote what.
- The log is a file in the session's directory, tailed by `bot log` and shown
  by the web face. Not in the repository: a log is not work.
- A session that finishes without a pull request says why in the log and
  exits non-zero, so the board shows failed rather than done.
- A time and a token budget per run, from the project's configuration, with
  defaults that stop a looping agent. Fails the run rather than decaying.
- The pull request is the only output anyone reads; the branch is its evidence.

Which agent runs is configuration, as 0001 says. The unattended contract is
small on purpose: a program that can read a file, work in a directory and push
a branch can be the agent, whether it is a hosted frontier model or one served
on the fleet under `local`. Porting between them means changing one line.

Depends on 0001 (sessions) and 0008 (containment). The first run is a public
experiment repository that already has its ruleset and its CI, so the whole
loop is exercised before anything the owner cares about is at stake.
