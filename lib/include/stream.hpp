#include <bit>
#include <cstring>
#include <tuple>

enum class StreamResult : int {
    OK = 0,
    END_OF_STREAM = -1
};

class MemorySource {
public:
    MemorySource(std::byte* start, std::size_t size);

    std::tuple<std::byte*, StreamResult> read(std::size_t offset, std::size_t size);

private:
    std::byte* start;
    std::size_t size;
};

template <typename T>
concept IsTriviallyCopyableAndConstructable = std::is_trivially_copyable_v<T> && std::is_trivially_constructible_v<T>;

// Assume endianness is same as host
template<class Source> class InputStream {
public:
    explicit InputStream(Source buffer);

    template<IsTriviallyCopyableAndConstructable T>
    std::tuple<T, StreamResult> read();
    
    StreamResult seek(std::size_t pos);

    std::size_t position() const;

private:
    Source buffer;
    std::size_t pos;
};

MemorySource::MemorySource(std::byte* start, std::size_t size) :
    start(start), size(size) {}


template<class Source> InputStream<Source>::InputStream(Source buffer) :
    buffer(std::move(buffer)) {}


template<class Source>
template<IsTriviallyCopyableAndConstructable T>
std::tuple<T, StreamResult> InputStream<Source>::read() const {
    auto [data, error] = buffer.read(pos, sizeof(T));
    if (error != StreamResult::OK) {
        return { {}, error };
    }

    pos += sizeof(T);
    T value;
    std::memcpy(&value, &T, sizeof(T));

    return { T, StreamResult::OK };
}
