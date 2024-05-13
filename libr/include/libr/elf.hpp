#pragma once

#include "stream.hpp"
#include "pointer.hpp"
#include <optional>
#include "allocator.hpp"

namespace rlib::Elf {

struct ElfParseResult {
    // TODO using enum is broken is gcc 12.2.0 (ICE)
    enum class Code : int {
        OK = 0,
        STREAM_ERROR = -1,
        OUT_OF_MEMORY_ERROR = -2,
        INVALID_ELF = -3,
        INVALID_CLASS = -4,
        INVALID_ENDIANNESS = -5,
        INVALID_VERSION = -6,
        INVALID_OBJECT_TYPE = -7,
        INVALID_MACHINE_TYPE = -8,
        INVALID_PROGRAM_HEADER_SIZE = -9
    } result;
    StreamResult streamError;

    operator Code() const {
        return result;
    } 
};

struct Segment {
    struct Flags {
        using Type = std::uint64_t;

        static constexpr auto Executable = Type(1);
        static constexpr auto Writable = Type(2);
        static constexpr auto Readable = Type(4);
    };

    std::uint32_t type;
    Flags::Type flags;
    std::uintptr_t fileOffset;
    std::uintptr_t virtualAddress; 
    std::size_t fileSize;
    std::size_t memorySize;
};

struct Elf {
    std::uintptr_t startAddress;
    OwningPointer<Segment[]> segments; 
};

template<class Source, IsAllocator Alloc>
std::tuple<Elf, ElfParseResult> parseElf(InputStream<Source>& elfStream, Alloc& allocator) {
    constexpr char magicBytes[] = { 0x7F, 'E', 'L', 'F' };
    auto magicRange = StreamRange<char, Source>(elfStream) | std::views::take(sizeof(magicBytes));
    auto hasMagic = std::ranges::equal(magicRange, magicBytes);
    if (!hasMagic) {
        return { Elf{}, ElfParseResult{ ElfParseResult::Code::INVALID_ELF, elfStream.lastReadResult() }};
    }

    elfStream.seek(0x04);
    auto elfClass = elfStream.template read<std::uint8_t>();
    if (elfStream.ok() && elfClass != 2) {    // 64-bit
        return { Elf{}, ElfParseResult{ ElfParseResult::Code::INVALID_CLASS, StreamResult::OK }};
    }

    auto endian = elfStream.template read<std::uint8_t>();
    if (elfStream.ok() && endian != 1) {    // Little endian
        return { Elf{}, ElfParseResult{ ElfParseResult::Code::INVALID_ENDIANNESS, StreamResult::OK }};
    }

    auto version = elfStream.template read<std::uint8_t>();
    if (elfStream.ok() && version != 1) {    // Version 1 is the originial and current version of ELF
        return { Elf{}, ElfParseResult{ ElfParseResult::Code::INVALID_VERSION, StreamResult::OK }};
    }

    elfStream.seek(0x10);
    auto objectFileType = elfStream.template read<std::uint16_t>();
    if (elfStream.ok() && objectFileType != 0x02) {// Executable file  
        return { Elf{}, ElfParseResult{ ElfParseResult::Code::INVALID_OBJECT_TYPE, StreamResult::OK }};
    }

    auto machineType = elfStream.template read<std::uint16_t>();
    if (elfStream.ok() && machineType != 0x3e) { //x86-x64 
        return { Elf{}, ElfParseResult{ ElfParseResult::Code::INVALID_MACHINE_TYPE, StreamResult::OK }};
    }

    elfStream.seek(0x18);
    auto entryPoint = elfStream.template read<std::uintptr_t>();
    auto programHeaderOffset = elfStream.template read<std::size_t>();

    elfStream.seek(0x36);
    auto programHeaderEntrySize = elfStream.template read<std::uint16_t>();
    if (elfStream.ok() && programHeaderEntrySize != 0x38) { // Standard 64-bit program header size.  
        return { Elf{}, ElfParseResult{ ElfParseResult::Code::INVALID_PROGRAM_HEADER_SIZE, StreamResult::OK }};
    }
    
    auto numberOfProgramHeaders = elfStream.template read<std::uint16_t>();
    if (!elfStream.ok()) {
        return { Elf{}, ElfParseResult{ ElfParseResult::Code::STREAM_ERROR, elfStream.lastReadResult()}};
    }
    auto segments = construct<Segment[]>(allocator, numberOfProgramHeaders);
    if (segments == nullptr) {
        return { Elf{}, ElfParseResult{ ElfParseResult::Code::OUT_OF_MEMORY_ERROR, StreamResult::OK }};
    }
    for (auto i = 0; i < numberOfProgramHeaders; ++i) {
        elfStream.seek(programHeaderOffset);
        auto& segment = segments[i];
        segment.type = elfStream.template read<std::uint32_t>();
        segment.flags = elfStream.template read<std::uint32_t>();
        segment.fileOffset = elfStream.template read<std::uintptr_t>();
        segment.virtualAddress = elfStream.template read<std::uintptr_t>();
        elfStream.seek(programHeaderOffset + 0x20);
        segment.fileSize = elfStream.template read<std::size_t>();
        segment.memorySize = elfStream.template read<std::size_t>();
        
        if (!elfStream.ok()) {
            return { Elf{}, ElfParseResult{ ElfParseResult::Code::STREAM_ERROR, elfStream.lastReadResult() }};
        }
        programHeaderOffset += programHeaderEntrySize;
    }

    return { Elf{entryPoint, std::move(segments)}, ElfParseResult{ElfParseResult::Code::OK, StreamResult::OK }};
}


}
