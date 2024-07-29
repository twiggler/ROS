#pragma once

#include <cstdint>
#include <algorithm>
#include "stream.hpp"
#include "pointer.hpp"
#include <optional>
#include "allocator.hpp"
#include <ranges>
#include "error.hpp"

namespace rlib::Elf {

    struct ElfErrorCategory : ErrorCategory {};
    inline constexpr auto elfErrorCategory = ElfErrorCategory{};

    inline constexpr auto InvalidElf               = Error{-3, &elfErrorCategory};
    inline constexpr auto InvalidClass             = Error{-4, &elfErrorCategory};
    inline constexpr auto InvalidEndianness        = Error{-5, &elfErrorCategory};
    inline constexpr auto InvalidVersion           = Error{-6, &elfErrorCategory};
    inline constexpr auto InvalidObjectType        = Error{-7, &elfErrorCategory};
    inline constexpr auto InvalidMachineType       = Error{-8, &elfErrorCategory};
    inline constexpr auto InvalidProgramHeaderSize = Error{-9, &elfErrorCategory};


    struct Segment {
        struct Flags {
            using Type = std::uint64_t;

            static constexpr auto Executable = Type(1);
            static constexpr auto Writable   = Type(2);
            static constexpr auto Readable   = Type(4);
        };

        struct Type {
            using _Type = std::uint32_t;

            static constexpr auto Load = _Type(1);
        };

        Type::_Type    type;
        Flags::Type    flags;
        std::uintptr_t fileOffset;
        std::uintptr_t virtualAddress;
        std::size_t    fileSize;
        std::size_t    memorySize;
    };

    struct Elf {
        std::uintptr_t           startAddress;
        OwningPointer<Segment[]> segments;
    };

    template<class Source, IsAllocator Alloc>
    std::expected<Elf, Error> parseElf(InputStream<Source>& elfStream, Alloc& allocator)
    {
        constexpr char magicBytes[] = {0x7F, 'E', 'L', 'F'};
        auto           magicRange   = StreamRange<char, Source>(elfStream) | std::views::take(sizeof(magicBytes));
        auto           hasMagic     = std::ranges::equal(magicRange, magicBytes);
        if (!hasMagic) {
            if (elfStream.ok() || elfStream.eof()) {
                return std::unexpected(InvalidElf);
            }

            return std::unexpected(*elfStream.error());
        }

        elfStream.seek(0x04);
        auto elfClass = elfStream.template read<std::uint8_t>();
        if (elfStream.ok() && elfClass != 2) { // 64-bit
            return std::unexpected(InvalidClass);
        }

        auto endian = elfStream.template read<std::uint8_t>();
        if (elfStream.ok() && endian != 1) { // Little endian
            return std::unexpected(InvalidEndianness);
        }

        auto version = elfStream.template read<std::uint8_t>();
        if (elfStream.ok() && version != 1) { // Version 1 is the originial and current version of ELF
            return std::unexpected(InvalidVersion);
        }

        elfStream.seek(0x10);
        auto objectFileType = elfStream.template read<std::uint16_t>();
        if (elfStream.ok() && objectFileType != 0x02) { // Executable file
            return std::unexpected(InvalidObjectType);
        }

        auto machineType = elfStream.template read<std::uint16_t>();
        if (elfStream.ok() && machineType != 0x3e) { //x86-x64
            return std::unexpected(InvalidMachineType);
        }

        elfStream.seek(0x18);
        auto entryPoint          = elfStream.template read<std::uintptr_t>();
        auto programHeaderOffset = elfStream.template read<std::size_t>();

        elfStream.seek(0x36);
        auto programHeaderEntrySize = elfStream.template read<std::uint16_t>();
        if (elfStream.ok() && programHeaderEntrySize != 0x38) { // Standard 64-bit program header size.
            return std::unexpected(InvalidProgramHeaderSize);
        }

        auto numberOfProgramHeaders = elfStream.template read<std::uint16_t>();
        if (!elfStream.ok()) {
            return std::unexpected(*elfStream.error());
        }
        auto segments = construct<Segment[]>(allocator, numberOfProgramHeaders);
        if (segments == nullptr) {
            return std::unexpected(OutOfMemoryError);
        }
        for (auto i = 0; i < numberOfProgramHeaders; ++i) {
            elfStream.seek(programHeaderOffset);
            auto& segment          = segments[i];
            segment.type           = elfStream.template read<std::uint32_t>();
            segment.flags          = elfStream.template read<std::uint32_t>();
            segment.fileOffset     = elfStream.template read<std::uintptr_t>();
            segment.virtualAddress = elfStream.template read<std::uintptr_t>();
            elfStream.seek(programHeaderOffset + 0x20);
            segment.fileSize   = elfStream.template read<std::size_t>();
            segment.memorySize = elfStream.template read<std::size_t>();

            if (!elfStream.ok()) {
                return std::unexpected(*elfStream.error());
            }
            programHeaderOffset += programHeaderEntrySize;
        }

        return Elf{entryPoint, std::move(segments)};
    }


} // namespace rlib::Elf
