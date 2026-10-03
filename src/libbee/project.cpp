#include "libbee/project.hpp"

#include "libbee/command.hpp"
#include "libbee/render.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <fstream>
#include <sstream>

namespace bee {
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

auto profile_image(std::string_view profile) -> std::string_view
{
    if (profile == "cpp") {
        return "bee-cpp:latest";
    }
    if (profile == "scratch") {
        return "bee-scratch:latest";
    }
    return "bee-base:latest";
}

// The name becomes a path here and an argument to a remote shell over ssh, so
// it is restricted rather than escaped: there is no quoting that is correct for
// every remote shell.
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
        {"image", std::string{profile_image(project.profile)}},
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

auto create(Config const& config, NewProject const& project)
    -> std::expected<std::filesystem::path, std::string>
{
    if (!is_valid_name(project.name)) {
        return std::unexpected(std::format("'{}' is not a usable project name", project.name));
    }

    auto const directory = config.projects / project.name;
    if (std::filesystem::exists(directory)) {
        return std::unexpected(std::format("already exists: {}", directory.string()));
    }

    if (auto written = scaffold(config, project, directory); !written) {
        return std::unexpected(written.error());
    }

    for (auto const& argv : std::initializer_list<std::vector<std::string>>{
             {"git", "init", "-q", "-b", "main"},
             {"git", "add", "-A"},
             {"git", "commit", "-q", "-m", std::format("Scaffold {} ({} profile)", project.name, project.profile)},
         }) {
        if (auto done = run_succeeds(argv, directory); !done) {
            return std::unexpected(done.error());
        }
    }

    if (config.has_remote()) {
        auto const bare = std::format("{}/{}.git", config.remoteRoot, project.name);
        if (auto made = run_succeeds({"ssh", config.remoteHost, "git", "init", "--bare", bare}); !made) {
            return std::unexpected(made.error());
        }
        for (auto const& argv : std::initializer_list<std::vector<std::string>>{
                 {"git", "remote", "add", "origin", config.remote_url(project.name)},
                 {"git", "push", "-q", "-u", "origin", "main"},
             }) {
            if (auto done = run_succeeds(argv, directory); !done) {
                return std::unexpected(done.error());
            }
        }
    }

    return directory;
}

}
