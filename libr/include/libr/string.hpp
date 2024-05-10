#pragma once

#include <cstddef>
#include <ranges>

namespace rlib {
    template <typename R>
    concept CharRange = std::ranges::input_range<R> && std::same_as<std::ranges::range_value_t<R>, char>;

    // Stop gap for along as basic_view is not freestanding
    constexpr auto operator ""_sv( const char* str, std::size_t len ) {
        return std::views::counted(str, len);
    }

    constexpr auto nullTerminated(std::size_t maxSize) {
        return std::views::take(maxSize) | std::views::take_while([](auto c) constexpr { return c != '\0'; });
    };

    template<CharRange R>
    std::tuple<std::ranges::iterator_t<R>, std::size_t> oct2bin(R octalString) {
        auto result = std::size_t(0);

        auto iter = octalString.begin();
        while (iter != octalString.end()) {
            auto octalDigit = *iter;
            if (octalDigit < '0' || octalDigit > '7') {
                return { iter, 0 };
            }

            result = result * 8 + (octalDigit - '0');
            iter++;
        }

        return { iter, result };
    }
}
