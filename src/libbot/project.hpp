#pragma once

#include "libbot/config.hpp"

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bot {

struct NewProject {
    std::string name;
    std::string description;
    std::string profile;
    std::string notes;
};

auto profiles() -> std::span<std::string_view const>;
auto is_profile(std::string_view candidate) -> bool;
auto profile_summary(std::string_view profile) -> std::string_view;

auto is_valid_name(std::string_view name) -> bool;

auto scaffold(Config const& config, NewProject const& project,
              std::filesystem::path const& directory) -> std::expected<void, std::string>;

// Scaffold, commit, and push to a new bare repository. Nothing stays on this
// machine but the remote, if the remote is here. Returns the remote's url.
auto create(Config const& config, NewProject const& project) -> std::expected<std::string, std::string>;

struct Work {
    std::string file;
    std::string title;
    bool next = false;
};

// Everything here is read from the remote's main branch, never stored.
struct Project {
    std::string name;
    std::string description;
    std::string lastCommit;
    bool scaffoldOnly = false;
    std::vector<Work> work;

    auto next_title() const -> std::string;
};

auto project_names(Config const& config) -> std::expected<std::vector<std::string>, std::string>;
auto describe(Config const& config, std::string const& name) -> Project;

}
