#include "libbot/web.hpp"

#include "libbot/project.hpp"
#include "libbot/session.hpp"

#include <algorithm>
#include <format>

namespace bot {
namespace {

constexpr auto stylesheet = std::string_view{
    ":root{color-scheme:light dark}"
    "body{font:16px/1.5 system-ui,sans-serif;max-width:48rem;margin:0 auto;padding:1rem}"
    "h1{font-size:1.25rem}h2{font-size:1rem;margin-top:2rem}"
    "a{color:inherit}ul{list-style:none;padding:0}"
    "li{padding:.6rem 0;border-top:1px solid #8884}"
    "small{display:block;opacity:.7}b{font-weight:600}"};

auto page(std::string_view title, std::string_view body) -> std::string
{
    return std::format("<!doctype html><html lang=en><meta charset=utf-8>"
                       "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
                       "<title>{0}</title><style>{1}</style><h1>{0}</h1>{2}</html>",
                       escaped(title), stylesheet, body);
}

auto response(std::string_view status, std::string_view body) -> std::string
{
    return std::format("HTTP/1.0 {}\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: {}\r\n"
                       "Cache-Control: no-store\r\nConnection: close\r\n\r\n{}",
                       status, body.size(), body);
}

auto session_list(std::vector<Session> const& all, std::string_view project) -> std::string
{
    auto items = std::string{};
    for (auto const& session : all) {
        if (project.empty() || session.project == project) {
            items += std::format("<li><b>{}</b><small>{}, started {}</small>", escaped(session.id), escaped(session.state),
                                 escaped(session.started));
        }
    }
    return items.empty() ? "<p>None.</p>" : "<ul>" + items + "</ul>";
}

auto board(Config const& config) -> std::string
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
    return page("bot", body + "<h2>Sessions</h2>" + session_list(all, {}));
}

auto project_page(Config const& config, std::string const& name) -> std::string
{
    auto const project = describe(config, name);
    auto body = std::format("<p>{}</p><p><a href=\"/\">All projects</a></p><h2>Work</h2><ul>", escaped(project.description));
    for (auto const& item : project.work) {
        body += std::format("<li>{}{}<small>{}</small>", item.next ? "<b>next</b> " : "", escaped(item.title), escaped(item.file));
    }
    return page(name, body + "</ul><h2>Sessions</h2>" + session_list(sessions(config), name));
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

auto respond(Config const& config, std::string_view request) -> std::string
{
    auto const line = request.substr(0, request.find("\r\n"));
    auto const pathEnd = line.rfind(' ');
    if (!line.starts_with("GET ") || pathEnd < 4) {
        return response("405 Method Not Allowed", page("Reading only", "<p>This face shows; it does not do.</p>"));
    }
    auto const path = line.substr(4, pathEnd - 4);
    if (path == "/") {
        return response("200 OK", board(config));
    }
    if (path.starts_with("/project/")) {
        auto const name = std::string{path.substr(9)};
        auto const names = project_names(config);
        if (names && std::ranges::contains(*names, name)) {
            return response("200 OK", project_page(config, name));
        }
    }
    return response("404 Not Found", page("Not here", "<p><a href=\"/\">All projects</a></p>"));
}

}
