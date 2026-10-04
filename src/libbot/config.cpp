#include "libbot/config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

namespace bot {
namespace {

auto trimmed(std::string_view text) -> std::string_view
{
    auto const first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

auto home() -> std::filesystem::path
{
    auto const* value = std::getenv("HOME");
    return value != nullptr ? std::filesystem::path{value} : std::filesystem::path{};
}

auto expanded(std::string_view value) -> std::filesystem::path
{
    if (value.starts_with("~/")) {
        return home() / value.substr(2);
    }
    return std::filesystem::path{value};
}

auto words(std::string_view value) -> std::vector<std::string>
{
    auto stream = std::istringstream{std::string{value}};
    auto result = std::vector<std::string>{};
    for (auto word = std::string{}; stream >> word;) {
        result.push_back(word);
    }
    return result;
}

}

auto Config::remote_url(std::string const& name) const -> std::string
{
    if (remote_is_local()) {
        return std::format("{}/{}.git", remoteRoot, name);
    }
    return std::format("{}:{}/{}.git", remoteHost, remoteRoot, name);
}

auto default_config_path() -> std::filesystem::path
{
    if (auto const* configHome = std::getenv("XDG_CONFIG_HOME"); configHome != nullptr) {
        return std::filesystem::path{configHome} / "bot" / "config";
    }
    return home() / ".config" / "bot" / "config";
}

auto is_host_name(std::string_view candidate) -> bool
{
    if (candidate.empty()) {
        return false;
    }
    auto const plain = std::ranges::all_of(candidate, [](char const character) {
        return std::islower(static_cast<unsigned char>(character)) != 0
            || std::isdigit(static_cast<unsigned char>(character)) != 0 || character == '.' || character == '-';
    });
    auto const hasLetter = std::ranges::any_of(candidate, [](char const character) {
        return std::islower(static_cast<unsigned char>(character)) != 0;
    });
    return plain && hasLetter && candidate.front() != '.' && candidate.front() != '-';
}

auto is_address(std::string_view candidate) -> bool
{
    return !candidate.empty() && candidate.find_first_not_of("0123456789abcdefABCDEF.:") == std::string_view::npos;
}

auto load_config(std::filesystem::path const& path) -> std::expected<Config, std::string>
{
    auto file = std::ifstream{path};
    if (!file) {
        return std::unexpected(std::format("no config at {}", path.string()));
    }

    auto config = Config{};
    config.templates = std::filesystem::path{BOT_SHARE} / "templates";
    config.images = std::filesystem::path{BOT_SHARE} / "images";

    auto line = std::string{};
    auto number = 0;
    while (std::getline(file, line)) {
        ++number;
        auto const content = trimmed(line);
        if (content.empty() || content.starts_with('#')) {
            continue;
        }
        auto const separator = content.find('=');
        if (separator == std::string_view::npos) {
            return std::unexpected(std::format("{}:{}: expected key = value", path.string(), number));
        }
        auto const key = trimmed(content.substr(0, separator));
        auto const value = trimmed(content.substr(separator + 1));
        if (key == "sessions") {
            config.sessions = expanded(value);
        } else if (key == "templates") {
            config.templates = expanded(value);
        } else if (key == "images") {
            config.images = expanded(value);
        } else if (key == "remote-host") {
            config.remoteHost = value;
        } else if (key == "remote-root") {
            config.remoteRoot = value.starts_with("~/") ? expanded(value).string() : std::string{value};
        } else if (key == "runtime") {
            config.runtime = value;
        } else if (key == "agent") {
            config.agent = words(value);
        } else if (key == "agent-env") {
            config.agentEnv = expanded(value);
        } else if (key == "agent-home") {
            config.agentHome = expanded(value);
        } else if (key == "allow") {
            config.allow = words(value);
        } else if (key == "probe") {
            config.probe = words(value);
        } else if (key == "owner") {
            config.owner = value;
        } else if (key == "whois") {
            config.whois = words(value);
        } else {
            return std::unexpected(std::format("{}:{}: unknown key '{}'", path.string(), number, key));
        }
    }

    if (config.sessions.empty()) {
        return std::unexpected(std::format("{}: 'sessions' is required", path.string()));
    }
    if (config.remoteRoot.empty()) {
        return std::unexpected(std::format("{}: 'remote-root' is required", path.string()));
    }
    for (auto const& name : config.allow) {
        if (!is_host_name(name)) {
            return std::unexpected(std::format("{}: 'allow' takes host names, and '{}' is not one", path.string(), name));
        }
    }
    for (auto const& address : config.probe) {
        if (!is_address(address)) {
            return std::unexpected(std::format("{}: 'probe' takes addresses, and '{}' is not one", path.string(), address));
        }
    }
    return config;
}

}
