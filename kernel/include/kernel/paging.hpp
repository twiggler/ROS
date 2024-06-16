#pragma once

#include <cstdint>
#include <libr/allocator.hpp>
#include <libr/error.hpp>
#include <libr/pointer.hpp>
#include <libr/intrusive/list.hpp>
#include <optional>
#include <expected>

constexpr std::size_t operator ""_KiB(unsigned long long int x) {
  return 1024ULL * x;
}

constexpr std::size_t operator ""_MiB(unsigned long long int x) {
  return 1024_KiB * x;
}

constexpr std::size_t operator ""_GiB(unsigned long long int x) {
  return 1024_MiB * x;
}

struct VirtualMemoryCategory : rlib::ErrorCategory {}
inline constexpr virtualMemoryCategory = VirtualMemoryCategory{};

inline constexpr auto OutOfPhysicalMemory = rlib::Error(-1, &virtualMemoryCategory);
inline constexpr auto VirtualRangeInUse = rlib::Error(-2, &virtualMemoryCategory);
inline constexpr auto AlreadyMapped = rlib::Error(-3, &virtualMemoryCategory);
inline constexpr auto OutOfBounds = rlib::Error(-4, &virtualMemoryCategory);

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

// Basic page frame allocator owning a stack of physical memory frames.
// Requires a lot of memory, optimize. 
class PageFrameAllocator {
public:
    static std::optional<PageFrameAllocator> make(rlib::Allocator& allocator, std::size_t physicalMemory, std::size_t frameSize);

    PageFrameAllocator(rlib::OwningPointer<std::uintptr_t[]> base, std::size_t physicalMemory, std::size_t frameSize);

    std::size_t physicalMemory() const;

    std::expected<Block, rlib::Error> alloc();

    void dealloc(std::uintptr_t physicalAddress);
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

enum struct PageSize : std::uint32_t {
    _4KiB = 4_KiB,
    _2MiB = 2_MiB,
    _1GiB = 1_GiB
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

class TableEntryView {
public:
    explicit TableEntryView(std::uint64_t& entry);

    TableEntryView(const TableEntryView&) = default;

    TableEntryView& operator=(const TableEntryView&);
    
    bool isUsed() const;

    PageFlags::Type flags() const;

    std::uint64_t physicalAddress() const;

    TableEntryView setFlags(PageFlags::Type flags);

    TableEntryView setPhysicalAddress(std::uint64_t address);

    void clear(); 

private:
    static constexpr std::uint64_t encodedPhysicalAddress(std::uint64_t address); 

    std::uint64_t* entry;
};

// Both TableEntryView and TableView do not own their data.
class TableView {
public:
    TableView(std::uint64_t* ptr, std::uintptr_t physicalAddress);

    TableEntryView at(std::uint16_t index);

    std::uintptr_t physicalAddress() const;

private:    
    std::uint64_t* ptr;
    std::uintptr_t _physicalAddress;
};

struct PageFrame {
    void*          ptr;
    std::uintptr_t physicalAddress;
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

    TableView mapTableView(std::uintptr_t physicalAddress) const;
    
    std::expected<TableView, rlib::Error> createPageTable();
    
    std::optional<rlib::Error> map(TableView addressSpace, VirtualAddress virtualAddress, std::uint64_t physicalAddress, PageSize pageSize, PageFlags::Type flags);
    
    std::optional<Block> unmap(TableView addressSpace, VirtualAddress virtualAddress);

    std::optional<Block> unmapAndDeallocate(TableView addressSpace, VirtualAddress virtualAddress);
    
    std::expected<PageFrame, rlib::Error> allocate();
       
    std::optional<rlib::Error> allocateAndMap(TableView addressSpace, VirtualAddress virtualAddress, PageFlags::Type flags);

    std::optional<rlib::Error> allocateAndMapRange(TableView addressSpace, VirtualAddress virtualAddress, PageFlags::Type flags, std::size_t nFrames);

    std::size_t unmapAndDeallocateRange(TableView addressSpace, VirtualAddress virtualAddress, std::size_t size);
    
    void relocate(std::uintptr_t newOffset);

private:
    std::expected<TableView, rlib::Error> ensurePageTable(TableEntryView entry);

    TableView mapTableView(TableEntryView entry) const; 

    std::uintptr_t offset;
    PageFrameAllocator frameAllocator;
};

class Region : rlib::intrusive::ListNode<Region> {
public:
    Region(VirtualAddress virtualAddress, std::size_t sizeInFrames, PageFlags::Type pageFlags, PageSize pageSize);

    bool overlap(const Region& other) const;

    std::optional<rlib::Error> mapPage(TableView tableLevel4, PageMapper& pageMapper, std::uint64_t physicalAddress, std::size_t pageIndex);

    std::optional<rlib::Error> map(TableView tableLevel4, PageMapper& pageMapper);

    VirtualAddress start() const;

    VirtualAddress end() const;

    std::size_t size() const;   

private:
    friend class rlib::intrusive::List<Region>;
    friend class rlib::intrusive::NodeFromBase<Region>;
    
    std::size_t pageSizeInBytes() const;

    VirtualAddress _start;
    std::size_t sizeInFrames;
    PageFlags::Type pageFlags;
    PageSize _pageSize;
};

class AddressSpace {
public:
    static std::expected<AddressSpace, rlib::Error> make(PageMapper& pageMapper, rlib::Allocator& allocator);

    explicit AddressSpace(PageMapper& pageMapper, TableView tableLevel4, rlib::Allocator& allocator);

    AddressSpace(AddressSpace&& other) = default;
    
    AddressSpace(const AddressSpace&) = delete;

    AddressSpace& operator=(const AddressSpace&) = delete;

    std::expected<Region*, rlib::Error> reserve(VirtualAddress start, std::size_t size, PageFlags::Type flags, PageSize pageSize);

    std::expected<Region*, rlib::Error> allocate(VirtualAddress start, std::size_t size, PageFlags::Type flags, PageSize pageSize);

    std::optional<rlib::Error> mapPage(Region& region, std::uint64_t physicalAddress, std::size_t offsetInFrames);
    
    std::uintptr_t pageDirectoryPhysicalAddress() const;
    
    // TODO: Take a source "AddressSpace" to improve encapsulation.
    void shallowCopyMapping(TableView from, VirtualAddress startAddress, VirtualAddress endAddress);
    
    ~AddressSpace();

private:
    PageMapper* pageMapper;
    TableView tableLevel4;
    // Is a custom allocator worth it, or can we get by with a generic kernel allocator?
    rlib::Allocator* allocator;
    rlib::intrusive::List<Region> regions;
};
