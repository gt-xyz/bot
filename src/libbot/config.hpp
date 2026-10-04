#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace bot {

// Where this machine keeps things. Never in the repository: the repository is
// public and this names private infrastructure.
struct Config {
    std::filesystem::path sessions;
    std::filesystem::path templates;
    std::filesystem::path images;
    std::string remoteHost;
    std::string remoteRoot;
    std::string runtime = "podman";
    std::vector<std::string> agent;
    std::filesystem::path agentEnv;
    std::filesystem::path agentHome;
    std::vector<std::string> allow;
    std::vector<std::string> probe;
    std::string owner;
    std::vector<std::string> whois;

    auto remote_is_local() const -> bool { return remoteHost.empty(); }
    auto remote_url(std::string const& name) const -> std::string;
};

auto default_config_path() -> std::filesystem::path;
auto load_config(std::filesystem::path const& path) -> std::expected<Config, std::string>;

// A name a session may reach through its proxy. It becomes a line in the
// proxy's filter, so anything that is not a plain host name is refused.
auto is_host_name(std::string_view candidate) -> bool;
auto is_address(std::string_view candidate) -> bool;

}
