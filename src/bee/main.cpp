#include "libbee/config.hpp"
#include "libbee/project.hpp"

#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

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
    auto const available = bee::profiles();
    std::cerr << "  toolchain\n";
    for (auto index = std::size_t{0}; index < available.size(); ++index) {
        std::cerr << std::format("    {}) {}\n", index + 1, bee::profile_summary(available[index]));
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
    std::cerr << "bee new <project>          create a project: three questions, then work\n"
                 "bee scaffold <directory> <name> <profile> <description> [notes-file]\n";
    return 2;
}

auto command_new(std::vector<std::string> const& arguments) -> int
{
    if (arguments.size() != 1) {
        return usage();
    }

    auto const config = bee::load_config(bee::default_config_path());
    if (!config) {
        return fail(config.error());
    }

    auto project = bee::NewProject{arguments[0], {}, {}, {}};
    if (!bee::is_valid_name(project.name)) {
        return fail(std::format("'{}' is not a usable project name", project.name));
    }

    std::cerr << std::format("new project: {}\n", project.name);
    project.description = ask_line("one line — what is it?");
    project.profile = ask_profile();
    project.notes = ask_notes();

    auto const created = bee::create(*config, project);
    if (!created) {
        return fail(created.error());
    }
    std::cout << created->string() << '\n';
    return 0;
}

auto command_scaffold(std::vector<std::string> const& arguments) -> int
{
    if (arguments.size() < 4 || arguments.size() > 5) {
        return usage();
    }

    auto config = bee::Config{};
    config.projects = ".";
    config.templates = std::filesystem::path{arguments[0]}.parent_path();

    if (auto const* fromEnvironment = std::getenv("BEE_TEMPLATES"); fromEnvironment != nullptr) {
        config.templates = fromEnvironment;
    }

    auto notes = std::string{};
    if (arguments.size() == 5) {
        auto file = std::ifstream{arguments[4]};
        auto buffer = std::ostringstream{};
        buffer << file.rdbuf();
        notes = buffer.str();
    }

    auto const project = bee::NewProject{arguments[1], arguments[3], arguments[2], notes};
    auto const written = bee::scaffold(config, project, arguments[0]);
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

    auto const rest = std::vector<std::string>{arguments.begin() + 1, arguments.end()};
    if (arguments[0] == "new") {
        return command_new(rest);
    }
    if (arguments[0] == "scaffold") {
        return command_scaffold(rest);
    }
    return usage();
}
