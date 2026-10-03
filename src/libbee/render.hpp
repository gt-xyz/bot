#pragma once

#include <map>
#include <string>

namespace bee {

using Fields = std::map<std::string, std::string>;

auto render(std::string_view source, Fields const& fields) -> std::string;

}
