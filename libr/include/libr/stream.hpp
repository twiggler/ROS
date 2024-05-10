#pragma once

#include <bit>
#include <tuple>
#include <iterator>
#include <type_traits>
#include <stddef.h>
#include "memory.hpp"

namespace rlib {

enum class StreamResult : int {
    OK = 0,
    END_OF_STREAM = -1
};

class MemorySource {
public:
    MemorySource(std::byte* start, std::size_t size);

    void seek(std::size_t position);

    std::size_t position() const;

    StreamResult read(std::size_t size, std::byte* dest);

private:
    std::byte* start;
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

    StreamResult lastReadResult() const;

    template<IsStreamReadable T>
    std::tuple<T, StreamResult> read();

private:
    Source source;
    std::size_t pos;
    StreamResult status;
};

template<IsStreamReadable T, class Source>
class StreamRange : public std::ranges::view_interface<StreamRange<T, Source>> {
public:
    explicit StreamRange(InputStream<Source>& stream);

    auto begin() const;
    
    auto end() const; 

private:
    InputStream<Source>* stream;
};

template<IsStreamReadable T, class Source>
class InputStreamIterator{
public:
    using element_type = T;
    using pointer = element_type*;
    using reference = element_type;
    using difference_type = std::ptrdiff_t;

    explicit InputStreamIterator(InputStream<Source>& stream);

    reference operator*() const;

    const pointer operator->() const;

    InputStreamIterator& operator++();

    InputStreamIterator operator++(int);

    bool operator==(std::default_sentinel_t) const;

    bool operator==(const InputStreamIterator& rhs) const;

private:
    InputStream<Source>* stream;
    T readValue;
};

inline MemorySource::MemorySource(std::byte* start, std::size_t size) :
    start(start), size(size) {}

inline void MemorySource::seek(std::size_t position) {
    pos = position;
}

inline std::size_t MemorySource::position() const {
    return pos;
}

inline StreamResult MemorySource::read(std::size_t bytesToRead, std::byte* dest) {
    if (pos + bytesToRead > size) {
        return StreamResult::END_OF_STREAM; 
    }

    memcpy(dest, start + pos, bytesToRead);
    pos += bytesToRead;

    return StreamResult::OK;
}

template<class Source> InputStream<Source>::InputStream(Source buffer) :
    source(std::move(buffer)), 
    status(StreamResult::OK) {}

template<class Source> InputStream<Source>& InputStream<Source>::seek(std::size_t pos) {
    source.seek(pos);
    return *this;
}

template<class Source> std::size_t InputStream<Source>::position() const {
    return source.position();
}

template<class Source> StreamResult InputStream<Source>::lastReadResult() const {
    return status;
}

template<class Source>
template<IsStreamReadable T>
std::tuple<T, StreamResult> InputStream<Source>::read() {
    T value;
    status = source.read(sizeof(T), reinterpret_cast<std::byte*>(&value));

    return { value, status };
}

template<IsStreamReadable T, class Source>
StreamRange<T, Source>::StreamRange(InputStream<Source>& stream) :
    stream(&stream) { }

template<IsStreamReadable T, class Source>
auto StreamRange<T, Source>::begin() const {
    static_assert(std::input_iterator<InputStreamIterator<T, Source>>);
    static_assert(std::sentinel_for<std::default_sentinel_t, InputStreamIterator<T, Source>>);

    return InputStreamIterator<T, Source>(*stream);
}

template<IsStreamReadable T, class Stream>
auto StreamRange<T, Stream>::end() const {
    return std::default_sentinel;
}

template<IsStreamReadable T, class Source>
InputStreamIterator<T, Source>::InputStreamIterator(InputStream<Source>& stream) :
    stream(&stream) 
{
    readValue = std::get<T>(stream.template read<T>());
};

template<IsStreamReadable T, class Source>
InputStreamIterator<T, Source>::reference InputStreamIterator<T, Source>::operator*() const {
    return readValue;
}

template<IsStreamReadable T, class Source>
const InputStreamIterator<T, Source>::pointer InputStreamIterator<T, Source>::operator->() const {
    return &readValue;
}

template<IsStreamReadable T, class Source>
InputStreamIterator<T, Source>& InputStreamIterator<T, Source>::operator++() {
    readValue = std::get<T>(stream->template read<T>());

    return *this;
}

template<IsStreamReadable T, class Source>
InputStreamIterator<T, Source> InputStreamIterator<T, Source>::operator++(int) {
    auto current = InputStreamIterator(*this);
    readValue = std::get<T>(stream->template read<T>());

    return current;
}

template<IsStreamReadable T, class Source>
bool InputStreamIterator<T, Source>::operator==(std::default_sentinel_t) const {
    return stream->lastReadResult() != StreamResult::OK;
}

template<IsStreamReadable T, class Source>
bool InputStreamIterator<T, Source>::operator==(const InputStreamIterator& rhs) const {
    return stream == rhs.stream;
}

}
