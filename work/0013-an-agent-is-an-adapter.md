---
title: An agent is an adapter
---

`bee` v1 was a hosted model's CLI plus a forge, and everything else followed
from those two. bot keeps the forge and drops the dependency on any one agent.
A session may run a different program than the session next to it, and the
tool knows nothing about any of them beyond one contract.

**The contract.** Five operations, and an agent is anything wrapped to meet
them:

- start, with a first message and a working directory
- send a message
- read events: text, a tool call and its result, a permission request, the end
- answer a permission request
- stop

The transcript on the web face, the message form, the log, the budgets and the
board are written once against these five. An agent is a container image plus
an adapter. Adding one is adding a directory.

**The first adapter is the hosted model's own CLI**, because the owner's
subscription is usable only through it. Its headless mode is one command per
turn with streamed structured output, resumed by session id, and it has a
documented hook that routes permission prompts to a tool the server answers
from the web form. Every byte of that is the vendor's product being used as
sold, which is what keeps the subscription legitimate. Its remote chat (0011)
stays available in attended sessions for anyone who wants the vendor's app.

**Other adapters use keys.** A key-based provider, and a model served on the
fleet, run through an open harness that exposes a structured mode. Evaluate
the existing protocol for agent clients before writing a loop: several
harnesses already speak it, and a thin loop is a weak coding agent however
small it is. The agent SDKs that vendors publish are a fallback for the same
contract, with the terms checked first, since at least one vendor ties its SDK
to API keys rather than subscriptions.

**Credentials follow the agent, not the project.** A login or key is mounted
only into sessions whose agent needs it: the hosted model's login into its
sessions, a provider's key into that provider's sessions, the forge token into
all of them and only for the one repository. The `cloud` policy in 0008 is
therefore parameterised by provider: the allowlist is that provider's domain
and the forge, so a session on one provider cannot reach another.

**What is deliberately not here.** No routing of a message to "the best
model", no fallback from one provider to another mid-session, no shared memory
across agents. A session is one agent from start to end. Choosing is a line in
the start form.

Depends on 0001 and 0008. 0009 and 0011 are written against the contract.
