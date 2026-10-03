#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace bee {

struct CommandResult {
    int status;
    std::string output;
};

auto run(std::vector<std::string> const& argv,
         std::filesystem::path const& directory = {}) -> std::expected<CommandResult, std::string>;

auto run_succeeds(std::vector<std::string> const& argv,
                  std::filesystem::path const& directory = {}) -> std::expected<std::string, std::string>;

}
