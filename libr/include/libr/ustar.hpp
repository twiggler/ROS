#pragma once

#include <algorithm>
#include <cstdint>
#include <tuple>
#include "stream.hpp"
#include <ranges>
#include <type_traits>
#include "string.hpp"

namespace rlib::UStar {

struct UStarErrorCategory : ErrorCategory {};

inline constexpr auto ustarErrorCategory = UStarErrorCategory{};

inline constexpr auto NotFound = Error{-2, &ustarErrorCategory};

inline constexpr auto InvalidUStar = Error{-1, &ustarErrorCategory};

template<class Source, CharRange R>
std::expected<InputStream<Source>, Error> lookup(InputStream<Source>& archive, R filename) {
    while (true) {
        auto entryOffset = archive.position();
        
        using CharStreamRange = StreamRange<char, MemorySource>;
        auto fileNameRange = CharStreamRange(archive) | nullTerminated(100);
        
        auto foundFile = std::ranges::equal(fileNameRange, filename);
        if (!archive.ok()) {
            if (!archive.eof()) {
                return std::unexpected(*archive.error());
            }
            // EOF, no new entry
            if (archive.position() == entryOffset) {
                return std::unexpected(NotFound);
            }
            // EOF in the middle of entry
            return std::unexpected(InvalidUStar);
        }
        
        auto magicRange = CharStreamRange(archive.seek(entryOffset + 257)) | std::views::take(6);
        auto hasMagic = std::ranges::equal(magicRange, "ustar");
        if (!hasMagic) {
            if (archive.ok() || archive.eof()) {
                return std::unexpected(InvalidUStar);
            }
            
            return std::unexpected(*archive.error());
        }

        auto fileSizeRange = CharStreamRange(archive.seek(entryOffset + 124)) | std::views::take(11);
        auto [ iter, fileSize ] = oct2bin(fileSizeRange);
        if (iter != fileSizeRange.end()) {
             if (archive.ok() || archive.eof()) {
                return std::unexpected(InvalidUStar);
            }
            return std::unexpected(*archive.error());
        } 

        if (foundFile) {
            return archive.slice(entryOffset + 512, fileSize);
        } 
        
        entryOffset += ((fileSize + 511) / 512 + 1) * 512;
        archive.seek(entryOffset);
    }
};

}
