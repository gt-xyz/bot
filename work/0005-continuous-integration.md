---
title: Decide what runs the checks
---

No profile ships a CI configuration, deliberately: the previous one was written
for a hosting provider this project no longer depends on, and a workflow for a
forge that has not been chosen would be worse than none.

`./test/check.sh` is the whole verification story and runs anywhere, so nothing
is blocked. What is missing is something that runs it on a push without being
asked.

Whatever is chosen should be reachable from the repository's own host, should
cost nothing per minute, and should not be able to stop work when it is
unavailable.
