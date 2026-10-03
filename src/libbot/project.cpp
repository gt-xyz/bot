#include "libbot/project.hpp"

#include "libbot/command.hpp"
#include "libbot/render.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <fstream>
#include <sstream>

#include <unistd.h>

namespace bot {
namespace {

constexpr auto knownProfiles = std::array<std::string_view, 3>{"cpp", "scratch", "none"};

auto read_file(std::filesystem::path const& path) -> std::expected<std::string, std::string>
{
    auto file = std::ifstream{path, std::ios::binary};
    if (!file) {
        return std::unexpected(std::format("cannot read {}", path.string()));
    }
    auto buffer = std::ostringstream{};
    buffer << file.rdbuf();
    return buffer.str();
}

auto write_file(std::filesystem::path const& path, std::string_view content)
    -> std::expected<void, std::string>
{
    auto error = std::error_code{};
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return std::unexpected(std::format("cannot create {}: {}", path.parent_path().string(), error.message()));
    }
    auto file = std::ofstream{path, std::ios::binary};
    if (!file) {
        return std::unexpected(std::format("cannot write {}", path.string()));
    }
    file << content;
    return {};
}

auto copy_tree(std::filesystem::path const& from, std::filesystem::path const& to, Fields const& fields)
    -> std::expected<void, std::string>
{
    if (!std::filesystem::is_directory(from)) {
        return {};
    }
    for (auto const& entry : std::filesystem::recursive_directory_iterator{from}) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto const relative = std::filesystem::relative(entry.path(), from);
        auto const source = read_file(entry.path());
        if (!source) {
            return std::unexpected(source.error());
        }
        if (auto written = write_file(to / relative, render(*source, fields)); !written) {
            return std::unexpected(written.error());
        }
    }
    return {};
}

auto seed_work(NewProject const& project) -> std::string
{
    return std::format(
        "---\ntitle: Shape v0.1\nnext: true\n---\n\n"
        "{} is scaffolded and nothing has been decided beyond the description.\n\n"
        "Settle, in docs/PROJECT.md: why now, where v0.1 stops, what it will not\n"
        "do, and what working means. Then replace this with the real work.\n",
        project.name);
}

}

auto profiles() -> std::span<std::string_view const>
{
    return knownProfiles;
}

auto is_profile(std::string_view candidate) -> bool
{
    return std::ranges::find(knownProfiles, candidate) != knownProfiles.end();
}

auto profile_summary(std::string_view profile) -> std::string_view
{
    if (profile == "cpp") {
        return "c++ / cmake";
    }
    if (profile == "scratch") {
        return "scratch — python + node, for exploratory ideas";
    }
    return "none — notes and docs, no build";
}

// The name becomes a path, a container name and an argument to a remote shell
// over ssh, so it is restricted rather than escaped: there is no quoting that
// is correct for every remote shell.
auto is_valid_name(std::string_view name) -> bool
{
    if (name.empty() || name.size() > 64) {
        return false;
    }
    if (!(std::isalnum(static_cast<unsigned char>(name.front())) != 0)) {
        return false;
    }
    return std::ranges::all_of(name, [](char const character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '-' || character == '_';
    });
}

auto scaffold(Config const& config, NewProject const& project,
              std::filesystem::path const& directory) -> std::expected<void, std::string>
{
    if (!is_profile(project.profile)) {
        return std::unexpected(std::format("unknown profile '{}'", project.profile));
    }

    auto const fields = Fields{
        {"name", project.name},
        {"description", project.description},
        {"profile", project.profile},
        {"notes", project.notes.empty() ? "Nothing captured beyond the description." : project.notes},
    };

    if (auto copied = copy_tree(config.templates / "common", directory, fields); !copied) {
        return std::unexpected(copied.error());
    }
    if (auto copied = copy_tree(config.templates / project.profile, directory, fields); !copied) {
        return std::unexpected(copied.error());
    }
    return write_file(directory / "work" / "0001-shape-v0.1.md", seed_work(project));
}

auto create(Config const& config, NewProject const& project) -> std::expected<std::string, std::string>
{
    if (!is_valid_name(project.name)) {
        return std::unexpected(std::format("'{}' is not a usable project name", project.name));
    }

    auto const url = config.remote_url(project.name);
    auto const bare = std::format("{}/{}.git", config.remoteRoot, project.name);
    if (config.remote_is_local() && std::filesystem::exists(bare)) {
        return std::unexpected(std::format("already exists: {}", bare));
    }

    // The scaffold is committed in a directory that is deleted once pushed: a
    // project's home is its remote, and a session makes its own clone.
    auto const directory = std::filesystem::temp_directory_path() / std::format("bot-init-{}-{}", project.name, ::getpid());
    auto const cleanup = [&directory] {
        auto ignored = std::error_code{};
        std::filesystem::remove_all(directory, ignored);
    };

    if (auto written = scaffold(config, project, directory); !written) {
        cleanup();
        return std::unexpected(written.error());
    }

    auto const makeBare = config.remote_is_local()
        ? std::vector<std::string>{"git", "init", "-q", "--bare", "-b", "main", bare}
        : std::vector<std::string>{"ssh", config.remoteHost, "git", "init", "-q", "--bare", "-b", "main", bare};
    for (auto const& argv : std::initializer_list<std::vector<std::string>>{
             {"git", "init", "-q", "-b", "main"},
             {"git", "add", "-A"},
             {"git", "commit", "-q", "-m", std::format("Scaffold {} ({} profile)", project.name, project.profile)},
             makeBare,
             {"git", "push", "-q", url, "main"},
         }) {
        if (auto done = run_succeeds(argv, directory); !done) {
            cleanup();
            return std::unexpected(done.error());
        }
    }

    cleanup();
    return url;
}

auto Project::next_title() const -> std::string
{
    auto const found = std::ranges::find_if(work, [](Work const& item) { return item.next; });
    return found != work.end() ? found->title : std::string{};
}

auto project_names(Config const& config) -> std::expected<std::vector<std::string>, std::string>
{
    if (!config.remote_is_local()) {
        return std::unexpected(std::format("the projects are on {}; the board is read where they live", config.remoteHost));
    }
    auto names = std::vector<std::string>{};
    auto error = std::error_code{};
    for (auto const& entry : std::filesystem::directory_iterator{config.remoteRoot, error}) {
        if (entry.is_directory() && entry.path().extension() == ".git" && is_valid_name(entry.path().stem().string())) {
            names.push_back(entry.path().stem().string());
        }
    }
    std::ranges::sort(names);
    return names;
}

// Three reads of the bare repository, and only ever of main: a branch a
// session published is not the project until its owner merges it.
auto describe(Config const& config, std::string const& name) -> Project
{
    auto project = Project{};
    project.name = name;
    auto const git = [&](std::vector<std::string> arguments) {
        arguments.insert(arguments.begin(), {"git", "--git-dir", config.remote_url(name)});
        return run_succeeds(arguments).value_or(std::string{});
    };

    auto commits = std::istringstream{git({"log", "-2", "--format=%cr", "main"})};
    auto second = std::string{};
    std::getline(commits, project.lastCommit);
    project.scaffoldOnly = !project.lastCommit.empty() && !std::getline(commits, second);

    // The first line of prose under the heading is the description `init` asked for.
    auto document = std::istringstream{git({"show", "main:docs/PROJECT.md"})};
    for (auto line = std::string{}; std::getline(document, line);) {
        if (!line.empty() && !line.starts_with('#')) {
            project.description = line;
            break;
        }
    }

    // Lines look like main:work/0001-shape.md:title: Shape v0.1
    auto matches = std::istringstream{git({"grep", "-E", "^(title|next): ", "main", "--", "work/"})};
    for (auto line = std::string{}; std::getline(matches, line);) {
        auto const fileEnd = line.find(".md:");
        if (!line.starts_with("main:work/") || fileEnd == std::string::npos) {
            continue;
        }
        auto const file = line.substr(10, fileEnd + 3 - 10);
        auto const field = line.substr(fileEnd + 4);
        if (project.work.empty() || project.work.back().file != file) {
            project.work.push_back(Work{file, {}, false});
        }
        if (field.starts_with("title: ") && project.work.back().title.empty()) {
            project.work.back().title = field.substr(7);
        } else if (field == "next: true") {
            project.work.back().next = true;
        }
    }
    return project;
}

}
