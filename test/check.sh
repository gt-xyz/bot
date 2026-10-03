#!/usr/bin/env bash
# The whole verification story for bee. Runs with no network and no access to
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

bee="$build/bee"
[ -x "$bee" ] || { echo "no bee binary at $bee — build first" >&2; exit 1; }

"$build/test/unit"

echo "notes captured at 3am" >"$workspace/notes.txt"

for profile in cpp scratch none; do
  directory="$workspace/$profile"
  BEE_TEMPLATES="$root/templates" "$bee" scaffold "$directory" "demo-$profile" "$profile" \
    "a demo of the $profile profile" "$workspace/notes.txt"

  expect_file "$directory" AGENTS.md
  expect_file "$directory" .devcontainer/devcontainer.json
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

  # No file here is named after one vendor's agent. The instructions are the
  # same for every reader, so the filename should be too.
  expect_absent "$directory" CLAUDE.md
  jq -e . "$directory/.devcontainer/devcontainer.json" >/dev/null \
    || fail "$profile: devcontainer.json is not valid json"

  # The repository is public and must name no private infrastructure. A
  # hostname or account that reaches a scaffold came from a template.
  if grep -rlnE '@[a-z0-9.-]+:|[0-9]{1,3}(\.[0-9]{1,3}){3}' "$directory" | grep -q .; then
    fail "$profile: a scaffolded project names a host or address"
  fi
done

expect_file "$workspace/cpp" CMakeLists.txt
expect_file "$workspace/cpp" test/smoke.cpp
expect_contains "$workspace/cpp/.devcontainer/devcontainer.json" bee-cpp:latest
expect_file "$workspace/scratch" test/test_smoke.py
expect_contains "$workspace/scratch/.devcontainer/devcontainer.json" bee-scratch:latest
expect_absent "$workspace/none" test
expect_contains "$workspace/none/.devcontainer/devcontainer.json" bee-base:latest

BEE_TEMPLATES="$root/templates" "$bee" scaffold "$workspace/nonotes" nonotes none "no notes given"
expect_contains "$workspace/nonotes/docs/PROJECT.md" "Nothing captured beyond the description."

if BEE_TEMPLATES="$root/templates" "$bee" scaffold "$workspace/bogus" bogus rust "unknown" 2>/dev/null; then
  fail "an unknown profile was accepted"
fi

# Nothing tracked here may name a machine, an account or an address.
if git -C "$root" grep -nIE '[0-9]{1,3}(\.[0-9]{1,3}){3}' -- . >/dev/null 2>&1; then
  fail "a tracked file contains an IP address"
fi

# bee is subject to its own rules. A repository that scaffolds a README and a
# vendor-neutral agent file, and has neither, is not one anybody should copy.
expect_file "$root" README.md
expect_file "$root" AGENTS.md
expect_absent "$root" CLAUDE.md
expect_contains "$root/README.md" "docs/PROJECT.md"

# Exactly one, or "the one marked next" names nothing.
marked="$(grep -l '^next: true$' "$root"/work/*.md 2>/dev/null | wc -l)"
[ "$marked" -eq 1 ] || fail "work/: expected exactly one item marked next, found $marked"

if [ "$failures" -eq 0 ]; then
  echo "all checks passed"
else
  echo "$failures check(s) failed" >&2
  exit 1
fi
