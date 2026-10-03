#include "libbot/render.hpp"

namespace bot {

// Only the given keys are replaced, so a template holding another tool's
// {{ }} syntax passes through untouched.
auto render(std::string_view source, Fields const& fields) -> std::string
{
    auto output = std::string{};
    output.reserve(source.size());

    for (auto position = std::size_t{0}; position < source.size();) {
        auto const opening = source.find("{{", position);
        if (opening == std::string_view::npos) {
            output.append(source.substr(position));
            break;
        }
        auto const closing = source.find("}}", opening);
        if (closing == std::string_view::npos) {
            output.append(source.substr(position));
            break;
        }

        auto const key = source.substr(opening + 2, closing - opening - 2);
        auto const found = fields.find(std::string{key});
        output.append(source.substr(position, opening - position));
        if (found != fields.end()) {
            output.append(found->second);
            position = closing + 2;
        } else {
            output.append(source.substr(opening, 2));
            position = opening + 2;
        }
    }

    return output;
}

}
