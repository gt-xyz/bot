---
title: Starting and attaching a session
next: true
---

`bee new` scaffolds, commits and pushes, then stops. A project is not yet a
place you can work: there is no container, no session, and no agent.

This is the gap that makes bee not yet a replacement for starting things by
hand, so it comes before anything else.

- `bee up <project>` — build the profile image if absent, start the container,
  open a session, launch whichever agent is configured
- `bee down <project>` — stop the session and the container, leave the tree
- `bee rm <project>` — remove the working copy, leave the remote alone and say
  so

Which agent runs is configuration, like everything else machine-specific. bee
carries the choice; it does not provide the agent.
