#pragma once

#include <cstdint>
#include <libr/allocator.hpp>
#include <libr/pointer.hpp>
#include <optional>

namespace Memory {

constexpr std::size_t operator ""_KiB(unsigned long long int x) {
  return 1024ULL * x;
}

constexpr std::size_t operator ""_MiB(unsigned long long int x) {
  return 1024_KiB * x;
}

constexpr std::size_t operator ""_GiB(unsigned long long int x) {
  return 1024_MiB * x;
}

struct Block {
    std::uintptr_t startAddress;
    std::size_t size;

    /**
     * Align block such that both the start and end address are a multiple of alignment.
     * 
     * Start address is rounded up, end address is rounded down.
     * When the resulting block is empty, return nothing.
     * 
     * @param alignment Should be a power of two. 
     * @returns An aligned block, possibly of size zero.
     */ 
    Block align(std::size_t alignment) const;

    Block take(std::size_t amount) const;

    Block resize(std::size_t newSize) const;
};

class PageFrameAllocator {
public:
    static std::optional<PageFrameAllocator> make(rlib::Allocator& allocator, std::size_t physicalMemory, std::size_t frameSize);

    PageFrameAllocator(rlib::OwningPointer<std::uintptr_t[]> base, std::size_t physicalMemory, std::size_t frameSize);

    std::size_t physicalMemory() const;

    Block alloc();

    void dealloc(void* ptr);
private:
    rlib::OwningPointer<std::uintptr_t[]> base;
    std::uintptr_t* top;
    std::size_t _physicalMemory;
    std::size_t frameSize;
};

struct PageFlags {
    using Type = std::uint64_t;

    static constexpr auto Present = Type(1);
    static constexpr auto Writable = Type(1) << 1;
    static constexpr auto UserAccessible = Type(1) << 2;
    static constexpr auto HugePage = Type(1) << 7;
    static constexpr auto Global = Type(1) << 8;
    static constexpr auto NoExecute = Type(1) << 63;
    static constexpr auto All = Present | Writable | UserAccessible | HugePage | Global | NoExecute;
};

enum struct PageSize : std::uint8_t {
    _4KiB = 1,
    _2MiB = 2,
    _1GiB = 3
};

class VirtualAddress {
public:
    VirtualAddress(std::uintptr_t address);

    std::uint16_t indexLevel4() const;

    std::uint16_t indexLevel3() const;

    std::uint16_t indexLevel2() const;

    std::uint16_t indexLevel1() const;

    operator std::uintptr_t() const;

private:
    std::uintptr_t address;
};

class PageTableEntry {
public:
    PageTableEntry() = default;
    
    PageTableEntry(std::uint64_t);

    bool isUsed() const;

    PageFlags::Type flags() const;

    std::uint64_t physicalAddress() const;

    PageTableEntry& setFlags(PageFlags::Type flags);

    PageTableEntry& setPhysicalAddress(std::uint64_t address);

    static PageTableEntry empty(); 

    operator std::uint64_t() const;

private:
    static constexpr std::uint64_t encodedPhysicalAddress(std::uint64_t address); 

    std::uint64_t entry;
};

enum class MapResult : int {
    OK = 0,
    OUT_OF_PHYSICAL_MEMORY = -1,
    ALREADY_MAPPED = -2
};

class PageMapper {
public:
    /**
     * Construct a new page mapper.
     * 
     * Assume that physically memory is mapped at some offset in the virtual address space.
     * 
     * @param offset Virtual address where mapping of physical memory starts.
     * @param frameAllocator Page frame allocator
     */ 
    PageMapper(std::uintptr_t offset, PageFrameAllocator frameAllocator);

    MapResult map(std::uint64_t* tableLevel4, VirtualAddress virtualAddress, std::uint64_t physicalAddress, PageSize pageSize, PageFlags::Type flags);
    
    std::size_t unmap(std::uint64_t* tableLevel4, VirtualAddress virtualAddress);
    
    MapResult allocateAndMap(std::uint64_t* tableLevel4, VirtualAddress virtualAddress, PageFlags::Type flags);

    MapResult allocateAndMapContiguous(std::uint64_t* tableLevel4, VirtualAddress virtualAddress, PageFlags::Type flags, std::size_t nFrames);

    void relocate(std::uintptr_t newOffset);

private:
    std::uint64_t* ensurePageTable(std::uint64_t&);

    std::uintptr_t offset;
    PageFrameAllocator frameAllocator;
};

}
