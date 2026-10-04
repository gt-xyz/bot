#pragma once

#include "libbot/config.hpp"

#include <string>
#include <string_view>

namespace bot {

// One request in, one response out, and nothing kept between them: the
// server is this function behind a socket that something else listens on.
// `remote` is the address the request came from. With no owner configured it
// answers GET and nothing else, so it can show and cannot do.
auto respond(Config const& config, std::string_view request, std::string_view remote = {}) -> std::string;

auto escaped(std::string_view text) -> std::string;

}
