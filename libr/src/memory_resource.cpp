#include <libr/memory_resource.hpp>
#include <libr/intrusive/multiindex.hpp>

using namespace rlib;


std::expected<MemoryResource, Error> MemoryResource::make(
    std::uintptr_t startAddress,
    std::size_t    size,
    std::size_t    layers,
    Allocator&     blockAllocator,
    Allocator&     skipNodeAllocator,
    Allocator&     listNodeAllocator
)
{
    auto blocksByAddress = BlocksByAddress::make(layers, skipNodeAllocator, listNodeAllocator);
    if (!blocksByAddress) {
        return std::unexpected(blocksByAddress.error());
    }
    auto blocksBySize = BlocksBySize::make(layers, skipNodeAllocator, listNodeAllocator);
    if (!blocksBySize) {
        return std::unexpected(blocksBySize.error());
    }
    auto blocks = Blocks{std::move(*blocksByAddress), std::move(*blocksBySize)};

    auto block = intrusive::emplace<OrderedBlock>(blockAllocator, blocks, startAddress, size);
    if (!block) {
        return std::unexpected(block.error());
    }

    return MemoryResource(blockAllocator, std::move(blocks));
}

MemoryResource::MemoryResource(Allocator& blockAllocator, Blocks blocks) :
    blockAllocator(&blockAllocator), blocks(std::move(blocks))
{}

MemoryResource::MemoryResource(MemoryResource&& other) :
    blockAllocator(other.blockAllocator), blocks(std::move(other.blocks))
{
    other.blockAllocator = nullptr;
}

std::expected<std::uintptr_t, Error> MemoryResource::allocate(std::size_t size)
{
    auto& blocksBySize = std::get<BlocksBySize>(blocks);
    auto  block        = blocksBySize.findFirstGreaterOrEqual(size);
    if (block == blocksBySize.end()) {
        return std::unexpected(OutOfResource);
    }

    auto address = block->startAddress;
    if (block->size > size) {
        auto error = update(*block, [&](auto& block) { block.startAddress += size, block.size -= size; }, blocks);
        if (error) {
            return std::unexpected(*error);
        }
    } else {
        removeAndDestruct(*block, *blockAllocator, blocks);
    }

    return address;
}

std::expected<std::uintptr_t, Error> MemoryResource::allocate(std::uintptr_t startAddress, std::size_t size)
{
    auto& blocksByAddress = std::get<BlocksByAddress>(blocks);
    auto  block           = blocksByAddress.findLastSmallerOrEqual(startAddress);
    if (block == blocksByAddress.end()) {
        return std::unexpected(DoesNotFit);
    }

    if (block->size - (startAddress - block->startAddress) < size) {
        return std::unexpected(DoesNotFit);
    }

    auto rightSize = block->startAddress + block->size - startAddress - size;
    if (block->startAddress < startAddress) {
        // Update block to be the free block to the left of the allocated block
        auto error = update(*block, [&](auto& block) { block.size = startAddress - block.startAddress; }, blocks);
        if (error) {
            return std::unexpected(*error);
        }

        if (rightSize > 0) {
            // Insert a new block to the right of the allocated block
            auto rightBlock = emplace<OrderedBlock>(*blockAllocator, blocks, startAddress + size, rightSize);
            if (!rightBlock) {
                return std::unexpected(rightBlock.error());
            }
        }
    } else if (rightSize > 0) {
        // Update block to be the free block to the right of the allocated block
        auto error = update(
            *block, [&](auto& block) { block.startAddress = startAddress + size, block.size = rightSize; }, blocks
        );
        if (error) {
            return std::unexpected(*error);
        }
    } else { // Block is exactly the right size
        removeAndDestruct(*block, *blockAllocator, blocks);
    }

    return startAddress;
}

std::optional<Error> MemoryResource::deallocate(std::uintptr_t address, std::size_t size)
{
    auto& blocksByAddress = std::get<BlocksByAddress>(blocks);
    auto  leftBlock       = blocksByAddress.findLastSmallerOrEqual(address);
    auto  rightBlock      = leftBlock == blocksByAddress.end() ? blocksByAddress.begin() : std::next(leftBlock);
    auto  hasLeftBlock    = leftBlock != blocksByAddress.end() && leftBlock->startAddress + leftBlock->size == address;
    auto  hasRightBlock = rightBlock != blocksByAddress.end() && rightBlock->startAddress == address + rightBlock->size;

    if (hasLeftBlock && hasRightBlock) {
        auto error = update(*leftBlock, [&](auto& block) { block.size += size + rightBlock->size; }, blocks);
        if (error) {
            return error;
        }

        removeAndDestruct(*rightBlock, *blockAllocator, blocks);
    } else if (hasLeftBlock) {
        auto error = update(*leftBlock, [&](auto& block) { block.size += size; }, blocks);
        if (error) {
            return error;
        }
    } else if (hasRightBlock) {
        auto error = update(*rightBlock, [&](auto& block) { block.startAddress -= size, block.size += size; }, blocks);
        if (error) {
            return error;
        }
    } else {
        auto block = emplace<OrderedBlock>(*blockAllocator, blocks, address, size);
        if (!block) {
            return {block.error()};
        }
    }

    return {};
}

MemoryResource::~MemoryResource()
{
    if (blockAllocator == nullptr) {
        return;
    }

    clear(blocks, *blockAllocator);
}
