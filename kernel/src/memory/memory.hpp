#pragma once

#include <cstdint>
#include <tuple>

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
    std::uintptr_t ptr;
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

    Block resize(std::size_t newSize) const;
};

class PageFrameAllocator {
public:
    static std::size_t requiredStorage(std::size_t numberOfFrames); 

    PageFrameAllocator(Block storage, std::size_t physicalMemory, std::size_t frameSize);

    std::size_t physicalMemory() const;

    Block alloc();

    void dealloc(void* ptr);

    void relocate(std::uintptr_t newOffset);
private:
    std::uintptr_t* base;
    std::uintptr_t* top;
    std::size_t _physicalMemory;
    std::size_t frameSize;
};

struct PageFlags {
    using Type = std::uint64_t;

    static constexpr Type Present = Type(1);
    static constexpr Type Writable = Type(1) << 1;
    static constexpr Type UserAccessible = Type(1) << 2;
    static constexpr Type HugePage = Type(1) << 7;
    static constexpr Type Global = Type(1) << 8;
    static constexpr Type NoExecute = Type(1) << 63;
    static constexpr Type All = Present | Writable | UserAccessible | HugePage | Global | NoExecute;
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

class PageMapper {
public:
    /**
     * Construct a new page mapper.
     * 
     * Assume that physically memory is mapped at some offset in the virtual address space.
     * 
     * @param level4 Pointer to level 4 paging table. 
     * @param offset Virtual address where mapping of physical memory starts.
     */ 
    PageMapper(std::uint64_t* level4Table, std::uintptr_t offset, PageFrameAllocator& allocator);

    int map(VirtualAddress virtualAddress, std::uint64_t physicalAddress, PageSize pageSize, PageFlags::Type flags);

    std::size_t unmap(VirtualAddress virtualAddress);

    void relocate(std::uintptr_t newOffset);

private:
    std::uint64_t* ensurePageTable(std::uint64_t&);

    std::uint64_t* tableLevel4;
    std::uintptr_t offset;
    PageFrameAllocator* frameAllocator;
};

}
