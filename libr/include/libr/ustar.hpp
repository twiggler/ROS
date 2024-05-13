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
    // TODO using enum is broken is gcc 12.2.0 (ICE)
    enum class Code : int {
        OK = 0,
        NOT_FOUND = -1,
        INVALID_USTAR = -2,
        STREAM_ERROR = -3
    };
    Code result;
    StreamResult streamError;

    operator Code() const {
        return result;
    } 
};

template<class Source, CharRange R>
std::tuple<InputStream<Source>, LookupResult> lookup(InputStream<Source>& archive, R filename) {
    while (true) {
        auto entryOffset = archive.position();
        
        using CharStreamRange = StreamRange<char, MemorySource>;
        auto fileNameRange = CharStreamRange(archive) | nullTerminated(100);
        
        auto foundFile = std::ranges::equal(fileNameRange, filename);
        if (!archive.ok()) {
            if (!archive.eof()) {
                return { archive, { LookupResult::Code::STREAM_ERROR, archive.lastReadResult() }};
            }
            // EOF, no new entry
            if (archive.position() == entryOffset) {
                return { archive, { LookupResult::Code::NOT_FOUND, StreamResult::OK }};
            }
            // EOF in the middle of entry
            return { archive, { LookupResult::Code::INVALID_USTAR, StreamResult::OK }};
        }
        
        auto magicRange = CharStreamRange(archive.seek(entryOffset + 257)) | std::views::take(6);
        auto hasMagic = std::ranges::equal(magicRange, "ustar");
        if (!hasMagic) {
            if (archive.ok() || archive.eof()) {
                return { archive, { LookupResult::Code::INVALID_USTAR, StreamResult::OK }};
            }
            
            return { archive, { LookupResult::Code::STREAM_ERROR, archive.lastReadResult() }};
        }

        auto fileSizeRange = CharStreamRange(archive.seek(entryOffset + 124)) | std::views::take(11);
        auto [ iter, fileSize ] = oct2bin(fileSizeRange);
        if (iter != fileSizeRange.end()) {
             if (archive.ok() || archive.eof()) {
                return { archive, { LookupResult::Code::INVALID_USTAR, StreamResult::OK }};
            }
            return { archive, { LookupResult::Code::STREAM_ERROR, archive.lastReadResult() }};
        } 

        if (foundFile) {
            return { archive.slice(entryOffset + 512, fileSize), { LookupResult::Code::OK, StreamResult::OK }};
        } 
        
        entryOffset += ((fileSize + 511) / 512 + 1) * 512;
        archive.seek(entryOffset);
    }
};

}
