#pragma once

#include "libbot/config.hpp"

#include <ostream>
#include <string>
#include <string_view>

namespace bot {

// One request in, one response out, and nothing kept between them: the
// server is this function behind a socket that something else listens on.
// `remote` is the address the request came from. With no owner configured it
// answers GET and nothing else, so it can show and cannot do.
// A request to start a session is answered at once; `launching` is then
// given the session whose slow half the caller should carry out afterwards.
auto respond(Config const& config, std::string_view request, std::string_view remote = {}, std::string* launching = nullptr)
    -> std::string;

// A session's events for as long as it runs, written as they happen. False,
// with nothing written, if the request is not the owner's request for that.
auto stream(Config const& config, std::string_view request, std::string_view remote, std::ostream& out) -> bool;

auto escaped(std::string_view text) -> std::string;

}
