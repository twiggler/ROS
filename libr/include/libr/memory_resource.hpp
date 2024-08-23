#pragma once

#include "intrusive/skiplist.hpp"
#include <cstdint>

namespace rlib {

    struct ResourceErrorCategory : ErrorCategory {};
    inline constexpr auto resourceErrorCategory = ResourceErrorCategory{};

    inline constexpr auto OutOfResource = Error{-1, &resourceErrorCategory};
    inline constexpr auto DoesNotFit    = Error{-2, &resourceErrorCategory};


    struct OrderedBlock {
        std::uintptr_t startAddress = 0;
        std::size_t    size         = 0;

        intrusive::SkipListNode<OrderedBlock> addressNode;
        intrusive::SkipListNode<OrderedBlock> sizeNode;
    };

    class MemoryResource {
    public:
        static std::expected<MemoryResource, Error> make(
            std::uintptr_t startAddress,
            std::size_t    size,
            std::size_t    layers,
            Allocator&     blockAllocator,
            Allocator&     skipNodeAllocator,
            Allocator&     listNodeAllocator
        );

        MemoryResource(MemoryResource&& other);

        MemoryResource(const MemoryResource&) = delete;

        MemoryResource& operator=(const MemoryResource&) = delete;

        std::expected<std::uintptr_t, Error> allocate(std::size_t size);

        std::expected<std::uintptr_t, Error> allocate(std::uintptr_t startAddress, std::size_t size);

        std::optional<Error> deallocate(std::uintptr_t address, std::size_t size);

        ~MemoryResource();

    private:
        using BlocksByAddress = intrusive::SkipList<
            OrderedBlock,
            intrusive::NodeFromMember<OrderedBlock, intrusive::SkipListNode, &OrderedBlock::addressNode>,
            intrusive::ProjectMember<OrderedBlock, decltype(OrderedBlock::startAddress), &OrderedBlock::startAddress>>;
        using BlocksBySize = intrusive::SkipList<
            OrderedBlock,
            intrusive::NodeFromMember<OrderedBlock, intrusive::SkipListNode, &OrderedBlock::sizeNode>,
            intrusive::ProjectMember<OrderedBlock, decltype(OrderedBlock::size), &OrderedBlock::size>>;

        using Blocks = std::tuple<BlocksByAddress, BlocksBySize>;

        MemoryResource(Allocator& blockAllocator, Blocks blocks);

        Allocator* blockAllocator;
        Blocks     blocks;
    };


} // namespace rlib
