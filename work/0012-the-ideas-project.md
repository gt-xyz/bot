---
title: The ideas project
---

A graveyard forms when ideas are cheap to capture and expensive to place.
`bot init` makes capture cheap on purpose; this note makes placing cheap too,
without a model inside `bot init`.

`ideas` is an ordinary project with the `none` profile. Each idea is one short
note in its `work/`, written in a minute, with no decision attached. Nothing
else is special about it.

Placing is an unattended session (0009) on the ideas project. It reads the board and
every project's `docs/PROJECT.md`, and for each unplaced idea proposes one of:

- a new project, with the three answers `bot init` would ask for
- part of an existing project, as a draft work note for that project
- not now, with one sentence of why

The proposal is a pull request on the ideas repository, so it arrives through
review like everything else. Accepting a "new project" line is still the
owner running `bot init`; accepting "part of X" is copying one file into X's
`work/`. The session never creates projects and never writes to other projects,
which keeps it inside 0008 without a special case.

What this does and does not fix. It fixes ideas that die because nobody looked
at them next to the projects that exist. It does not fix projects that die
because the owner stopped caring, and it should not try; for those the board's
age column and the `shelved` status are the honest tools. Say so in the
project's doc so nobody expects an agent to curate taste.

Depends on 0009 and 0010.
