#include "libbot/config.hpp"
#include "libbot/command.hpp"
#include "libbot/project.hpp"
#include "libbot/session.hpp"
#include "libbot/web.hpp"

#include <array>
#include <expected>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include <unistd.h>

namespace {

auto fail(std::string_view message) -> int
{
    std::cerr << message << '\n';
    return 1;
}

auto ask_line(std::string_view prompt) -> std::string
{
    auto answer = std::string{};
    while (answer.empty()) {
        std::cerr << "  " << prompt << ' ';
        if (!std::getline(std::cin, answer)) {
            return {};
        }
    }
    return answer;
}

auto ask_profile() -> std::string
{
    auto const available = bot::profiles();
    std::cerr << "  toolchain\n";
    for (auto index = std::size_t{0}; index < available.size(); ++index) {
        std::cerr << std::format("    {}) {}\n", index + 1, bot::profile_summary(available[index]));
    }
    while (true) {
        std::cerr << std::format("  pick 1-{}: ", available.size());
        auto answer = std::string{};
        if (!std::getline(std::cin, answer)) {
            return {};
        }
        if (answer.size() == 1 && answer[0] >= '1' && answer[0] - '1' < static_cast<int>(available.size())) {
            return std::string{available[static_cast<std::size_t>(answer[0] - '1')]};
        }
    }
}

auto ask_notes() -> std::string
{
    std::cerr << "  anything else on your mind? (blank line to finish)\n";
    auto notes = std::string{};
    auto line = std::string{};
    while (std::getline(std::cin, line) && !line.empty()) {
        notes += line;
        notes += '\n';
    }
    return notes;
}

auto usage() -> int
{
    std::cerr << "bot init <project>      create a project: three questions, then work\n"
                 "bot up <project>        start a session on a fresh clone and attach to it\n"
                 "bot attach <session>    attach to a running session; ctrl-p ctrl-q detaches\n"
                 "bot stop <session>      stop a session and publish its branch\n"
                 "bot rm <session>        stop, publish, and delete a session\n"
                 "bot log <session>       what a session has printed so far\n"
                 "bot ls                  projects and sessions\n"
                 "bot check [address...]  prove a session is contained on this machine\n"
                 "bot serve               answer one http request on stdin, for a socket unit\n"
                 "bot scaffold <directory> <name> <profile> <description> [notes-file]\n";
    return 2;
}

auto command_init(bot::Config const& config, std::string const& name) -> int
{
    auto project = bot::NewProject{name, {}, {}, {}};
    if (!bot::is_valid_name(project.name)) {
        return fail(std::format("'{}' is not a usable project name", project.name));
    }

    std::cerr << std::format("new project: {}\n", project.name);
    project.description = ask_line("one line — what is it?");
    project.profile = ask_profile();
    project.notes = ask_notes();

    auto const created = bot::create(config, project);
    if (!created) {
        return fail(created.error());
    }
    std::cout << *created << '\n';
    return 0;
}

auto command_ls(bot::Config const& config) -> int
{
    auto const all = bot::sessions(config);
    if (auto const names = bot::project_names(config); names) {
        for (auto const& name : *names) {
            auto const project = bot::describe(config, name);
            std::cout << std::format("{:<24} {:<16} next: {}\n", name,
                                     project.scaffoldOnly ? "scaffold only" : project.lastCommit, project.next_title());
        }
    } else {
        std::cerr << names.error() << '\n';
    }
    for (auto const& session : all) {
        std::cout << std::format("  {:<40} {:<10} started {}\n", session.id, session.state, session.started);
    }
    return 0;
}

// One request, one response, exit. The socket is stdin and stdout, handed
// over by whatever listens, so there is no listening code and no idle process.
auto command_serve(bot::Config const& config) -> int
{
    auto request = std::string{};
    auto buffer = std::array<char, 2048>{};
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
        auto const count = ::read(STDIN_FILENO, buffer.data(), buffer.size());
        if (count <= 0) {
            break;
        }
        request.append(buffer.data(), static_cast<std::size_t>(count));
    }
    std::cout << bot::respond(config, request) << std::flush;
    return 0;
}

auto say(std::expected<std::string, std::string> const& outcome) -> int
{
    if (!outcome) {
        return fail(outcome.error());
    }
    std::cout << *outcome << '\n';
    return 0;
}

auto command_scaffold(std::vector<std::string> const& arguments) -> int
{
    if (arguments.size() < 4 || arguments.size() > 5) {
        return usage();
    }

    auto config = bot::Config{};
        config.templates = std::filesystem::path{arguments[0]}.parent_path();

    if (auto const* fromEnvironment = std::getenv("BOT_TEMPLATES"); fromEnvironment != nullptr) {
        config.templates = fromEnvironment;
    }

    auto notes = std::string{};
    if (arguments.size() == 5) {
        auto file = std::ifstream{arguments[4]};
        auto buffer = std::ostringstream{};
        buffer << file.rdbuf();
        notes = buffer.str();
    }

    auto const project = bot::NewProject{arguments[1], arguments[3], arguments[2], notes};
    auto const written = bot::scaffold(config, project, arguments[0]);
    if (!written) {
        return fail(written.error());
    }
    return 0;
}

}

auto main(int argc, char** argv) -> int
{
    auto const arguments = std::vector<std::string>{argv + 1, argv + argc};
    if (arguments.empty()) {
        return usage();
    }

    auto const& command = arguments[0];
    auto const rest = std::vector<std::string>{arguments.begin() + 1, arguments.end()};
    if (command == "scaffold") {
        return command_scaffold(rest);
    }

    auto const config = bot::load_config(bot::default_config_path());
    if (!config) {
        return fail(config.error());
    }
    if (command == "ls" && rest.empty()) {
        return command_ls(*config);
    }
    if (command == "serve" && rest.empty()) {
        return command_serve(*config);
    }
    if (command == "check") {
        return bot::check(*config, rest, std::cout) ? 0 : fail("a session is not contained on this machine");
    }
    if (rest.size() != 1) {
        return usage();
    }
    if (command == "init") {
        return command_init(*config, rest[0]);
    }
    if (command == "up") {
        auto const id = bot::up(*config, rest[0]);
        if (!id) {
            return fail(id.error());
        }
        std::cerr << std::format("session {}: ctrl-p ctrl-q detaches, `bot attach {}` returns\n", *id, *id);
        return fail(bot::replace(bot::attach_command(*config, *id)));
    }
    if (command == "stop") {
        return say(bot::stop(*config, rest[0]));
    }
    if (command == "rm") {
        return say(bot::remove(*config, rest[0]));
    }
    if (command == "attach" || command == "log") {
        if (!bot::is_session(*config, rest[0])) {
            return fail(std::format("no session named {}", rest[0]));
        }
        auto attach = bot::attach_command(*config, rest[0]);
        if (command == "log") {
            attach[1] = "logs";
        }
        return fail(bot::replace(attach));
    }
    return usage();
}
