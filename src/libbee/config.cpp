#include "libbee/config.hpp"

#include <cstdlib>
#include <format>
#include <fstream>

namespace bee {
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

}

auto Config::remote_url(std::string const& name) const -> std::string
{
    return std::format("{}:{}/{}.git", remoteHost, remoteRoot, name);
}

auto default_config_path() -> std::filesystem::path
{
    if (auto const* configHome = std::getenv("XDG_CONFIG_HOME"); configHome != nullptr) {
        return std::filesystem::path{configHome} / "bee" / "config";
    }
    return home() / ".config" / "bee" / "config";
}

auto load_config(std::filesystem::path const& path) -> std::expected<Config, std::string>
{
    auto file = std::ifstream{path};
    if (!file) {
        return std::unexpected(std::format("no config at {}", path.string()));
    }

    auto config = Config{};
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
        if (key == "projects") {
            config.projects = expanded(value);
        } else if (key == "templates") {
            config.templates = expanded(value);
        } else if (key == "remote-host") {
            config.remoteHost = value;
        } else if (key == "remote-root") {
            config.remoteRoot = value;
        } else {
            return std::unexpected(std::format("{}:{}: unknown key '{}'", path.string(), number, key));
        }
    }

    if (config.projects.empty()) {
        return std::unexpected(std::format("{}: 'projects' is required", path.string()));
    }
    if (config.templates.empty()) {
        return std::unexpected(std::format("{}: 'templates' is required", path.string()));
    }
    return config;
}

}
