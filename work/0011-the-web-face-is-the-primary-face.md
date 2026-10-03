---
title: bot serve is the primary face
---

0002 makes the web face a reader and keeps writing in the command. Reverse the
emphasis: the web face is how the tool is normally used, from any device on
the tailnet, and the command is the same operations for scripts, for a
terminal that is already open, and for the day the server is down. Both link
`libbot`; neither has logic the other lacks.

Four pages, server-rendered, no JavaScript:

- **Projects.** Every project with status, policy, running sessions, open pull
  requests and age. A form to create one: name, one line, profile, policy. The
  same three answers `bot init` asks, no more.
- **Project.** Its description, its work queue with the one marked next, its
  sessions past and present, and a form to start one: which agent, and either
  a first message or the next piece of work.
- **Session.** State, the transcript on a page that refreshes itself while
  the session runs, a form for the next message, a stop button, the branch and
  the pull request link.
- **Ideas**, once 0012 exists: the unplaced notes and the latest proposal.

**Interacting with a session is a conversation, not a terminal.** A session
speaks events: the agent's messages, tool calls, questions, and the end. The
web face renders the transcript on the session page and has one form, the next
message. An approval is a message. A correction mid-run is a message. Stop is a
button. While the session runs the page refreshes itself every few seconds,
which is enough for a conversation whose turns take longer than that. No
JavaScript, and it works from a phone.

This is what makes the agent swappable: the five operations in 0013 are
the whole interface between the web face and whatever runs inside the session.

**The hosted model's own remote chat is the stopgap, and it fits.** The
hosted CLI can hand a running session to its vendor's app: a transcript on a
phone, a message box, a notice when work finishes. It relays only through the
vendor, which is the one place a `cloud` session may reach anyway, so an
attended session can start with it switched on and the session page can say
"open in the app". Nothing to build. What it costs: the control path leaves
the tailnet, the container holds a login to the owner's account rather than a
scoped key, and it is one vendor only. It is the right first step and the
wrong last one.

**The self-hosted relay is this note, over the contract in 0013.** The
hosted model's CLI is driven in its headless mode, one turn per command,
resumed by session id, with its permission hook answered by the web form. That
is the vendor's product used as sold, so the subscription applies. Other
agents meet the same five operations through their own structured modes.

**"Tell me when it is done" needs a push channel.** A page nobody is looking
at cannot notify. Run an existing self-hosted notification relay beside the
server, one HTTP request per event, with its phone app; never write one.

The agent's own terminal interface is still there for anyone who wants it:
`bot attach` opens it in a terminal over ssh, and the session page may link to
a web terminal that is an existing tool run beside the server, never written
here. The management face itself stays plain HTML.

Where it listens is the whole security story for this face:

- The server binds only to the tailnet interface. Never the LAN, never the
  internet, never a session's container network. A session that could reach
  the face could start other sessions, which is exactly the privilege 0008
  removes.
- Served through the tailnet's own serving layer, so every request arrives
  with an identity the server did not have to implement. The server refuses
  anything that is not the owner. Single-user, with no login page to build.
- Actions are `POST` only, with a token in the form that the server issued,
  so a link somewhere cannot trigger one. Reads are `GET`.
- Socket activation as in 0002, so nothing listens until asked.

Same stylesheet discipline and budgets as the owner's site. One stylesheet,
system fonts, readable at 320 px.

Depends on 0008, because a face that can start sessions must not exist before
sessions are contained, and on 0009 for `run`.
