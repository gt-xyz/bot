#pragma once

#include "libbot/config.hpp"

#include <expected>
#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace bot {

// One agent in one container on one project. Its directory holds its own
// clone, its own home and its log; its state is whatever the runtime says.
struct Session {
    std::string id;
    std::string project;
    std::string started;
    std::string state;
};

auto sessions(Config const& config) -> std::vector<Session>;
auto session_directory(Config const& config, std::string const& id) -> std::filesystem::path;
auto is_session(Config const& config, std::string const& id) -> bool;

// Clone the project, start the session's proxy and its container. Returns the
// session's id, with the agent running and nobody attached.
auto up(Config const& config, std::string const& project) -> std::expected<std::string, std::string>;

auto attach_command(Config const& config, std::string const& id) -> std::vector<std::string>;

// Stop the containers, then publish whatever the session committed as a
// branch named after it. Returns a sentence saying what was published.
auto stop(Config const& config, std::string const& id) -> std::expected<std::string, std::string>;

// Stop, publish, then delete the containers, the network and the directory.
auto remove(Config const& config, std::string const& id) -> std::expected<std::string, std::string>;

// Start a session that runs nothing, try from inside it everything a session
// must not be able to do, and report each attempt. True only if all held.
// The addresses are extra ones that must be unreachable: a tailnet peer, a
// machine on the local network.
auto check(Config const& config, std::vector<std::string> const& addresses, std::ostream& report) -> bool;

}
