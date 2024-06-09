#pragma once

#include <bit>
#include <tuple>
#include <optional>
#include <libr/error.hpp>
#include <type_traits>
#include <stddef.h>
#include <libr/memory.hpp>

namespace rlib {

struct StreamErrorCategory : ErrorCategory{};

inline constexpr auto streamErrorCategory = StreamErrorCategory{};

inline constexpr auto EndOfStream = Error(-1, &streamErrorCategory);

template <typename Source>
concept IsSlicable = requires(Source source, std::size_t start, std::size_t size) {
    { source.slice(start, size) } -> std::same_as<Source>;
};

class MemorySource {
public:
    MemorySource(std::byte* start, std::size_t size);

    void seek(std::size_t position);

    std::size_t position() const;

    std::optional<Error> read(std::size_t size, std::byte* dest);

    MemorySource slice(std::size_t start, std::size_t size) const;

private:
    std::byte* data;
    std::size_t size;
    std::size_t pos;
};

template <typename T>
concept IsStreamReadable = std::is_scalar_v<T>;


// Assume endianness is same as host
template<class Source> class InputStream {
public:
    explicit InputStream(Source buffer);

    InputStream& seek(std::size_t pos);

    std::size_t position() const;

    std::optional<Error> error() const;

    bool ok() const;

    bool eof() const;

    template<IsStreamReadable T> T read();

    InputStream slice(std::size_t start, std::size_t size) const requires IsSlicable<Source>;

private:
    Source source;
    std::size_t pos;
    std::optional<Error> lastError;
};

template<IsStreamReadable T, class Source>
class InputStreamIterator{
public:
    using value_type = T;
    using difference_type = std::ptrdiff_t;

    explicit InputStreamIterator(InputStream<Source>& stream);

    T operator*() const;

    const T* operator->() const;

    InputStreamIterator& operator++();

    InputStreamIterator operator++(int);

    bool operator==(std::default_sentinel_t) const;

    bool operator==(const InputStreamIterator& rhs) const;

private:
    InputStream<Source>* stream;
    T readValue;
};

template<IsStreamReadable T, class Source>
class StreamRange : public std::ranges::view_interface<StreamRange<T, Source>> {
public:
    explicit StreamRange(InputStream<Source>& stream) :
        stream(&stream) { }

    auto begin() const {
        return InputStreamIterator<T, Source>(*stream);
    }
    
    auto end() const { return std::default_sentinel; } 

private:
    InputStream<Source>* stream;
};

inline MemorySource::MemorySource(std::byte* start, std::size_t size) :
    data(start), size(size), pos(0) {}

inline void MemorySource::seek(std::size_t position) {
    pos = position;
}

inline std::size_t MemorySource::position() const {
    return pos;
}

inline std::optional<Error> MemorySource::read(std::size_t bytesToRead, std::byte* dest) {
    if (pos + bytesToRead > size) {
        return EndOfStream;
    }

    memcpy(dest, data + pos, bytesToRead);
    pos += bytesToRead;

    return {};
}

inline MemorySource MemorySource::slice(std::size_t start, std::size_t size) const {
    if (start + size > this->size) {
        return MemorySource(nullptr, 0);
    }

    return MemorySource(this->data + start, size);
}

template<class Source> InputStream<Source>::InputStream(Source buffer) :
    source(std::move(buffer)) { }

template<class Source> InputStream<Source>& InputStream<Source>::seek(std::size_t pos) {
    if (!ok()) {
        return *this;
    }
    source.seek(pos);
    return *this;
}

template<class Source> std::size_t InputStream<Source>::position() const {
    return source.position();
}

template<class Source> std::optional<Error> InputStream<Source>::error() const {
    return lastError;
}

template<class Source> bool InputStream<Source>::ok() const {
    return !lastError;
}

template<class Source> bool InputStream<Source>::eof() const {
    return lastError == EndOfStream;
}

template<class Source>
InputStream<Source> InputStream<Source>::slice(std::size_t start, std::size_t size) const requires IsSlicable<Source>{
    auto sliced = source.slice(start, size);
    return InputStream(sliced);
}

template<class Source>
template<IsStreamReadable T> T InputStream<Source>::read() {
    if (!ok()) {
        return T{};
    }
    
    T value;
    lastError = source.read(sizeof(T), reinterpret_cast<std::byte*>(&value));

    return value;
}

template<IsStreamReadable T, class Source>
InputStreamIterator<T, Source>::InputStreamIterator(InputStream<Source>& stream) :
    stream(&stream) 
{
    readValue = stream.template read<T>();
};

template<IsStreamReadable T, class Source>
T InputStreamIterator<T, Source>::operator*() const {
    return readValue;
}

template<IsStreamReadable T, class Source>
const T* InputStreamIterator<T, Source>::operator->() const {
    return &readValue;
}

template<IsStreamReadable T, class Source>
InputStreamIterator<T, Source>& InputStreamIterator<T, Source>::operator++() {
    readValue = stream->template read<T>();

    return *this;
}

template<IsStreamReadable T, class Source>
InputStreamIterator<T, Source> InputStreamIterator<T, Source>::operator++(int) {
    auto current = InputStreamIterator(*this);
    readValue = stream->template read<T>();

    return current;
}

template<IsStreamReadable T, class Source>
bool InputStreamIterator<T, Source>::operator==(std::default_sentinel_t) const {
    return stream->error().has_value();
}

template<IsStreamReadable T, class Source>
bool InputStreamIterator<T, Source>::operator==(const InputStreamIterator& rhs) const {
    return stream == rhs.stream;
}

}
