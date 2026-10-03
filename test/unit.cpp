#include "libbot/command.hpp"
#include "libbot/config.hpp"
#include "libbot/project.hpp"
#include "libbot/render.hpp"
#include "libbot/web.hpp"

#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>

namespace {

auto failures = 0;

auto check(bool const condition, std::string_view what) -> void
{
    if (!condition) {
        std::cerr << std::format("FAIL: {}\n", what);
        ++failures;
    }
}

auto render_substitutes_only_known_keys() -> void
{
    auto const fields = bot::Fields{{"name", "kestrel"}};
    check(bot::render("hello {{name}}", fields) == "hello kestrel", "render substitutes a known key");
    check(bot::render("${{ github.sha }}", fields) == "${{ github.sha }}",
          "render leaves another tool's braces alone");
    check(bot::render("{{unclosed", fields) == "{{unclosed", "render tolerates an unclosed brace");
    check(bot::render("{{a}}{{name}}", fields) == "{{a}}kestrel", "render keeps unknown keys verbatim");
}

// The name reaches a remote shell through ssh, where no quoting is portable.
auto names_that_could_reach_a_remote_shell_are_refused() -> void
{
    check(bot::is_valid_name("gd-water"), "a plain name is accepted");
    check(bot::is_valid_name("bot_2"), "underscores and digits are accepted");
    check(!bot::is_valid_name(""), "the empty name is refused");
    check(!bot::is_valid_name("-leading"), "a leading dash is refused");
    check(!bot::is_valid_name("../escape"), "path traversal is refused");
    check(!bot::is_valid_name("a;rm -rf /"), "shell metacharacters are refused");
    check(!bot::is_valid_name("a b"), "whitespace is refused");
    check(!bot::is_valid_name("$(whoami)"), "command substitution is refused");
}

auto commands_run_without_a_shell() -> void
{
    auto const echoed = bot::run({"echo", "$HOME && rm"});
    check(echoed.has_value() && echoed->status == 0, "echo runs");
    check(echoed && echoed->output == "$HOME && rm\n", "arguments reach the child uninterpreted");

    auto const missing = bot::run({"a-command-that-does-not-exist"});
    check(!missing.has_value(), "a missing command is an error, not a crash");

    auto const failing = bot::run({"false"});
    check(failing.has_value() && failing->status != 0, "a non-zero exit is reported, not thrown");
}

auto config_rejects_what_it_cannot_understand() -> void
{
    auto const path = std::filesystem::temp_directory_path() / "bot-unit-config";
    auto const loads = [&path](std::string_view content) {
        {
            auto file = std::ofstream{path};
            file << content;
        }
        return bot::load_config(path);
    };

    auto const good = loads("# a comment\nsessions = /tmp/s\nremote-root = /tmp/r\n");
    check(good.has_value(), "a minimal config loads");
    check(good && good->remote_is_local(), "a remote without a host is on this machine");
    check(good && good->runtime == "podman", "the runtime has a default");
    check(good && good->allow.empty(), "nothing is reachable unless it is allowed");

    check(!loads("sessions = /tmp/s\nremote-root = /tmp/r\nnonsense = 1\n").has_value(),
          "an unknown key is refused rather than ignored");
    check(!loads("remote-root = /tmp/r\n").has_value(), "a missing 'sessions' is refused");
    check(!loads("sessions = /tmp/s\n").has_value(), "a missing 'remote-root' is refused");

    auto const agent = loads("sessions = /tmp/s\nremote-root = /tmp/r\nagent = an-agent --flag\nallow = a.example b.example\n");
    check(agent && agent->agent == std::vector<std::string>{"an-agent", "--flag"}, "the agent is a command line");
    check(agent && agent->allow.size() == 2, "allowed names are separated by spaces");

    std::filesystem::remove(path);
    check(!bot::load_config(path).has_value(), "an absent config is an error");
}

// An allowed name becomes a line in the proxy's filter. Anything that could
// widen that line, or name a machine by address, is not a name.
auto only_plain_host_names_are_allowed() -> void
{
    auto const path = std::filesystem::temp_directory_path() / "bot-unit-config";
    check(bot::is_host_name("api.example.com"), "a host name is accepted");
    check(!bot::is_host_name(".*"), "a pattern is refused");
    check(!bot::is_host_name("*.example.com"), "a wildcard is refused");
    check(!bot::is_host_name("example.com|.*"), "an alternation is refused");
    check(!bot::is_host_name(std::format("{}.{}.{}.{}", 10, 0, 0, 1)), "an address is refused");
    check(!bot::is_host_name(""), "the empty name is refused");
    {
        auto file = std::ofstream{path};
        file << "sessions = /tmp/s\nremote-root = /tmp/r\nallow = .*\n";
    }
    check(!bot::load_config(path).has_value(), "a config allowing a pattern does not load");
    std::filesystem::remove(path);
}

auto remote_url_is_built_from_config_alone() -> void
{
    auto config = bot::Config{};
    config.remoteRoot = "/srv/git";
    check(config.remote_url("kestrel") == "/srv/git/kestrel.git", "a local remote is a path");
    config.remoteHost = "user@host";
    check(config.remote_url("kestrel") == "user@host:/srv/git/kestrel.git", "a remote on another machine is reached over ssh");
}

auto the_web_face_reads_and_does_nothing_else() -> void
{
    auto config = bot::Config{};
    config.sessions = "/nonexistent";
    config.remoteRoot = "/nonexistent";
    config.runtime = "a-command-that-does-not-exist";
    check(bot::respond(config, "POST / HTTP/1.1\r\n\r\n").starts_with("HTTP/1.0 405"), "anything but GET is refused");
    check(bot::respond(config, "").starts_with("HTTP/1.0 405"), "an empty request is refused");
    check(bot::respond(config, "GET / HTTP/1.1\r\n\r\n").starts_with("HTTP/1.0 200"), "the board is served");
    check(bot::respond(config, "GET /project/../etc HTTP/1.1\r\n\r\n").starts_with("HTTP/1.0 404"),
          "a project that does not exist is not found");
    check(bot::escaped("<script>&\"") == "&lt;script&gt;&amp;&quot;", "text is escaped before it reaches a page");
}

}

auto main() -> int
{
    render_substitutes_only_known_keys();
    names_that_could_reach_a_remote_shell_are_refused();
    commands_run_without_a_shell();
    config_rejects_what_it_cannot_understand();
    only_plain_host_names_are_allowed();
    remote_url_is_built_from_config_alone();
    the_web_face_reads_and_does_nothing_else();

    if (failures == 0) {
        std::cout << "all unit checks passed\n";
        return 0;
    }
    std::cerr << std::format("{} unit check(s) failed\n", failures);
    return 1;
}
