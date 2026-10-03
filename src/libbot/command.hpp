#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace bot {

struct CommandResult {
    int status;
    std::string output;
};

auto run(std::vector<std::string> const& argv,
         std::filesystem::path const& directory = {}) -> std::expected<CommandResult, std::string>;

// The same, with the child on this process's own terminal: for a build worth
// watching. Returns the exit status.
auto run_visible(std::vector<std::string> const& argv) -> std::expected<int, std::string>;

// Become the command. Returns only if that failed.
auto replace(std::vector<std::string> const& argv) -> std::string;

auto run_succeeds(std::vector<std::string> const& argv,
                  std::filesystem::path const& directory = {}) -> std::expected<std::string, std::string>;

}
