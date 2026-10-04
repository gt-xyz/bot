#include "libbot/web.hpp"

#include "libbot/command.hpp"
#include "libbot/project.hpp"
#include "libbot/session.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <ranges>

#include <unistd.h>

namespace bot {
namespace {

constexpr auto stylesheet = std::string_view{
    ":root{color-scheme:light dark;--line:#8884;--soft:#8882;--accent:#3d6fe8}"
    "*{box-sizing:border-box}"
    "body{font:16px/1.5 system-ui,sans-serif;max-width:40rem;margin:0 auto;padding:1rem 1rem 4rem}"
    "a{color:inherit;text-decoration:none}h1{font-size:1.4rem;margin:.3rem 0 1rem;overflow-wrap:anywhere}"
    "h2{font-size:1.15rem;margin:0}p{margin:.4rem 0}small,.muted{opacity:.65;font-size:.88rem}small{display:block}"
    ".card{border:1px solid var(--line);border-radius:12px;padding:1rem;margin:1rem 0}"
    ".row{display:flex;gap:.7rem;align-items:baseline;padding:.65rem 0;border-top:1px solid var(--line)}"
    ".card>.row:first-of-type{margin-top:.8rem}"
    ".pill{font-size:.75rem;padding:.1rem .6rem;border-radius:99px;background:var(--soft);white-space:nowrap}"
    ".working{background:var(--accent);color:#fff}.waiting{background:#e3a72f;color:#000}"
    "summary{cursor:pointer;padding:.55rem 0;border-top:1px solid var(--line);opacity:.8}"
    "details.card>summary,.steps>summary{border:0;padding:0}"
    "textarea,input,select{font:inherit;width:100%;padding:.6rem;margin:.3rem 0;border:1px solid var(--line);"
    "border-radius:8px;background:Canvas;color:CanvasText}"
    "button{font:inherit;font-weight:600;padding:.55rem 1.2rem;margin:.3rem 0;border:0;border-radius:8px;"
    "background:var(--accent);color:#fff}button.quiet{background:var(--soft);color:inherit}"
    ".you,.agent,.tool,.error{white-space:pre-wrap;overflow-wrap:anywhere}"
    ".you{background:var(--soft);border-radius:12px;padding:.6rem .9rem;margin:1.4rem 0 1rem 2.5rem}"
    ".agent{margin:1rem 0}.error{color:#d9463e;margin:1rem 0}"
    ".steps{margin:.6rem 0;font-size:.85rem}.steps>summary{opacity:.6}"
    ".tool{font:12px/1.5 ui-monospace,monospace;opacity:.7;padding:.2rem 0 0 1rem}"
    "code{font:.85rem ui-monospace,monospace;overflow-wrap:anywhere}"};

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

auto page(std::string_view title, std::string_view body, std::string_view above = {}, std::string_view head = {}) -> std::string
{
    return std::format("<!doctype html><html lang=en><meta charset=utf-8>"
                       "<meta name=viewport content=\"width=device-width,initial-scale=1\">{3}"
                       "<title>{0}</title><style>{1}</style>{4}<h1>{0}</h1>{2}</html>",
                       escaped(title), stylesheet, body, head, above);
}

auto response(std::string_view status, std::string_view body, std::string_view extra = {}) -> std::string
{
    return std::format("HTTP/1.0 {}\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: {}\r\n"
                       "Cache-Control: no-store\r\n{}Connection: close\r\n\r\n{}",
                       status, body.size(), extra, body);
}

auto see(std::string_view where) -> std::string
{
    return response("303 See Other", "", std::format("Location: {}\r\n", where));
}

constexpr auto home = std::string_view{"<p class=muted><a href=\"/\">&larr; All projects</a></p>"};

auto refused(std::string_view status, std::string_view why) -> std::string
{
    return response(status, page("That did not happen", std::format("<p class=error>{}</p>", escaped(why)), home));
}

auto status_words(std::string_view status) -> std::string_view
{
    return status == "waiting" ? "waiting for you" : status == "terminal" ? "in a terminal" : status;
}

auto row(Session const& session) -> std::string
{
    return std::format("<a class=row href=\"/session/{}\"><span class=\"pill {}\">{}</span><span>{}<small>{}</small></span></a>",
                       escaped(session.id), session.status, status_words(session.status), escaped(session.title), session.age);
}

// A project's sessions, newest first: the live ones in sight, the stopped ones folded away.
auto session_rows(std::vector<Session> const& all, std::string_view project) -> std::string
{
    auto live = std::string{};
    auto stopped = std::string{};
    auto count = 0;
    for (auto const& session : all | std::views::reverse) {
        if (session.project == project) {
            (session.status == "stopped" ? stopped : live) += row(session);
            count += session.status == "stopped" ? 1 : 0;
        }
    }
    return count == 0 ? live : live + std::format("<details><summary>{} stopped</summary>{}</details>", count, stopped);
}

auto new_session(Project const& project) -> std::string
{
    return std::format("<form method=post action=\"/project/{}/run\"><textarea name=message rows=3 placeholder=\""
                       "What should it do? Leave this empty and it does the next piece of work: {}\"></textarea>"
                       "<button>Start</button></form>",
                       escaped(project.name), escaped(project.next_title()));
}

auto board(Config const& config, bool const acts) -> std::string
{
    auto const all = sessions(config);
    auto body = std::string{};
    if (auto const names = project_names(config); !names) {
        body += std::format("<p>{}</p>", escaped(names.error()));
    } else {
        for (auto const& name : *names) {
            auto const project = describe(config, name);
            body += std::format("<section class=card><h2><a href=\"/project/{0}\">{0}</a></h2><p class=muted>{1}</p>{2}",
                                escaped(name), escaped(project.description), session_rows(all, name));
            body += acts ? "<details><summary>New session</summary>" + new_session(project) + "</details>" : "";
            body += "</section>";
        }
    }
    if (acts) {
        body += "<details class=card><summary>New project</summary><form method=post action=\"/init\">"
                "<input name=name placeholder=\"Name: letters, digits, dashes\" required pattern=\"[A-Za-z0-9][A-Za-z0-9_-]*\">"
                "<input name=description placeholder=\"One line: what is it?\" required><select name=profile>";
        for (auto const profile : profiles()) {
            body += std::format("<option value=\"{}\">{}</option>", profile, escaped(profile_summary(profile)));
        }
        body += "</select><textarea name=notes rows=3 placeholder=\"Anything else on your mind\"></textarea>"
                "<button>Create</button></form></details>";
    }
    return page("Projects", body);
}

auto project_page(Config const& config, std::string const& name, bool const acts) -> std::string
{
    auto const project = describe(config, name);
    auto body = std::format("<p class=muted>{}</p>", escaped(project.description));
    if (acts) {
        body += "<section class=card><h2>New session</h2>" + new_session(project) + "</section>";
    }
    body += "<section class=card><h2>Sessions</h2>" + session_rows(sessions(config), name) + "</section>"
            "<section class=card><h2>Work</h2>";
    for (auto const& item : project.work) {
        body += std::format("<div class=row><span>{}{}<small>{}</small></span></div>", item.next ? "<b>next</b> " : "",
                            escaped(item.title), escaped(item.file));
    }
    return page(name, body + "</section>", home);
}

// The conversation. While the agent works the page reloads itself and has no
// form, because a reload would take the words out from under whoever was
// typing; `?write` is the same page standing still, for a message mid-turn.
auto session_page(Config const& config, Session const& session, bool const acts, bool const writing) -> std::string
{
    auto const working = session.status == "working";
    auto const address = "/session/" + escaped(session.id);
    auto body = std::format("<p><span class=\"pill {}\">{}</span> <span class=muted>started {}</span></p>", session.status,
                            status_words(session.status), session.age);

    // What it said is shown; what it did on the way is folded into a count,
    // open only for the steps it is in the middle of.
    auto const events = takes_messages(config, session.id) ? transcript(config, session.id).events : std::vector<Event>{};
    for (auto index = std::size_t{0}; index < events.size(); ++index) {
        auto const& event = events[index];
        if (event.kind == "tool") {
            auto steps = std::string{};
            auto count = 0;
            for (; index < events.size() && events[index].kind == "tool"; ++index, ++count) {
                steps += std::format("<div class=tool>{}</div>", escaped(events[index].text));
            }
            body += std::format("<details class=steps{}><summary>{} step{}</summary>{}</details>",
                                working && index == events.size() ? " open" : "", count, count == 1 ? "" : "s", steps);
            --index;
        } else if (event.kind == "you" || event.kind == "agent" || event.kind == "error") {
            body += std::format("<div class={}>{}</div>", event.kind, escaped(event.text));
        }
    }

    if (session.status == "terminal") {
        body += std::format("<p>This one is in a terminal: <code>bot attach {}</code></p>", escaped(session.project));
    } else if (working && !writing) {
        body += std::format("<p id=end class=muted>Working&hellip; <a href=\"{}?write\"><u>write to it anyway</u></a></p>", address);
    } else if (session.status != "stopped" && acts) {
        body += std::format("<form id=end method=post action=\"{}/say\"><textarea name=message rows=3 required "
                            "placeholder=\"Reply\"></textarea><button>Send</button></form>",
                            address);
    }
    if (acts && session.status != "stopped") {
        body += std::format("<form method=post action=\"{}/stop\"><button class=quiet>Stop and keep its work</button></form>", address);
    } else if (acts) {
        body += std::format("<p class=muted>Stopped. Its work is the branch <code>{}</code>.</p><form method=post "
                            "action=\"{}/rm\"><button class=quiet>Remove this session</button></form>",
                            escaped(session.id), address);
    }
    // Each reload lands at the end, where the newest words are.
    return page(session.title, body,
                std::format("<p class=muted><a href=\"/\">&larr; All projects</a> / <a href=\"/project/{0}\">{0}</a></p>",
                            escaped(session.project)),
                working && !writing ? std::format("<meta http-equiv=refresh content=\"5;url={}#end\">", address) : "");
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

auto act(Config const& config, Request const& request) -> std::string
{
    auto const field = [&](std::string const& name) {
        auto const found = request.form.find(name);
        return found != request.form.end() ? found->second : std::string{};
    };
    auto const outcome = [](std::expected<std::string, std::string> const& result) {
        return result ? response("200 OK", page("Done", std::format("<p>{}</p>", escaped(*result)), home))
                      : refused("400 Bad Request", result.error());
    };
    auto const& path = request.path;

    if (path == "/init") {
        auto const made = create(config, NewProject{field("name"), field("description"), field("profile"), field("notes")});
        return made ? see("/project/" + field("name")) : refused("400 Bad Request", made.error());
    }
    if (path.starts_with("/project/") && path.ends_with("/run")) {
        auto const started = run_session(config, path.substr(9, path.size() - 13), field("message"));
        return started ? see("/session/" + *started + "#end") : refused("400 Bad Request", started.error());
    }
    if (path.starts_with("/session/")) {
        auto const slash = path.find('/', 9);
        auto const id = path.substr(9, slash == std::string::npos ? slash : slash - 9);
        auto const verb = slash == std::string::npos ? std::string{} : path.substr(slash + 1);
        if (verb == "say") {
            auto const said = say(config, id, field("message"));
            return said ? see("/session/" + id + "#end") : refused("400 Bad Request", said.error());
        }
        if (verb == "stop") {
            return outcome(stop(config, id));
        }
        if (verb == "rm") {
            return outcome(remove(config, id));
        }
    }
    return refused("404 Not Found", "There is nothing here to do.");
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

auto respond(Config const& config, std::string_view raw, std::string_view remote) -> std::string
{
    auto const request = parsed(raw);
    // With an owner named, every request is the owner's or is refused, and the
    // face acts. With none, it reads for whoever can reach it and does nothing.
    auto const acts = !config.owner.empty();
    if (acts && !is_owner(config, remote)) {
        return refused("403 Forbidden", "This is not yours.");
    }
    if (request.method == "POST" && acts) {
        return is_from_here(request) ? act(config, request) : refused("403 Forbidden", "That did not come from this site's own page.");
    }
    if (request.method != "GET") {
        return response("405 Method Not Allowed", page("Reading only", "<p>This face shows; it does not do.</p>"));
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
            return response("200 OK", session_page(config, *found, acts, request.query == "write"));
        }
    }
    return response("404 Not Found", page("Not here", "", home));
}

}
