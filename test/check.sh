#!/usr/bin/env bash
# The whole verification story for bot. Runs with no network and no access to
# any particular machine, so a third party can audit it by running it.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-$root/build}"
workspace="$(mktemp -d)"
trap 'rm -rf "$workspace"' EXIT

failures=0

fail() {
  echo "FAIL: $*" >&2
  failures=$((failures + 1))
}

expect_file() {
  [ -f "$1/$2" ] || fail "$(basename "$1"): missing $2"
}

expect_absent() {
  [ -e "$1/$2" ] && fail "$(basename "$1"): should not ship $2"
  return 0
}

expect_contains() {
  grep -qF "$2" "$1" || fail "$1: expected to contain '$2'"
}

expect_absent_text() {
  grep -qF -- "$2" <<<"$1" && fail "$3"
  return 0
}

expect_text() {
  grep -qF -- "$2" <<<"$1" || fail "$3"
}

bot="$build/bot"
[ -x "$bot" ] || { echo "no bot binary at $bot — build first" >&2; exit 1; }

"$build/test/unit"

echo "notes captured at 3am" >"$workspace/notes.txt"

for profile in cpp scratch none; do
  directory="$workspace/$profile"
  BOT_TEMPLATES="$root/templates" "$bot" scaffold "$directory" "demo-$profile" "$profile" \
    "a demo of the $profile profile" "$workspace/notes.txt"

  expect_file "$directory" AGENTS.md
  expect_file "$directory" docs/PROJECT.md
  expect_file "$directory" docs/DEVELOPMENT.md
  expect_file "$directory" README.md
  expect_file "$directory" work/0001-shape-v0.1.md

  if grep -rlF '{{' "$directory" | grep -q .; then
    fail "$profile: unsubstituted placeholders in $(grep -rlF '{{' "$directory" | tr '\n' ' ')"
  fi

  expect_contains "$directory/docs/PROJECT.md" "a demo of the $profile profile"
  expect_contains "$directory/docs/PROJECT.md" "notes captured at 3am"
  expect_contains "$directory/work/0001-shape-v0.1.md" "next: true"
  expect_contains "$directory/README.md" "docs/PROJECT.md"
  expect_contains "$directory/AGENTS.md" "next: true"

  # No file here is named after one vendor's agent, none mentions one, and
  # none mentions this tool: a project outlives both. And none tells a
  # container what to mount, which is how a session once got its owner's home.
  expect_absent "$directory" CLAUDE.md
  expect_absent "$directory" .devcontainer
  if grep -rliE 'claude|anthropic|\bbot\b' "$directory" | grep -q .; then
    fail "$profile: a scaffolded project names a vendor or this tool"
  fi

  # The repository is public and must name no private infrastructure. A
  # hostname or account that reaches a scaffold came from a template.
  if grep -rlnE '@[a-z0-9.-]+:|[0-9]{1,3}(\.[0-9]{1,3}){3}' "$directory" | grep -q .; then
    fail "$profile: a scaffolded project names a host or address"
  fi
done

expect_file "$workspace/cpp" CMakeLists.txt
expect_file "$workspace/cpp" test/smoke.cpp
expect_file "$workspace/scratch" test/test_smoke.py
expect_absent "$workspace/none" test

BOT_TEMPLATES="$root/templates" "$bot" scaffold "$workspace/nonotes" nonotes none "no notes given"
expect_contains "$workspace/nonotes/docs/PROJECT.md" "Nothing captured beyond the description."

if BOT_TEMPLATES="$root/templates" "$bot" scaffold "$workspace/bogus" bogus rust "unknown" 2>/dev/null; then
  fail "an unknown profile was accepted"
fi

# Sessions, against a container runtime that contains nothing. It records
# what it is asked and runs `exec` directly on this machine, unless it is told
# to pretend that it contains, when it answers every probe as held. Two things are
# proven with it and neither needs a real runtime: what bot asks for when it
# starts a session, and that `bot check` fails when what it asked for is not
# enforced. That the real runtime enforces it is proven by `bot check` there.
unset XDG_CONFIG_HOME HTTPS_PROXY HTTP_PROXY https_proxy http_proxy
export HOME="$workspace/home"
sessions="$workspace/sessions"
asked="$workspace/asked"
mkdir -p "$HOME/.config/bot"
git config --global user.name "check"
git config --global user.email "check@example.invalid"
git config --global init.defaultBranch main

cat >"$workspace/runtime" <<'RUNTIME'
#!/usr/bin/env bash
echo "$*" >>"$BOT_ASKED"
case "$1 ${2:-}" in
  "image exists") exit 0 ;;
  "network exists") exit 1 ;;
  "info --format") if [ -n "${BOT_CONTAINS:-}" ]; then echo true; else echo false; fi ;;
  "ps --all") if [ "$4" = "{{.Names}}" ]; then cut -d' ' -f1 "$BOT_RUNNING"; else cat "$BOT_RUNNING"; fi ;;
  "inspect --format") case "$3" in *Mounts*) [ -n "${BOT_CONTAINS:-}" ] || echo "$HOME" ;; *) echo proxy.invalid ;; esac ;;
  "exec "*) [ -z "${BOT_CONTAINS:-}" ] || exit 0; shift 2; HTTPS_PROXY=http://proxy.invalid:8888 exec "$@" ;;
esac
exit 0
RUNTIME
chmod +x "$workspace/runtime"
export BOT_ASKED="$asked" BOT_RUNNING="$workspace/running"
: >"$BOT_RUNNING"

cat >"$HOME/.config/bot/config" <<CONFIG
sessions    = $sessions
remote-root = $workspace/remotes
templates   = $root/templates
images      = $root/images
runtime     = $workspace/runtime
agent       = an-agent --flag
agent-env   = $HOME/.config/bot/agent.env
agent-home  = $HOME/.config/bot/agent-home
allow       = model.example
CONFIG
mkdir -p "$HOME/.config/bot/agent-home/.agent"
echo "seeded" >"$HOME/.config/bot/agent-home/.agent/settings"
echo "TOKEN=secret" >"$HOME/.config/bot/agent.env"

printf 'a demo project\n3\na note\n\n' | "$bot" init demo >/dev/null 2>&1 || fail "init: did not create a project"
remote="$workspace/remotes/demo.git"
git --git-dir "$remote" cat-file -e main:AGENTS.md 2>/dev/null || fail "init: the remote has no scaffold on main"
"$bot" init demo </dev/null >/dev/null 2>&1 && fail "init: an existing project was overwritten"
"$bot" init '../escape' </dev/null >/dev/null 2>&1 && fail "init: a path was accepted as a name"

"$bot" up demo >/dev/null 2>&1 || fail "up: did not start a session"
id="$(ls "$sessions" | head -n 1)"
session="$sessions/$id"
given="$(grep -E "^run --detach --name bot-$id " "$asked" || true)"
proxied="$(grep -E "^run --detach --name bot-$id-proxy " "$asked" || true)"
echo "bot-$id running" >"$BOT_RUNNING"

# What a session is given, all of it, on one command line.
[ -n "$given" ] || fail "up: no session container was started"
for flag in "--network=bot-$id " --userns=keep-id --read-only --cap-drop=all --security-opt=no-new-privileges \
            --pids-limit= --memory= "bot-session:latest an-agent --flag"; do
  expect_text "$given" "$flag" "up: a session is started without $flag"
done
[ "$(grep -o -- '--volume' <<<"$given" | wc -l)" -eq 2 ] || fail "up: a session is given other than two mounts"
expect_text "$given" "--volume $session/tree:/work:Z" "up: a session is not given its clone"
expect_text "$given" "--volume $session/home:/home/agent:Z" "up: a session is not given a home of its own"
expect_text "$given" "--env-file $HOME/.config/bot/agent.env" "up: the agent's credential is not passed"
expect_absent_text "${given//--env-file $HOME\/.config\/bot\/agent.env/}" "$HOME" "up: a session is given something from the owner's home"
for never in sock --privileged host --device --cap-add --publish; do
  expect_absent_text "$given" "$never" "up: a session's command line mentions $never"
done
expect_text "$(cat "$asked")" "network create --internal --disable-dns bot-$id" "up: a session's network is not internal and without a resolver"

# The agent's home starts as a copy of what its owner prepared, not a mount of it.
[ "$(cat "$session/home/.agent/settings" 2>/dev/null)" = "seeded" ] || fail "up: the session's home was not seeded"
[ -L "$session/home/.agent/settings" ] && fail "up: the session's home links back to the owner's"

# The proxy stands on both networks, mounts only its two files, read-only,
# and those files hold the allowed names and no others.
expect_text "$proxied" "--network=bot-$id,bot-egress" "up: the proxy is not on both networks"
expect_text "$proxied" "--volume $session/proxy:/etc/bot-proxy:ro,Z" "up: the proxy's files are not mounted read-only"
[ "$(grep -o -- '--volume' <<<"$proxied" | wc -l)" -eq 1 ] || fail "up: the proxy is given other than one mount"
[ "$(cat "$session/proxy/filter")" = '^model\.example$' ] || fail "up: the proxy's filter is not exactly the allowed names"
expect_contains "$session/proxy/tinyproxy.conf" "FilterDefaultDeny Yes"

# The clone is on its own branch and knows no remote.
[ "$(git -C "$session/tree" branch --show-current)" = "$id" ] || fail "up: the clone is not on a branch named after the session"
[ -z "$(git -C "$session/tree" remote)" ] || fail "up: the clone still knows where it came from"

listing="$("$bot" ls)"
expect_text "$listing" "Shape v0.1" "ls: the next piece of work is not shown"
expect_text "$listing" "$id" "ls: a session is not shown"
expect_text "$listing" "running" "ls: a session's state is not shown"

front="$(printf 'GET / HTTP/1.1\r\n\r\n' | "$bot" serve)"
expect_text "$front" "HTTP/1.0 200" "serve: the board is not served"
expect_text "$front" "a demo project" "serve: a project's description is not shown"
expect_text "$front" "$id" "serve: a session is not shown"
expect_text "$(printf 'GET /project/demo HTTP/1.1\r\n\r\n' | "$bot" serve)" "<b>next</b> Shape v0.1" "serve: a project's work is not shown"
expect_text "$(printf 'POST / HTTP/1.1\r\n\r\n' | "$bot" serve)" "HTTP/1.0 405" "serve: something other than a read was answered"

# `bot check` must fail on a runtime that contains nothing, and say what leaked.
if proof="$("$bot" check 2>/dev/null)"; then
  fail "check: passed on a runtime that enforces nothing"
fi
for leak in "the container runtime is rootless" "bot-$id mounts nothing outside its own directory" \
            "a session's root filesystem is read-only" "a session cannot see this tool's configuration" \
            "a session cannot see the directory that holds the other sessions" \
            "a session cannot see the file its agent's credential came from" \
            "a session cannot see the owner's home directory" "the proxy refuses a name that is not allowed"; do
  expect_text "$proof" "FAIL  $leak" "check: did not notice that $leak is false"
done
[ "$(ls "$sessions" | wc -l)" -eq 1 ] || fail "check: the probe session was left behind"

# Publishing. The session commits, and also does what a hostile one would:
# plants a hook and configuration that run commands for whoever uses git in
# its tree. The branch must arrive and none of it may run.
tree="$session/tree"
echo "work" >"$tree/done.txt"
git -C "$tree" add done.txt
git -C "$tree" commit -q -m "Work from the session"
printf '#!/bin/sh\ntouch "%s/hooked"\n' "$workspace" >"$tree/.git/hooks/pre-push"
chmod +x "$tree/.git/hooks/pre-push"
for key in core.fsmonitor core.sshCommand uploadpack.packObjectsHook core.pager; do
  git -C "$tree" config "$key" "touch $workspace/hooked"
done
before="$(git --git-dir "$remote" rev-parse main)"
"$bot" stop "$id" >/dev/null 2>&1 || fail "stop: did not publish"
[ "$(git --git-dir "$remote" log -1 --format=%s "$id" 2>/dev/null)" = "Work from the session" ] \
  || fail "stop: the session's branch did not reach the remote"
[ "$(git --git-dir "$remote" rev-parse main)" = "$before" ] || fail "stop: main was moved"
expect_absent "$workspace" hooked

"$bot" rm "$id" >/dev/null 2>&1 || fail "rm: failed"
[ -e "$session" ] && fail "rm: the session's directory is still there"
expect_text "$(cat "$asked")" "network rm bot-$id" "rm: the session's network was not removed"

# A session that swaps its repository for a link to another one publishes nothing.
sleep 1
"$bot" up demo >/dev/null 2>&1 || fail "up: did not start a second session"
second="$(ls "$sessions" | head -n 1)"
git init -q "$workspace/elsewhere"
git -C "$workspace/elsewhere" commit -q --allow-empty -m "Not this project's"
mv "$sessions/$second/tree/.git" "$workspace/set-aside"
ln -s "$workspace/elsewhere/.git" "$sessions/$second/tree/.git"
"$bot" stop "$second" >/dev/null 2>&1 && fail "stop: published from a repository the session was not given"
git --git-dir "$remote" rev-parse -q --verify "$second" >/dev/null && fail "stop: a branch arrived from elsewhere"

# A session that takes messages. It is not started where `bot check` fails,
# which on this stand-in runtime is everywhere unless it is told to pretend.
sleep 1
count="$(ls "$sessions" | wc -l)"
"$bot" run demo "do the thing" >/dev/null 2>&1 && fail "run: started a session on a machine that failed the check"
[ "$(ls "$sessions" | wc -l)" -eq "$count" ] || fail "run: a refused session was left behind"

export BOT_CONTAINS=1
talking="$("$bot" run demo "do the thing" 2>/dev/null)" || fail "run: did not start a session"
talk="$sessions/$talking"
given="$(grep -E "^run --detach --name bot-$talking " "$asked" || true)"
[ "$(cat "$talk/inbox/0001" 2>/dev/null)" = "do the thing" ] || fail "run: the first message is not in the inbox"
[ "$(grep -o -- '--volume' <<<"$given" | wc -l)" -eq 3 ] || fail "run: a session is given other than three mounts"
expect_text "$given" "--volume $talk/inbox:/inbox:ro,Z" "run: the inbox is not mounted read-only"
expect_text "$given" "bot-session:latest agent-loop" "run: the session does not run the adapter"
expect_absent_text "$given" "--tty" "run: a session that takes messages was given a terminal"
"$bot" say demo "and then this" 2>/dev/null || fail "say: a project's name did not reach its newest session"
[ "$(cat "$talk/inbox/0002" 2>/dev/null)" = "and then this" ] || fail "say: the second message is not in the inbox"
"$bot" say "$second" "hello" 2>/dev/null && fail "say: a terminal session accepted a message"

# The adapter, for real, with a stand-in for the agent behind it. Its output
# becomes the session's log in the form the runtime writes, and is read back.
command -v jq >/dev/null || fail "jq is needed to run the adapter"
mkdir -p "$workspace/agent"
cat >"$workspace/agent/claude" <<'AGENT'
#!/bin/sh
echo "$*" >>"$AGENT_ASKED"
echo '{"type":"system","subtype":"init"}'
echo '{"type":"assistant","message":{"content":[{"type":"text","text":"Looking at <the> files."}]}}'
echo '{"type":"assistant","message":{"content":[{"type":"tool_use","name":"Shell","input":{"command":"ls"}}]}}'
echo 'something that is not an event'
echo '{"type":"result","subtype":"success","is_error":false}'
AGENT
chmod +x "$workspace/agent/claude"
PATH="$workspace/agent:$PATH" BOT_INBOX="$talk/inbox" AGENT_ASKED="$workspace/agent/asked" \
  timeout 2 "$root/images/session/agent-loop" >"$workspace/agent/said" || true
sed 's/^/2026-01-01T00:00:00Z stdout F /' "$workspace/agent/said" >"$talk/log"
[ "$(grep -c -- '--continue' "$workspace/agent/asked")" -eq 1 ] || fail "adapter: only the turns after the first continue the conversation"
echo "bot-$talking running" >"$BOT_RUNNING"

# The web face with an owner. Who is asking is the address the request came
# from, as the network reports it; everything else about a request is claimed.
cat >"$workspace/whois" <<'WHOIS'
#!/bin/sh
[ "$1" = "fd00::1" ] && echo owner@example.invalid || echo someone@example.invalid
WHOIS
chmod +x "$workspace/whois"
printf 'owner = owner@example.invalid\nwhois = %s\n' "$workspace/whois" >>"$HOME/.config/bot/config"

get() {
  printf 'GET %s HTTP/1.1\r\nHost: [fd00::2]:8807\r\n\r\n' "$2" | REMOTE_ADDR="$1" "$bot" serve
}
post() {
  printf 'POST %s HTTP/1.1\r\nHost: [fd00::2]:8807\r\nOrigin: %s\r\nContent-Length: %d\r\n\r\n%s' \
    "$2" "${4:-http://[fd00::2]:8807}" "${#3}" "$3" | REMOTE_ADDR="$1" "$bot" serve
}

expect_text "$(get fd00::9 /)" "HTTP/1.0 403" "serve: someone who is not the owner could read"
expect_text "$(get fd00::1 /)" 'action="/init"' "serve: the owner is not offered a new project"

said="$(get fd00::1 "/session/$talking")"
expect_text "$said" "do the thing" "serve: the owner's message is not in the transcript"
expect_text "$said" "Looking at &lt;the&gt; files." "serve: the agent's words are not in the transcript, escaped"
expect_text "$said" 'Shell {&quot;command&quot;:&quot;ls&quot;}' "serve: a tool call is not in the transcript"
expect_text "$said" "something that is not an event" "serve: output that is not an event was lost"
expect_text "$said" "action=\"/session/$talking/say\"" "serve: an idle session has no form for the next message"
expect_absent_text "$said" "http-equiv=refresh" "serve: an idle session's page reloads under whoever is typing"

expect_text "$(post fd00::9 "/session/$talking/say" "message=no")" "HTTP/1.0 403" "serve: someone else sent a message"
expect_text "$(post fd00::1 "/session/$talking/say" "message=no" "http://elsewhere.example")" "HTTP/1.0 403" \
  "serve: a form from another site's page was acted on"
[ -e "$talk/inbox/0003" ] && fail "serve: a refused message reached the inbox"
expect_text "$(post fd00::1 "/session/$talking/say" "message=once+more%21")" "Location: /session/$talking" "serve: the owner's message was not taken"
[ "$(cat "$talk/inbox/0003" 2>/dev/null)" = "once more!" ] || fail "serve: the owner's message is not in the inbox as written"

working="$(get fd00::1 "/session/$talking")"
expect_text "$working" "http-equiv=refresh" "serve: a working session's page does not reload"
expect_absent_text "$working" "<textarea" "serve: a working session's page has a form a reload would empty"
expect_text "$(get fd00::1 "/session/$talking?write")" "<textarea" "serve: there is no way to write to a working session"

started="$(post fd00::1 /project/demo/run "message=from+the+web")"
expect_text "$started" "303 See Other" "serve: the owner could not start a session"
[ "$(cat "$sessions"/demo-*/inbox/0001 | grep -c 'from the web')" -eq 1 ] || fail "serve: the session started from the web has no first message"
expect_text "$(post fd00::1 "/session/$talking/stop" "")" "nothing committed beyond main" "serve: stopping did not say what was published"
expect_text "$(post fd00::1 /init "name=fromweb&description=made+from+the+web&profile=none")" "Location: /project/fromweb" \
  "serve: the owner could not create a project"
git --git-dir "$workspace/remotes/fromweb.git" cat-file -e main:AGENTS.md 2>/dev/null || fail "serve: the project made from the web has no scaffold"
unset BOT_CONTAINS

# Nothing tracked here may name a machine, an account or an address.
if git -C "$root" grep -nIE '[0-9]{1,3}(\.[0-9]{1,3}){3}' -- . >/dev/null 2>&1; then
  fail "a tracked file contains an IP address"
fi

# bot is subject to its own rules. A repository that scaffolds a README and a
# vendor-neutral agent file, and has neither, is not one anybody should copy.
expect_file "$root" README.md
expect_file "$root" AGENTS.md
expect_absent "$root" CLAUDE.md
expect_contains "$root/README.md" "docs/PROJECT.md"

# Scope is a budget. Raise it only in a commit that says why.
budget=2100
lines="$(cat "$root"/src/libbot/* "$root"/src/bot/* | wc -l)"
[ "$lines" -le "$budget" ] || fail "the library and the command are $lines lines, over the budget of $budget"

# Exactly one, or "the one marked next" names nothing.
marked="$(grep -l '^next: true$' "$root"/work/*.md 2>/dev/null | wc -l)"
[ "$marked" -eq 1 ] || fail "work/: expected exactly one item marked next, found $marked"

if [ "$failures" -eq 0 ]; then
  echo "all checks passed"
else
  echo "$failures check(s) failed" >&2
  exit 1
fi
