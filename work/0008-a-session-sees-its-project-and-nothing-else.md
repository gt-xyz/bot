---
title: A session sees its project and nothing else
---

`bee` mounted the owner's whole GitHub credential, the owner's whole
agent state and the host's whole network into every container, and nobody
noticed for months because nothing checked. This note is the frame for 0004 and
for everything after it: no session is dispatched (0009) and no action is reachable
from the web (0011) until `bot check` passes on the host.

**Threat model.** The threat is the session, not an outsider. A model can be wrong,
and a repository can carry text that steers it. So the question is never "is
the agent trustworthy" but "what can a session reach, touch and do if it is not".
Three boundaries, each one enforced by the container or the forge, never by the
agent's instructions, and each one tested.

**Reach: the network.** A session's container is on an internal network with no
route anywhere. What it may reach is granted by the project's egress policy
(0004) through a proxy that the session's environment points at and the container
cannot bypass:

- `local`: the inference host and the git remote, by name, nothing else
- `cloud`: the session's own provider and the forge, by name, nothing else;
  which provider is the agent's, per 0013
- `open`: the host's bridge, for throwaway work only, never the default

The tailnet range, the LAN ranges and the host itself are unreachable under
`local` and `cloud` by construction, not by a rule someone remembers. Git in a
`cloud` session uses HTTPS through the proxy, not ssh, so there is one path out.

**Touch: the filesystem.** A session gets its own clone, its own agent state
directory and a scratch directory, and nothing else from the host. No home
directory, no `~/.claude`, no `~/.claude.json`, no `~/.config/gh`, no socket to
the container runtime (that one is root on the host). The container runs
rootless, as an unprivileged user, with a read-only root filesystem, dropped
capabilities and process and memory limits. The image carries no secret.

**Do: the forge.** A session holds one credential for the forge: a fine-grained
token issued to the agent account, scoped to that one repository, contents and
pull requests only, mounted read-only into that session and no other. The agent
account has Write on experiment repositories and nothing on anything the owner
keeps for himself. `main` is protected everywhere the account can write: pull
request with the owner's approval, no force push, no squash. So the most a
misbehaving session can do is open a pull request on its own repository, and the
owner reads it.

The one credential that is not per-project is the agent's own: a provider key
or the hosted model's login, mounted only into sessions of that agent (0013).
It cannot be scoped to a repository; a key can carry a spending limit, and
nothing else in the session is worth stealing.

**Proof, not policy.** `test/check.sh` starts a session under each policy and tries
to connect to an address it must not reach; the build fails if it connects.
`bot check` repeats the live checks on the real host and refuses to let
`bot run` proceed while any fail: the runtime is rootless, no session mounts a
socket or a home directory, each mounted token answers for one repository only
when asked, the proxy allowlist matches the policy, and a probe from a running
`cloud` session to a tailnet address times out. Issue 14 was found by hand once. It
is found by `bot check` from now on.

**Which projects may be `cloud`.** Public experiment repositories. A project
holding personal data is `local` and `bot` refuses to start it any other way,
because the policy is a line in the project's configuration, not a choice at
launch.
