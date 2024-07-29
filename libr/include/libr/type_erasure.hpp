#pragma once

#include <optional>

namespace rlib {

    template<class Result>
    struct Iterator {
        virtual std::optional<Result> next() = 0;
    };

} // namespace rlib
