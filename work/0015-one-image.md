---
title: One image, and what a project may add to it
---

Decided with the owner on 2026-10-03: a profile no longer picks a container
image. Every session runs in `images/session`, and a profile is only the files
a new project starts with.

A contained session cannot install a system package: it is not root, its root
filesystem is read-only and it has no route to a mirror. That is the boundary
working. So a project names the tools and versions it wants in the `Tools`
section of its `docs/DEVELOPMENT.md`, and the owner adds them to the image.

Open, and nothing is built for any of it:

- Whether profiles should go entirely, leaving one scaffold and two questions.
- Whether a session may fetch from a package registry. It would be a name in
  `allow`, and a registry that accepts uploads is also a way out for whatever
  the session can read.
- Pinning. The image takes whatever versions its base has on the day it is
  built. A list of pinned versions would make two builds the same.
- Something shared between sessions, such as a download cache. Anything two
  sessions can both write is a channel between them, so it needs its own
  boundary and its own probe in `bot check` before it exists.
