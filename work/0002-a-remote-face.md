---
title: bee serve
---

`libbee` exists so that a second face costs almost nothing. `bee serve` answers
HTTP with server-rendered HTML: the projects, their state derived from git, and
each project's `work/`.

Server-rendered rather than a client application, because the client then
already exists on every device with nothing installed, and because HTML outlives
the toolkits that would otherwise need maintaining for as long as this project
lasts. No framework, no build step, no bundle to download.

Socket activation is the intended deployment: nothing runs until the first
connection, and it can exit when idle.

The reader is the point. Writing can stay in the command for now.
