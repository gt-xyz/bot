#include "libbee/command.hpp"
#include "libbee/config.hpp"
#include "libbee/project.hpp"
#include "libbee/render.hpp"

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
    auto const fields = bee::Fields{{"name", "kestrel"}};
    check(bee::render("hello {{name}}", fields) == "hello kestrel", "render substitutes a known key");
    check(bee::render("${{ github.sha }}", fields) == "${{ github.sha }}",
          "render leaves another tool's braces alone");
    check(bee::render("{{unclosed", fields) == "{{unclosed", "render tolerates an unclosed brace");
    check(bee::render("{{a}}{{name}}", fields) == "{{a}}kestrel", "render keeps unknown keys verbatim");
}

// The name reaches a remote shell through ssh, where no quoting is portable.
auto names_that_could_reach_a_remote_shell_are_refused() -> void
{
    check(bee::is_valid_name("gd-water"), "a plain name is accepted");
    check(bee::is_valid_name("bee_2"), "underscores and digits are accepted");
    check(!bee::is_valid_name(""), "the empty name is refused");
    check(!bee::is_valid_name("-leading"), "a leading dash is refused");
    check(!bee::is_valid_name("../escape"), "path traversal is refused");
    check(!bee::is_valid_name("a;rm -rf /"), "shell metacharacters are refused");
    check(!bee::is_valid_name("a b"), "whitespace is refused");
    check(!bee::is_valid_name("$(whoami)"), "command substitution is refused");
}

auto commands_run_without_a_shell() -> void
{
    auto const echoed = bee::run({"echo", "$HOME && rm"});
    check(echoed.has_value() && echoed->status == 0, "echo runs");
    check(echoed && echoed->output == "$HOME && rm\n", "arguments reach the child uninterpreted");

    auto const missing = bee::run({"a-command-that-does-not-exist"});
    check(!missing.has_value(), "a missing command is an error, not a crash");

    auto const failing = bee::run({"false"});
    check(failing.has_value() && failing->status != 0, "a non-zero exit is reported, not thrown");
}

auto config_rejects_what_it_cannot_understand() -> void
{
    auto const path = std::filesystem::temp_directory_path() / "bee-unit-config";
    {
        auto file = std::ofstream{path};
        file << "# a comment\nprojects = /tmp/p\ntemplates = /tmp/t\n";
    }
    auto const good = bee::load_config(path);
    check(good.has_value(), "a minimal config loads");
    check(good && !good->has_remote(), "a config without a remote is allowed");

    {
        auto file = std::ofstream{path};
        file << "projects = /tmp/p\ntemplates = /tmp/t\nnonsense = 1\n";
    }
    check(!bee::load_config(path).has_value(), "an unknown key is refused rather than ignored");

    {
        auto file = std::ofstream{path};
        file << "templates = /tmp/t\n";
    }
    check(!bee::load_config(path).has_value(), "a missing 'projects' is refused");

    std::filesystem::remove(path);
    check(!bee::load_config(path).has_value(), "an absent config is an error");
}

auto remote_url_is_built_from_config_alone() -> void
{
    auto config = bee::Config{};
    config.remoteHost = "user@host";
    config.remoteRoot = "/srv/git";
    check(config.has_remote(), "host and root together make a remote");
    check(config.remote_url("kestrel") == "user@host:/srv/git/kestrel.git", "remote url is assembled");
}

}

auto main() -> int
{
    render_substitutes_only_known_keys();
    names_that_could_reach_a_remote_shell_are_refused();
    commands_run_without_a_shell();
    config_rejects_what_it_cannot_understand();
    remote_url_is_built_from_config_alone();

    if (failures == 0) {
        std::cout << "all unit checks passed\n";
        return 0;
    }
    std::cerr << std::format("{} unit check(s) failed\n", failures);
    return 1;
}
