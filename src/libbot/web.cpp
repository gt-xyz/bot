#include "libbot/web.hpp"

#include "libbot/command.hpp"
#include "libbot/project.hpp"
#include "libbot/session.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <format>
#include <fstream>
#include <map>
#include <ranges>
#include <thread>

#include <unistd.h>

namespace bot {
namespace {

constexpr auto stylesheet = std::string_view{
    ":root{color-scheme:light dark;--line:#8883;--soft:#8882;--accent:#4c7cf0}*{box-sizing:border-box}"
    "body{margin:0;font:14px/1.45 system-ui,sans-serif}a{color:inherit;text-decoration:none}"
    ".app{display:grid;grid-template-columns:18rem minmax(0,1fr);min-height:100dvh}"
    "nav{border-right:1px solid var(--line);padding:.5rem;position:sticky;top:0;height:100dvh;overflow:auto}"
    "main{display:flex;flex-direction:column;min-height:100dvh;width:100%;max-width:54rem;margin:0 auto}"
    ".brand{display:block;font-weight:700;padding:.3rem .5rem .6rem}"
    ".proj{display:flex;justify-content:space-between;font-weight:600;padding:.5rem .5rem .2rem;margin-top:.3rem}"
    ".proj a:last-child{opacity:.5;font-weight:400}"
    ".s{display:flex;gap:.5rem;align-items:baseline;padding:.25rem .5rem;border-radius:6px}"
    ".s:hover,.here{background:var(--soft)}.s span{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
    "small,.muted{opacity:.6;font-size:.85em}.s small{white-space:nowrap}"
    ".dot{width:.5rem;height:.5rem;border-radius:50%;background:#8886;flex:none;align-self:center}"
    ".dot.working,.dot.starting,.pill.working,.pill.starting{background:var(--accent)}"
    ".dot.waiting,.pill.waiting{background:#e3a72f}.pill.working,.pill.starting{color:#fff}.pill.waiting{color:#000}"
    ".pill{font-size:.75rem;padding:.05rem .5rem;border-radius:99px;background:var(--soft);white-space:nowrap}"
    "summary{cursor:pointer;padding:.25rem .5rem;opacity:.6;font-size:.85em}"
    "header{position:sticky;top:0;z-index:1;display:flex;gap:.7rem;align-items:center;padding:.5rem 1rem;"
    "background:Canvas;border-bottom:1px solid var(--line)}header>div{flex:1;min-width:0}"
    "header b{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}header small{opacity:.85}"
    ".back{display:none;font-size:1.4rem;padding:0 .3rem}section{padding:.6rem 1rem}h2{font-size:.9rem;margin:.8rem 0 .3rem}"
    "#log{flex:1;padding:.2rem 1rem 1rem}.you,.agent,.tool,.error{white-space:pre-wrap;overflow-wrap:anywhere}"
    ".you{background:var(--soft);border-radius:10px;padding:.45rem .7rem;margin:1rem 0 .6rem 15%}"
    ".agent{margin:.6rem 0}.error{color:#e05a50;margin:.6rem 0}.steps{margin:.3rem 0}.steps>summary{padding:0}"
    ".tool{font:12px/1.5 ui-monospace,monospace;opacity:.65;padding-left:1rem}"
    ".work{display:flex;gap:.5rem;padding:.3rem 0;border-top:1px solid var(--line)}.work span{flex:1}"
    ".compose{position:sticky;bottom:0;display:flex;gap:.5rem;align-items:flex-end;padding:.6rem 1rem;"
    "background:Canvas;border-top:1px solid var(--line)}.stack{display:block;border:0;position:static}"
    "textarea,input,select{font:inherit;width:100%;padding:.5rem .6rem;margin:.2rem 0;border:1px solid var(--line);"
    "border-radius:8px;background:Canvas;color:CanvasText;resize:vertical}.compose textarea{margin:0}"
    "button{font:inherit;font-weight:600;padding:.5rem 1rem;border:0;border-radius:8px;background:var(--accent);"
    "color:#fff;cursor:pointer}button.quiet{background:var(--soft);color:inherit;font-weight:400}"
    "code{font:.85em ui-monospace,monospace;overflow-wrap:anywhere}"
    "@media(max-width:760px){.app{display:block}nav{position:static;height:auto;border:0}"
    "main{min-height:0}.detail nav{display:none}.detail main{min-height:100dvh}.detail .back{display:block}"
    ".you{margin-left:8%}body{font-size:15px}}"};

// The one script. It keeps a session's page in step with the session, so the
// page never has to be loaded again: new events are appended as they happen,
// and a message is sent without leaving. Without it the pages still work, by
// reloading. It talks only to this server, which the policy header enforces.
constexpr auto script = std::string_view{R"js(
const log = document.getElementById('log'), pill = document.getElementById('pill'), form = document.getElementById('say');
const atEnd = () => innerHeight + scrollY >= document.body.offsetHeight - 120;
const toEnd = () => scrollTo(0, document.body.scrollHeight);
const stream = new EventSource(location.pathname + '/stream?after=' + log.dataset.count);
stream.addEventListener('add', event => {
  const follow = atEnd(), holder = document.createElement('template'), last = log.lastElementChild;
  holder.innerHTML = event.data;
  const item = holder.content.firstChild;
  if (item.className === 'tool') {
    let steps = last && last.className === 'steps' ? last : null;
    if (!steps) {
      steps = log.appendChild(document.createElement('details'));
      steps.className = 'steps';
      steps.open = true;
      steps.appendChild(document.createElement('summary'));
    }
    steps.appendChild(item);
    const count = steps.children.length - 1;
    steps.firstChild.textContent = count + (count === 1 ? ' step' : ' steps');
  } else {
    if (last && last.className === 'steps') last.open = false;
    log.appendChild(item);
  }
  if (follow) toEnd();
});
stream.addEventListener('status', event => {
  pill.textContent = event.data;
  pill.className = 'pill ' + event.data.split(' ')[0];
  if (event.data === 'stopped') { stream.close(); location.reload(); }
});
if (form) {
  const box = form.message;
  const send = () => {
    const text = box.value.trim();
    if (!text) return;
    box.value = '';
    fetch(form.action, { method: 'POST', redirect: 'manual', body: new URLSearchParams({ message: text }) });
  };
  form.addEventListener('submit', event => { event.preventDefault(); send(); });
  box.addEventListener('keydown', event => {
    if (event.key === 'Enter' && !event.shiftKey && !matchMedia('(pointer:coarse)').matches) { event.preventDefault(); send(); }
  });
}
toEnd();
)js"};

struct Request {
    std::string method;
    std::string path;
    std::string query;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> form;
};

auto decoded(std::string_view text) -> std::string
{
    auto result = std::string{};
    for (auto index = std::size_t{0}; index < text.size(); ++index) {
        if (text[index] == '%' && index + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[index + 1])) != 0
            && std::isxdigit(static_cast<unsigned char>(text[index + 2])) != 0) {
            result += static_cast<char>(std::stoi(std::string{text.substr(index + 1, 2)}, nullptr, 16));
            index += 2;
        } else {
            result += text[index] == '+' ? ' ' : text[index];
        }
    }
    return result;
}

auto parsed(std::string_view raw) -> Request
{
    auto request = Request{};
    auto const headEnd = std::min(raw.find("\r\n\r\n"), raw.size());
    auto const line = raw.substr(0, raw.find("\r\n"));
    auto const methodEnd = line.find(' ');
    auto const targetEnd = line.rfind(' ');
    if (methodEnd == std::string_view::npos || targetEnd <= methodEnd) {
        return request;
    }
    request.method = line.substr(0, methodEnd);
    auto const target = line.substr(methodEnd + 1, targetEnd - methodEnd - 1);
    request.path = target.substr(0, target.find('?'));
    request.query = target.find('?') == std::string_view::npos ? "" : target.substr(target.find('?') + 1);

    for (auto position = line.size() + 2; position < headEnd;) {
        auto const end = std::min(raw.find("\r\n", position), headEnd);
        auto const header = raw.substr(position, end - position);
        position = end + 2;
        if (auto const colon = header.find(':'); colon != std::string_view::npos) {
            auto name = std::string{header.substr(0, colon)};
            std::ranges::transform(name, name.begin(), [](unsigned char character) { return std::tolower(character); });
            auto const value = header.substr(std::min(header.find_first_not_of(' ', colon + 1), header.size()));
            request.headers[name] = value;
        }
    }

    auto const body = headEnd + 4 <= raw.size() ? raw.substr(headEnd + 4) : std::string_view{};
    for (auto position = std::size_t{0}; position < body.size();) {
        auto const end = std::min(body.find('&', position), body.size());
        auto const pair = body.substr(position, end - position);
        position = end + 1;
        if (auto const equals = pair.find('='); equals != std::string_view::npos) {
            request.form[decoded(pair.substr(0, equals))] = decoded(pair.substr(equals + 1));
        }
    }
    return request;
}

// A different value for every response, which is one per process. The policy
// header names it, and the browser then runs the script and the stylesheet
// that carry it and nothing else, whatever else ends up in a page.
auto nonce() -> std::string const&
{
    static auto const value = [] {
        auto bytes = std::array<unsigned char, 12>{};
        std::ifstream{"/dev/urandom", std::ios::binary}.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        auto text = std::string{};
        for (auto const byte : bytes) {
            text += std::format("{:02x}", byte);
        }
        return text;
    }();
    return value;
}

auto response(std::string_view status, std::string_view body, std::string_view extra = {}) -> std::string
{
    return std::format("HTTP/1.0 {}\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: {}\r\nCache-Control: no-store\r\n"
                       "Content-Security-Policy: default-src 'none'; style-src 'nonce-{}'; script-src 'nonce-{}'; "
                       "connect-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'\r\n"
                       "{}Connection: close\r\n\r\n{}",
                       status, body.size(), nonce(), nonce(), extra, body);
}

auto see(std::string_view where) -> std::string
{
    return response("303 See Other", "", std::format("Location: {}\r\n", where));
}

auto status_words(std::string_view status) -> std::string_view
{
    return status == "waiting" ? "waiting for you" : status == "terminal" ? "in a terminal" : status;
}

// The list at the side: every project, and under it its sessions, newest
// first, by what each was asked and what it is doing now.
auto side(Config const& config, std::vector<Session> const& all, std::string_view here) -> std::string
{
    auto const names = project_names(config);
    auto list = std::string{"<nav><a class=brand href=\"/\">bot</a>"};
    for (auto const& name : names ? *names : std::vector<std::string>{}) {
        list += std::format("<div class=proj><a href=\"/project/{0}\">{0}</a><a href=\"/project/{0}\">+ new</a></div>", escaped(name));
        auto stopped = std::string{};
        auto count = 0;
        for (auto const& session : all | std::views::reverse) {
            if (session.project != name) {
                continue;
            }
            auto const row = std::format("<a class=\"s{}\" href=\"/session/{}\"><i class=\"dot {}\"></i><span>{}</span><small>{}</small></a>",
                                         session.id == here ? " here" : "", escaped(session.id), session.status,
                                         escaped(session.title), session.age);
            (session.status == "stopped" ? stopped : list) += row;
            count += session.status == "stopped" ? 1 : 0;
        }
        list += count == 0 ? "" : std::format("<details{}><summary>{} stopped</summary>{}</details>",
                                              stopped.contains(" here") ? " open" : "", count, stopped);
    }
    return list + (names ? "" : "<p class=muted>" + escaped(names.error()) + "</p>") + "</nav>";
}

auto page(Config const& config, std::string_view title, std::string_view main, std::string_view here = {}, std::string_view head = {})
    -> std::string
{
    return std::format("<!doctype html><html lang=en><meta charset=utf-8>"
                       "<meta name=viewport content=\"width=device-width,initial-scale=1\">{}<title>{}</title>"
                       "<style nonce={}>{}</style><body class=\"app{}\">{}<main>{}</main></html>",
                       head, escaped(title), nonce(), stylesheet, title == "bot" ? "" : " detail", side(config, sessions(config), here), main);
}

auto top(std::string_view title, std::string_view under, std::string_view right = {}) -> std::string
{
    return std::format("<header><a class=back href=\"/\">&lsaquo;</a><div><b>{}</b><small>{}</small></div>{}</header>", escaped(title),
                       under, right);
}

auto refused(Config const& config, std::string_view status, std::string_view why) -> std::string
{
    return response(status, page(config, "Not done", top("That did not happen", "") + std::format("<section class=error>{}</section>", escaped(why))));
}

// One event as it appears in a conversation. A line break is written as its
// character reference, so that an item is one line wherever it is sent.
auto item(Event const& event) -> std::string
{
    if (event.kind != "you" && event.kind != "agent" && event.kind != "tool" && event.kind != "error") {
        return {};
    }
    auto text = escaped(event.text);
    for (auto position = text.find('\n'); position != std::string::npos; position = text.find('\n', position)) {
        text.replace(position, 1, "&#10;");
    }
    return std::format("<div class={}>{}</div>", event.kind, text);
}

auto board(Config const& config, bool const acts) -> std::string
{
    auto main = std::string{};
    if (acts) {
        main += "<section><h2>New project</h2><form method=post action=\"/init\">"
                "<input name=name placeholder=\"Name: letters, digits, dashes\" required pattern=\"[A-Za-z0-9][A-Za-z0-9_-]*\">"
                "<input name=description placeholder=\"One line: what is it?\" required><select name=profile>";
        for (auto const profile : profiles()) {
            main += std::format("<option value=\"{}\">{}</option>", profile, escaped(profile_summary(profile)));
        }
        main += "</select><textarea name=notes rows=3 placeholder=\"Anything else on your mind\"></textarea>"
                "<button>Create</button></form></section>";
    }
    return page(config, "bot", main);
}

auto project_page(Config const& config, std::string const& name, bool const acts) -> std::string
{
    auto const project = describe(config, name);
    auto main = top(name, escaped(project.description));
    if (acts) {
        main += std::format("<form class=\"compose stack\" method=post action=\"/project/{}/run\"><textarea name=message rows=3 "
                            "placeholder=\"What should a new session do? Leave this empty and it does the next piece of work: {}\">"
                            "</textarea><button>Start a session</button></form>",
                            escaped(name), escaped(project.next_title()));
    }
    main += "<section><h2>Work</h2>";
    for (auto const& work : project.work) {
        main += std::format("<div class=work><span>{}{}</span><small>{}</small></div>", work.next ? "<b>next</b> " : "",
                            escaped(work.title), escaped(work.file));
    }
    return page(config, name, main + "</section>");
}

// The conversation. What was said is shown; what was done on the way is
// folded into a count, open only for the steps under way. The script keeps
// it current; without the script the page reloads itself while there is work.
auto session_page(Config const& config, Session const& session, bool const acts) -> std::string
{
    auto const stopped = session.status == "stopped";
    auto const busy = session.status == "working" || session.status == "starting";
    auto const address = "/session/" + escaped(session.id);
    auto const events = takes_messages(config, session.id) ? transcript(config, session.id).events : std::vector<Event>{};

    auto main = top(session.title,
                    std::format("<span id=pill class=\"pill {}\">{}</span> <a href=\"/project/{}\">{}</a> &middot; started {}",
                                session.status, status_words(session.status), escaped(session.project), escaped(session.project),
                                session.age),
                    acts && !stopped ? std::format("<form method=post action=\"{}/stop\"><button class=quiet>Stop</button></form>", address) : "");
    main += std::format("<div id=log data-count={}>", events.size());
    for (auto index = std::size_t{0}; index < events.size(); ++index) {
        if (events[index].kind != "tool") {
            main += item(events[index]);
            continue;
        }
        auto steps = std::string{};
        auto count = 0;
        for (; index < events.size() && events[index].kind == "tool"; ++index, ++count) {
            steps += item(events[index]);
        }
        main += std::format("<details class=steps{}><summary>{} step{}</summary>{}</details>",
                            busy && index == events.size() ? " open" : "", count, count == 1 ? "" : "s", steps);
        --index;
    }
    main += "</div>";

    if (session.status == "terminal") {
        main += std::format("<section>This one is in a terminal: <code>bot attach {}</code></section>", escaped(session.project));
    } else if (stopped) {
        main += std::format("<section class=muted>Stopped. Its work is the branch <code>{}</code>.</section>", escaped(session.id));
        main += acts ? std::format("<section><form method=post action=\"{}/rm\"><button class=quiet>Remove this session</button></form></section>", address) : "";
    } else if (acts) {
        main += std::format("<form id=say class=compose method=post action=\"{}/say\"><textarea name=message rows=2 required "
                            "placeholder=\"Message\"></textarea><button>Send</button></form>",
                            address);
    }
    if (!stopped && session.status != "terminal") {
        main += std::format("<script nonce={}>{}</script>", nonce(), script);
    }
    return page(config, session.title, main, session.id,
                busy ? std::format("<noscript><meta http-equiv=refresh content=\"5;url={}\"></noscript>", address) : "");
}

// Who is at that address, by asking the network the request came in on. The
// address is the one thing about a request its sender cannot choose.
auto is_owner(Config const& config, std::string_view remote) -> bool
{
    if (config.whois.empty() || !is_address(remote)) {
        return false;
    }
    auto argv = config.whois;
    argv.emplace_back(remote);
    auto const answer = run_succeeds(argv);
    return answer && answer->substr(0, answer->find_last_not_of(" \r\n") + 1) == config.owner;
}

// A form is only acted on if it was this site's own page that sent it, and
// this site is only ever this machine's name or an address. Together they
// stop a page somewhere else from pressing the buttons with its owner's hands.
auto is_from_here(Request const& request) -> bool
{
    auto const host = request.headers.find("host");
    auto const origin = request.headers.find("origin");
    if (host == request.headers.end() || origin == request.headers.end() || origin->second != "http://" + host->second) {
        return false;
    }
    auto name = host->second;
    if (name.starts_with('[')) {
        name = name.substr(1, name.find(']') - 1);
    } else if (auto const port = name.rfind(':'); port != std::string::npos) {
        name.erase(port);
    }
    auto self = std::string(256, '\0');
    ::gethostname(self.data(), self.size() - 1);
    return is_address(name) || name == self.c_str();
}

auto act(Config const& config, Request const& request, std::string* launching) -> std::string
{
    auto const field = [&](std::string const& name) {
        auto const found = request.form.find(name);
        return found != request.form.end() ? found->second : std::string{};
    };
    auto const& path = request.path;

    if (path == "/init") {
        auto const made = create(config, NewProject{field("name"), field("description"), field("profile"), field("notes")});
        return made ? see("/project/" + field("name")) : refused(config, "400 Bad Request", made.error());
    }
    if (path.starts_with("/project/") && path.ends_with("/run")) {
        auto const opened = open_session(config, path.substr(9, path.size() - 13), field("message"));
        if (!opened) {
            return refused(config, "400 Bad Request", opened.error());
        }
        // The answer goes back now. Starting is slow, so it happens after, and
        // the session's own page is where it shows.
        if (launching != nullptr) {
            *launching = *opened;
        } else {
            (void)launch(config, *opened);
        }
        return see("/session/" + *opened);
    }
    if (path.starts_with("/session/")) {
        auto const slash = path.find('/', 9);
        auto const id = path.substr(9, slash == std::string::npos ? slash : slash - 9);
        auto const verb = slash == std::string::npos ? std::string{} : path.substr(slash + 1);
        auto const done = verb == "say" ? say(config, id, field("message")).transform([] { return std::string{}; })
                        : verb == "stop" ? stop(config, id)
                        : verb == "rm" ? remove(config, id)
                                       : std::unexpected(std::string{"There is nothing here to do."});
        return !done ? refused(config, "400 Bad Request", done.error()) : see(verb == "rm" ? "/" : "/session/" + id);
    }
    return refused(config, "404 Not Found", "There is nothing here to do.");
}

}

auto escaped(std::string_view text) -> std::string
{
    auto result = std::string{};
    for (auto const character : text) {
        switch (character) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        default: result += character;
        }
    }
    return result;
}

auto respond(Config const& config, std::string_view raw, std::string_view remote, std::string* launching) -> std::string
{
    auto const request = parsed(raw);
    // With an owner named, every request is the owner's or is refused, and the
    // face acts. With none, it reads for whoever can reach it and does nothing.
    auto const acts = !config.owner.empty();
    if (acts && !is_owner(config, remote)) {
        return response("403 Forbidden", "This is not yours.");
    }
    if (request.method == "POST" && acts) {
        return is_from_here(request) ? act(config, request, launching)
                                     : refused(config, "403 Forbidden", "That did not come from this site's own page.");
    }
    if (request.method != "GET") {
        return response("405 Method Not Allowed", "This face shows; it does not do.");
    }

    auto const& path = request.path;
    if (path == "/") {
        return response("200 OK", board(config, acts));
    }
    if (path.starts_with("/project/")) {
        auto const name = path.substr(9);
        if (auto const names = project_names(config); names && std::ranges::contains(*names, name)) {
            return response("200 OK", project_page(config, name, acts));
        }
    }
    if (path.starts_with("/session/")) {
        auto const all = sessions(config);
        if (auto const found = std::ranges::find(all, path.substr(9), &Session::id); found != all.end()) {
            return response("200 OK", session_page(config, *found, acts));
        }
    }
    return refused(config, "404 Not Found", "There is nothing at that address.");
}

// A session's events as they happen, for the script: each new one as it
// would appear on the page, and the session's status whenever it changes.
// It ends when the session has stopped or nobody is listening any more.
auto stream(Config const& config, std::string_view raw, std::string_view remote, std::ostream& out) -> bool
{
    auto const request = parsed(raw);
    auto const& path = request.path;
    if (request.method != "GET" || !path.starts_with("/session/") || !path.ends_with("/stream")
        || (!config.owner.empty() && !is_owner(config, remote))) {
        return false;
    }
    auto const id = path.substr(9, path.size() - 16);
    if (!takes_messages(config, id)) {
        return false;
    }

    // Where to resume: what the page already shows, or the last event a broken connection delivered.
    auto const resumed = request.headers.find("last-event-id");
    auto const after = request.query.starts_with("after=") ? request.query.substr(6) : std::string{"0"};
    auto sent = static_cast<std::size_t>(std::atol((resumed != request.headers.end() ? resumed->second : after).c_str()));

    out << "HTTP/1.0 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\n\r\n";
    auto status = std::string{};
    auto running = true;
    for (auto tick = 0; out; ++tick) {
        auto const said = transcript(config, id);
        for (; sent < said.events.size(); ++sent) {
            if (auto const html = item(said.events[sent]); !html.empty()) {
                out << "id: " << sent + 1 << "\nevent: add\ndata: " << html << "\n\n";
            }
        }
        // Asking the runtime costs more than reading a file, so it is asked less often.
        auto now = running ? std::string{said.working ? "working" : "waiting"} : status;
        if (tick % 4 == 0) {
            auto const all = sessions(config);
            auto const found = std::ranges::find(all, id, &Session::id);
            now = found != all.end() ? found->status : "stopped";
            running = now == "working" || now == "waiting";
        }
        if (now != status) {
            status = now;
            out << "event: status\ndata: " << status_words(status) << "\n\n";
        }
        // A line that means nothing, so that a listener who has gone is noticed.
        out << ":\n\n" << std::flush;
        if (status == "stopped") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }
    return true;
}

}
