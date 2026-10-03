---
title: Prove containment on the real host
next: true
---

Sessions, their network and their proxy were written on a machine with no
container runtime. `test/check.sh` proves what bot asks the runtime for and
that `bot check` notices a runtime that enforces nothing. It cannot prove that
a real runtime enforces it. Nothing in `images/` has been built, and no session
has started for real.

So the next piece of work is not new code. Follow `docs/HOST.md` on the host:

- build the two images and fix what the build turns up
- run `bot check` with a tailnet peer and a local-network address, and fix
  whatever real podman does differently from what `src/libbot/session.cpp`
  assumes, until every line says `ok`
- start one attended session and find the names the agent needs in `allow`
- set up the socket unit and read the board from a phone

Done when `bot check` passes on the host and its output is kept with the
commit that says so.

What this does not do, and 0008 still asks for: a policy per project (0004)
rather than one `allow` for the host; a credential for a forge and pull
requests; `bot check` gating an unattended run (0009). The web face checks no
identity yet (0011), so it shows the board to anything on the tailnet.
