#pragma once

#include <concepts>

template<typename T, typename U>
concept HasFrontInsertion = requires(T t, U& u) {
    { t.pushFront(u) } -> std::same_as<void>;
};
