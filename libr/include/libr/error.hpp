#pragma once

namespace rlib {

struct ErrorCategory {};

class Error {
public:
    constexpr Error(int code, const ErrorCategory* category) :
        code(code), category(category) {}

    constexpr Error(const Error& error) :
        code(error.code), category(error.category) {}

    constexpr bool operator==(const Error& other) const {
        return code == other.code && category == other.category;
    }

private:
    int code;
    const ErrorCategory* category;
};

struct CommonCategory : ErrorCategory {};
inline constexpr auto commonCategory = CommonCategory{};

inline constexpr auto InvalidArgument = Error{-1, &commonCategory};


} // namespace rlib
