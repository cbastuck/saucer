#pragma once

#include <string>
#include <variant>

#include <cstdint>
#include <optional>

namespace saucer::requests
{
    struct resize
    {
        int edge;
    };

    struct maximize
    {
        bool value;
    };

    struct minimize
    {
        bool value;
    };

    struct drag
    {
    };

    struct maximized
    {
        std::uint64_t id;
    };

    struct minimized
    {
        std::uint64_t id;
    };

    using request = std::variant<resize, maximize, minimize, drag, maximized, minimized>;

    std::optional<request> parse(const std::string &);
} // namespace saucer::requests
