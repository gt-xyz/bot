#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace bee {

// Where this machine keeps things. Never in the repository: the repository is
// public and this names private infrastructure.
struct Config {
    std::filesystem::path projects;
    std::filesystem::path templates;
    std::string remoteHost;
    std::string remoteRoot;

    auto has_remote() const -> bool { return !remoteHost.empty() && !remoteRoot.empty(); }
    auto remote_url(std::string const& name) const -> std::string;
};

auto default_config_path() -> std::filesystem::path;
auto load_config(std::filesystem::path const& path) -> std::expected<Config, std::string>;

}
