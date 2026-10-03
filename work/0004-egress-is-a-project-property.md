---
title: Egress should be a property of the project, enforced by the container
---

A project should be able to promise that its source reaches nothing but the
model it is talking to, and a project that must stay local should be unable to
reach anything at all.

Today a container inherits the host's network and mounts credentials that reach
far past the one project, so neither promise holds.

- `local` — no route off the machine or its private network
- `cloud` — the model provider and nothing else
- `open` — unrestricted, for throwaway work

The reason to make this a property the container enforces, rather than two
separate tools a person chooses between, is that a person choosing correctly
every time is not a guarantee. `test/check.sh` can assert the real thing: start
a container under `cloud`, try to open an address that should be unreachable,
and fail if it connects.

Per-project agent state and credentials scoped to the one repository belong with
this, since a network boundary around shared credentials is only half a
boundary.
