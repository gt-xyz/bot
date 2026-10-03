---
title: The board shows sessions
---

0003 derives each project's columns from its repository. Add the columns a
person juggling many projects actually looks for, still derived, never stored:

- sessions running on it, attended or not, and since when
- a pull request from the agent account is open, with its number
- the last session failed, from the exit of its log
- age of the last commit, so stale shows itself
- the project's egress policy, so a `cloud` session on the wrong project is
  visible at a glance

Status is the one declared field, because "shelved" is a decision and not a
fact derivable from a tree. It lives in `docs/PROJECT.md` as one word from a
fixed list: `in progress`, `shelved`, `complete`, `abandoned`. The board shows
the word and the date of the commit that last changed it, so a declaration that
has drifted is visible as old. The owner's site uses the same four words on its
cards, so one vocabulary covers both places.

Reading the board is file reads over the session directories, one call to the
container runtime, and one call to the forge for open pull requests, cached
briefly and skipped when offline.
