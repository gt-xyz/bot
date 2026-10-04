#include "libbot/web.hpp"

#include "libbot/command.hpp"
#include "libbot/project.hpp"
#include "libbot/session.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>

#include <unistd.h>

namespace bot {
namespace {

constexpr auto stylesheet = std::string_view{
    ":root{color-scheme:light dark}"
    "body{font:16px/1.5 system-ui,sans-serif;max-width:48rem;margin:0 auto;padding:1rem}"
    "h1{font-size:1.25rem;overflow-wrap:anywhere}h2{font-size:1rem;margin-top:2rem}"
    "a{color:inherit}ul{list-style:none;padding:0}"
    "li{padding:.6rem 0;border-top:1px solid #8884}"
    "small{display:block;opacity:.7}b{font-weight:600}"
    "textarea,input,select{font:inherit;width:100%;box-sizing:border-box;margin:.2rem 0}"
    "button{font:inherit;padding:.4rem 1rem;margin:.2rem 0}"
    ".you,.agent,.tool,.error{white-space:pre-wrap;overflow-wrap:anywhere;margin:.8rem 0}"
    ".you{border-left:3px solid #8888;padding-left:.6rem}"
    ".tool{font:13px/1.4 ui-monospace,monospace;opacity:.7}.error{color:#c33}"};

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

auto page(std::string_view title, std::string_view body, std::string_view head = {}) -> std::string
{
    return std::format("<!doctype html><html lang=en><meta charset=utf-8>"
                       "<meta name=viewport content=\"width=device-width,initial-scale=1\">{3}"
                       "<title>{0}</title><style>{1}</style><h1>{0}</h1>{2}</html>",
                       escaped(title), stylesheet, body, head);
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

auto refused(std::string_view status, std::string_view why) -> std::string
{
    return response(status, page("Not done", std::format("<p class=error>{}</p><p><a href=\"/\">All projects</a></p>", escaped(why))));
}

auto button(std::string_view action, std::string_view label) -> std::string
{
    return std::format("<form method=post action=\"{}\"><button>{}</button></form>", action, label);
}

auto session_list(std::vector<Session> const& all, std::string_view project) -> std::string
{
    auto items = std::string{};
    for (auto const& session : all) {
        if (project.empty() || session.project == project) {
            items += std::format("<li><a href=\"/session/{0}\"><b>{0}</b></a><small>{1}, started {2}</small>",
                                 escaped(session.id), escaped(session.state), escaped(session.started));
        }
    }
    return items.empty() ? "<p>None.</p>" : "<ul>" + items + "</ul>";
}

auto board(Config const& config, bool const acts) -> std::string
{
    auto const all = sessions(config);
    auto body = std::string{"<h2>Projects</h2>"};
    if (auto const names = project_names(config); !names) {
        body += std::format("<p>{}</p>", escaped(names.error()));
    } else if (names->empty()) {
        body += "<p>None yet.</p>";
    } else {
        body += "<ul>";
        for (auto const& name : *names) {
            auto const project = describe(config, name);
            auto const running = std::ranges::count_if(all, [&](Session const& session) {
                return session.project == name && session.state == "running";
            });
            body += std::format("<li><a href=\"/project/{0}\"><b>{0}</b></a> {1}<small>next: {2}</small>"
                                "<small>last commit {3}{4}, {5} running</small>",
                                escaped(name), escaped(project.description), escaped(project.next_title()),
                                escaped(project.lastCommit), project.scaffoldOnly ? ", still the scaffold" : "", running);
        }
        body += "</ul>";
    }
    body += "<h2>Sessions</h2>" + session_list(all, {});
    if (acts) {
        body += "<h2>A new project</h2><form method=post action=\"/init\">"
                "<input name=name placeholder=\"name\" required pattern=\"[A-Za-z0-9][A-Za-z0-9_-]*\">"
                "<input name=description placeholder=\"one line: what is it?\" required><select name=profile>";
        for (auto const profile : profiles()) {
            body += std::format("<option value=\"{}\">{}</option>", profile, escaped(profile_summary(profile)));
        }
        body += "</select><textarea name=notes rows=3 placeholder=\"anything else on your mind\"></textarea>"
                "<button>Create</button></form>";
    }
    return page("bot", body);
}

auto project_page(Config const& config, std::string const& name, bool const acts) -> std::string
{
    auto const project = describe(config, name);
    auto body = std::format("<p>{}</p><p><a href=\"/\">All projects</a></p>", escaped(project.description));
    if (acts) {
        body += std::format("<h2>Start a session</h2><form method=post action=\"/project/{}/run\">"
                            "<textarea name=message rows=4 placeholder=\"What should it do? Leave empty for the work marked next.\">"
                            "</textarea><button>Start</button></form>",
                            escaped(name));
    }
    body += "<h2>Work</h2><ul>";
    for (auto const& item : project.work) {
        body += std::format("<li>{}{}<small>{}</small>", item.next ? "<b>next</b> " : "", escaped(item.title), escaped(item.file));
    }
    return page(name, body + "</ul><h2>Sessions</h2>" + session_list(sessions(config), name));
}

// The conversation. While the agent works the page reloads itself and has no
// form, because a reload would take the words out from under whoever was
// typing; `?write` is the same page standing still, for a message mid-turn.
auto session_page(Config const& config, Session const& session, bool const acts, bool const writing) -> std::string
{
    auto const running = session.state == "running";
    auto const conversation = takes_messages(config, session.id);
    auto const said = conversation ? transcript(config, session.id) : Transcript{};
    auto const working = running && said.working;
    auto const address = "/session/" + escaped(session.id);

    auto body = std::format("<p>{}, started {}. <a href=\"/project/{}\">{}</a></p>", escaped(session.state),
                            escaped(session.started), escaped(session.project), escaped(session.project));
    for (auto const& event : said.events) {
        if (event.kind == "done") {
            body += "<hr>";
        } else if (event.kind == "you" || event.kind == "agent" || event.kind == "tool" || event.kind == "error") {
            body += std::format("<div class={}>{}</div>", event.kind, escaped(event.text));
        }
    }
    if (!conversation) {
        body += std::format("<p>Attended in a terminal: <code>bot attach {}</code></p>", escaped(session.id));
    } else if (working && !writing) {
        body += std::format("<p id=end>Working. <a href=\"{}?write\">Write to it now</a></p>", address);
    } else if (running && acts) {
        body += std::format("<form method=post action=\"{}/say\"><textarea name=message rows=4 required></textarea>"
                            "<button>Send</button></form>",
                            address);
    }
    if (acts) {
        body += running ? button(address + "/stop", "Stop and publish its branch")
                        : std::format("<p>Its work is the branch <code>{}</code>.</p>", escaped(session.id))
                              + button(address + "/rm", "Remove this session");
    }
    // Each reload lands at the end, where the newest words are.
    return page(session.id, body,
                working && !writing ? std::format("<meta http-equiv=refresh content=\"5;url={}#end\">", address) : "");
}

// Who is at that address, by asking the network the request came in on. The
// address is the one thing about a request its sender cannot choose.
auto is_owner(Config const& config, std::string_view remote) -> bool
{
    if (remote.starts_with("::ffff:")) {
        remote.remove_prefix(7);
    }
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
        return result ? response("200 OK", page("Done", std::format("<p>{}</p><p><a href=\"/\">All projects</a></p>", escaped(*result))))
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
    return response("404 Not Found", page("Not here", "<p><a href=\"/\">All projects</a></p>"));
}

}
