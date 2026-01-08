#pragma once

#include <expected>
#include <string>

namespace pnkr::core {

    template<typename E>
    using Unexpected = std::unexpected<E>;

    template<typename T, typename E = std::string>
    using Result = std::expected<T, E>;

}
