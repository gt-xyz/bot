#pragma once

#include "libbee/config.hpp"

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace bee {

struct NewProject {
    std::string name;
    std::string description;
    std::string profile;
    std::string notes;
};

auto profiles() -> std::span<std::string_view const>;
auto is_profile(std::string_view candidate) -> bool;
auto profile_summary(std::string_view profile) -> std::string_view;
auto profile_image(std::string_view profile) -> std::string_view;

auto is_valid_name(std::string_view name) -> bool;

auto scaffold(Config const& config, NewProject const& project,
              std::filesystem::path const& directory) -> std::expected<void, std::string>;

auto create(Config const& config, NewProject const& project)
    -> std::expected<std::filesystem::path, std::string>;

}
