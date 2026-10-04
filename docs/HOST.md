# Setting up a host

The host is one machine you own. It holds the bare repositories, runs the
sessions, and serves the web face to your other devices. A virtual machine is
a good host: it puts a second boundary between a session and whatever else the
hardware runs.

Nothing below has been run on a real host by the checks. `bot check` is how
you find out whether it holds on yours.

## What it needs

Debian 13 or anything as recent, with:

    git  cmake  ninja-build  g++  podman  curl  jq

and a user that is not root to own everything. Sessions belong to that user,
and must outlive its logins:

    sudo loginctl enable-linger "$USER"

## Install

    git clone <this repository> && cd bot
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build && ./test/check.sh build
    sudo cmake --install build

## Configure

`~/.config/bot/config`:

    sessions    = ~/sessions
    remote-root = ~/git
    agent       = <the agent's command>
    agent-env   = ~/.config/bot/agent.env
    allow       = <the one name the agent's provider is reached at>

`agent-env` holds the agent's credential as `NAME=value` lines and should be
readable by you alone. It is passed to a session's environment and to nothing
else. Git on the host needs a `user.name` and `user.email`; a session commits
as them.

A session's home is new each time, so an agent that asks questions on its
first start asks them in every session. `agent-home` names a directory whose
contents are copied into each session's home before it starts: put the agent's
settings there, answered once. It is copied, not mounted, and a session can
read all of it, so it is no place for anything a session should not have.

`allow` is everything a session can reach. Start with the one name the agent
needs. If the agent then fails to connect, the proxy's own log names what it
refused: `podman logs bot-<session>-proxy`.

## Prove it

    bot check <a tailnet peer's address> <an address on your local network>

The first run builds the two images, which takes some minutes. It then starts
a session that runs nothing and tries, from inside it: reading your home, the
configuration, the credential file and the other sessions; writing the root
filesystem; resolving a name; connecting to every address this machine has
and to the ones you gave; and asking the proxy for those addresses and for a
name that is not allowed. Every line must say `ok`. It exits non-zero
otherwise, and says which boundary did not hold.

Do not start a session on a host where this fails. Run it again after changing
the config, the runtime or the network the host is on.

## Sessions

    bot init <project>
    bot run <project> "what it should do"
    bot say <project> "and then this"
    bot stop <project>

A project's name stands for its newest session. `bot run` proves containment
again before it starts anything, so put the addresses you gave `bot check` in
the config as `probe`. `bot stop` publishes the session's branch; from another
machine the project is

    git clone <host>:git/<project>.git

`bot up <project>` is the same session with the agent's own terminal interface
instead of messages. `ctrl-p ctrl-q` detaches from it and `bot attach` returns.

## The web face

`bot serve` answers one request on its standard input, so a socket unit does
the listening. It should listen on the tailnet and nowhere else. As your user,
in `~/.config/systemd/user/`:

`bot.socket`

    [Socket]
    ListenStream=<this machine's tailnet address>:8807
    FreeBind=yes
    Accept=yes

    [Install]
    WantedBy=sockets.target

`bot@.service`

    [Service]
    ExecStart=/usr/local/bin/bot serve
    StandardInput=socket
    StandardError=journal
    KillMode=process

then

    systemctl --user enable --now bot.socket

Until it is told who owns it, it only reads, for anyone who can reach it. To
make it yours, and able to start sessions and send messages, give the config
your name on the tailnet and a command that prints the name of whoever is at
an address:

    owner = <your login on the tailnet>
    whois = <the full path of a script that prints the login at the address given>

The web face then refuses every request that does not come from one of your
devices. A session cannot reach it either way: a session has no route to the
host, and `bot check` is what shows that.
