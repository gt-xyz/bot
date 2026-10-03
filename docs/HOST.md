# Setting up a host

The host is one machine you own. It holds the bare repositories, runs the
sessions, and serves the web face to your other devices. A virtual machine is
a good host: it puts a second boundary between a session and whatever else the
hardware runs.

Nothing below has been run on a real host by the checks. `bot check` is how
you find out whether it holds on yours.

## What it needs

Debian 13 or anything as recent, with:

    git  cmake  ninja-build  g++  podman  curl

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

## A first session

    bot init <project>
    bot up <project>

`ctrl-p ctrl-q` detaches and leaves it running. `bot attach <session>` returns
to it, from any terminal that can ssh to the host. `bot stop <session>`
publishes its branch; from another machine the project is

    git clone <host>:git/<project>.git

## The web face

It reads; it does not act. `bot serve` answers one request on its standard
input, so a socket unit does the listening. As your user, in
`~/.config/systemd/user/`:

`bot.socket`

    [Socket]
    ListenStream=8807
    BindToDevice=lo
    Accept=yes

    [Install]
    WantedBy=sockets.target

`bot@.service`

    [Service]
    ExecStart=/usr/local/bin/bot serve
    StandardInput=socket
    StandardError=journal

then

    systemctl --user enable --now bot.socket

That listens on the host's loopback only. To reach it from your other devices,
let the tailnet's own serving layer forward to it, so it is never on the local
network or the internet:

    sudo tailscale serve --bg 8807

A session cannot reach it either: a session has no route to the host, and
`bot check` is what shows that.
