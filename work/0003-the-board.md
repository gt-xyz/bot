---
title: bee ls
---

A board over every project, with each column derived rather than stored:
whether the only commit is still the scaffold, whether the tree is dirty,
whether anything is unpushed, and the title of whatever is marked `next`.

Reading `work/` is a file read, so this stays fast and works offline. An earlier
design fetched the equivalent from a hosting provider's API, which needed
parallel requests, timeouts and silent failure to stay usable at all; none of
that machinery is needed now and none of it should come back.
