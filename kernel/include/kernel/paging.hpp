#pragma once

#include <cstdint>
#include <libr/allocator.hpp>
#include <libr/error.hpp>
#include <libr/pointer.hpp>
#include <libr/intrusive/list.hpp>
#include <libr/type_erasure.hpp>
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

    std::uintptr_t endAddress() const;
};

class VirtualAddress {
public:
    constexpr VirtualAddress(std::uintptr_t address) : 
        address(address) {}

    constexpr VirtualAddress(void* ptr) :
        address(reinterpret_cast<std::uintptr_t>(ptr)) {}

    constexpr std::uint16_t indexLevel4() const {
        return (address >> 39) & 0x1FF;
    }

    constexpr std::uint16_t indexLevel3() const {
        return (address >> 30) & 0x1FF;
    }

    constexpr std::uint16_t indexLevel2() const {
        return (address >> 21) & 0x1FF;
    }

    constexpr std::uint16_t indexLevel1() const {
        return (address >> 12) & 0x1FF;
    }

    template<class T = void>
    constexpr T* ptr() const { 
        return reinterpret_cast<T*>(address); 
    }

    constexpr operator std::uintptr_t() const {
        return address;
    }

private:
    std::uintptr_t address;
};

struct IdentityMapping {
    explicit IdentityMapping(std::size_t offset);

    VirtualAddress translate(std::size_t physicalAddress) const;
private: 
    std::size_t offset;
};

struct FreePage {
    std::uintptr_t physicalAddress;
    rlib::intrusive::ListNode<FreePage> node;
};

// Holds a stack of physical memory frames.
// Uses no memory by storing the stack inside free pages. 
class PageFrameAllocator {
public:
    PageFrameAllocator(rlib::Iterator<Block>& memoryMap, IdentityMapping identityMapping, std::size_t frameSize = 4_KiB);

    std::expected<Block, rlib::Error> alloc();

    void dealloc(std::uintptr_t physicalAddress);
private:
    rlib::intrusive::ListWithNodeMember<FreePage, &FreePage::node> freePages;
    IdentityMapping identityMapping;
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

class TableEntryView {
public:
    explicit TableEntryView(std::uint64_t& entry);

    TableEntryView(const TableEntryView&) = default;

    TableEntryView& operator=(const TableEntryView&);
    
    operator bool() const;

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

    TableEntryView at(std::uint16_t index) const;

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
    PageMapper(IdentityMapping identityMapping, PageFrameAllocator frameAllocator);

    PageMapper(const PageMapper&) = delete;

    PageMapper& operator=(const PageMapper&) = delete;

    TableView mapTableView(std::uintptr_t physicalAddress) const;
    
    std::expected<TableView, rlib::Error> createPageTable();
    
    std::optional<rlib::Error> map(TableView addressSpace, VirtualAddress virtualAddress, std::uint64_t physicalAddress, PageSize pageSize, PageFlags::Type flags);

    std::optional<std::uintptr_t> read(TableView addressSpace, VirtualAddress virtualAddress);
    
    std::optional<Block> unmap(TableView addressSpace, VirtualAddress virtualAddress);

    std::optional<Block> unmapAndDeallocate(TableView addressSpace, VirtualAddress virtualAddress);
    
    std::expected<PageFrame, rlib::Error> allocate();
       
    std::optional<rlib::Error> allocateAndMap(TableView addressSpace, VirtualAddress virtualAddress, PageFlags::Type flags);

    std::optional<rlib::Error> allocateAndMapRange(TableView addressSpace, VirtualAddress virtualAddress, PageFlags::Type flags, std::size_t nFrames);

    std::size_t unmapAndDeallocateRange(TableView addressSpace, VirtualAddress virtualAddress, std::size_t size);
    
private:
    std::expected<TableView, rlib::Error> ensurePageTable(TableEntryView entry);

    TableView mapTableView(TableEntryView entry) const; 

    IdentityMapping identityMapping;
    PageFrameAllocator frameAllocator;
};

class Region : rlib::intrusive::ListNode<Region> {
public:
    Region(VirtualAddress virtualAddress, std::size_t sizeInFrames, PageFlags::Type pageFlags, PageSize pageSize);

    bool overlap(const Region& other) const;

    std::optional<rlib::Error> mapPage(TableView tableLevel4, PageMapper& pageMapper, std::uint64_t physicalAddress, std::size_t pageIndex);

    std::optional<rlib::Error> allocatePage(TableView tableLevel4, PageMapper& pageMapper, std::size_t pageIndex); 
    
    std::optional<rlib::Error> allocate(TableView tableLevel4, PageMapper& pageMapper);

    VirtualAddress start() const;

    VirtualAddress end() const;

    std::size_t size() const;

    std::size_t sizeInFrames() const;   

private:
    friend class rlib::intrusive::List<Region>;
    friend class rlib::intrusive::NodeFromBase<Region>;
    
    std::size_t pageSizeInBytes() const;

    VirtualAddress _start;
    std::size_t _sizeInFrames;
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

    std::optional<rlib::Error> mapPageOfRegion(Region& region, std::uint64_t physicalAddress, std::size_t offsetInFrames);

    std::optional<rlib::Error> allocatePageOfRegion(Region& region, std::size_t offsetInFrames);
    
    std::uintptr_t rootTablePhysicalAddress() const;
    
    void shallowCopyRootMapping(const AddressSpace& from, VirtualAddress startAddress, VirtualAddress endAddress);
    
    ~AddressSpace();

private:
    PageMapper* pageMapper;
    TableView tableLevel4;
    rlib::Allocator* allocator;
    rlib::intrusive::List<Region> regions;
};
