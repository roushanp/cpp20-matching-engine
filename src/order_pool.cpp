#include "matching_engine/order_pool.hpp"

#include <limits>
#include <stdexcept>

namespace matching_engine {

namespace {

std::size_t validated_capacity(const std::size_t capacity) {
    const auto maximum_capacity = static_cast<std::size_t>(kInvalidOrderIndex);
    if (capacity > maximum_capacity) {
        throw std::length_error("OrderPool capacity exceeds OrderIndex range");
    }
    return capacity;
}

}  // namespace

OrderPool::OrderPool(const std::size_t capacity) : slots_(validated_capacity(capacity)) {

    for (std::size_t index = 0; index < capacity; ++index) {
        const bool has_next_slot = index + 1U < capacity;
        slots_[index].next_free = has_next_slot
            ? static_cast<OrderIndex>(index + 1U)
            : kInvalidOrderIndex;
    }

    free_head_ = capacity == 0U ? kInvalidOrderIndex : 0U;
}

AllocationResult OrderPool::allocate(const OrderRequest& request) noexcept {
    if (validate_order_request(request) != OrderRequestError::None) {
        return {.status = AllocationStatus::InvalidRequest};
    }
    if (free_head_ == kInvalidOrderIndex) {
        return {.status = AllocationStatus::Exhausted};
    }

    const OrderIndex index = free_head_;
    OrderNode& node = slots_[static_cast<std::size_t>(index)];
    free_head_ = node.next_free;

    node = {};
    node.id = request.id;
    node.side = request.side;
    node.type = request.type;
    node.price = request.price;
    node.original_quantity = request.quantity;
    node.remaining_quantity = request.quantity;
    node.in_use = true;

    ++active_count_;
    return {.status = AllocationStatus::Allocated, .index = index};
}

ReleaseStatus OrderPool::release(const OrderIndex index) noexcept {
    if (static_cast<std::size_t>(index) >= slots_.size()) {
        return ReleaseStatus::InvalidIndex;
    }

    OrderNode& node = slots_[static_cast<std::size_t>(index)];
    if (!node.in_use) {
        return ReleaseStatus::DoubleRelease;
    }

    node = {};
    node.next_free = free_head_;
    free_head_ = index;
    --active_count_;
    return ReleaseStatus::Released;
}

bool OrderPool::is_allocated(const OrderIndex index) const noexcept {
    return try_get(index) != nullptr;
}

OrderNode* OrderPool::try_get(const OrderIndex index) noexcept {
    if (static_cast<std::size_t>(index) >= slots_.size()) {
        return nullptr;
    }

    OrderNode& node = slots_[static_cast<std::size_t>(index)];
    return node.in_use ? &node : nullptr;
}

const OrderNode* OrderPool::try_get(const OrderIndex index) const noexcept {
    if (static_cast<std::size_t>(index) >= slots_.size()) {
        return nullptr;
    }

    const OrderNode& node = slots_[static_cast<std::size_t>(index)];
    return node.in_use ? &node : nullptr;
}

std::size_t OrderPool::capacity() const noexcept {
    return slots_.size();
}

std::size_t OrderPool::size() const noexcept {
    return active_count_;
}

bool OrderPool::empty() const noexcept {
    return active_count_ == 0U;
}

bool OrderPool::validate_invariants() const noexcept {
    if (active_count_ > slots_.size()) {
        return false;
    }

    std::size_t free_count = 0;
    OrderIndex index = free_head_;
    while (index != kInvalidOrderIndex) {
        if (static_cast<std::size_t>(index) >= slots_.size() || free_count == slots_.size()) {
            return false;
        }

        const OrderNode& node = slots_[static_cast<std::size_t>(index)];
        if (node.in_use) {
            return false;
        }
        ++free_count;
        index = node.next_free;
    }

    return active_count_ + free_count == slots_.size();
}

}  // namespace matching_engine
