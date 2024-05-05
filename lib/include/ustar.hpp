#pragma once

#include <algorithm>
#include <cstdint>
#include <tuple>
#include "stream.hpp"
#include <ranges>
#include <type_traits>
#include "string.hpp"

namespace rlib::UStar {

struct LookupResult {
    enum class Code : int {
        OK = 0,
        NOT_FOUND = -1,
        INVALID_USTAR = -2,
        STREAM_ERROR = -3
    } result;
    StreamResult streamError;
};

template<class Source, CharRange R>
std::tuple<InputStream<Source>&, LookupResult> lookup(InputStream<Source>& archive, R filename) {
    while (true) {
        auto entryOffset = archive.position();
        
        using CharStreamRange = StreamRange<char, MemorySource>;
        auto fileNameRange = CharStreamRange(archive) | nullTerminated(100);
        
        auto foundFile = std::ranges::equal(fileNameRange, filename);
        if (archive.lastReadResult() != StreamResult::OK) {
            if (archive.position() == entryOffset) {
                return { archive, { LookupResult::Code::NOT_FOUND, archive.lastReadResult() }};
            }
            return { archive, { LookupResult::Code::STREAM_ERROR, archive.lastReadResult() }};
        }
        
        auto magic = CharStreamRange(archive.seek(entryOffset + 257)) | std::views::take(6);
        auto hasMagic = std::ranges::equal(magic, "ustar");
        if (!hasMagic) {
            return { archive, { LookupResult::Code::INVALID_USTAR, archive.lastReadResult() }};
        }

        auto fileSizeRange = CharStreamRange(archive.seek(entryOffset + 124)) | std::views::take(11);
        auto [ iter, fileSize ] = oct2bin(fileSizeRange);
        if (iter != fileSizeRange.end()) {
            return { archive, { LookupResult::Code::INVALID_USTAR, archive.lastReadResult() }};
        } 

        if (foundFile) {
            archive.seek(entryOffset + 512);
            // TODO return sub-stream
            return { archive, { LookupResult::Code::OK, archive.lastReadResult() }};
        } 
        
        entryOffset += ((fileSize + 511) / 512 + 1) * 512;
        archive.seek(entryOffset);
    }
};

}
