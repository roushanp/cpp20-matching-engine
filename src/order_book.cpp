#include "matching_engine/order_book.hpp"

#include <cassert>
#include <limits>
#include <utility>

namespace matching_engine {

namespace {

template <typename Levels>
bool validate_levels(
    const Levels& levels,
    const Side expected_side,
    const OrderPool& pool) noexcept {
    for (const auto& [price, level] : levels) {
        if (level.price != price || level.order_count == 0U) {
            return false;
        }

        Quantity calculated_quantity = 0;
        std::uint32_t calculated_count = 0;
        OrderIndex expected_previous = kInvalidOrderIndex;
        OrderIndex index = level.head;

        while (index != kInvalidOrderIndex) {
            const OrderNode* const node = pool.try_get(index);
            if (node == nullptr || node->side != expected_side || node->price != price
                || node->remaining_quantity == 0U || node->previous != expected_previous
                || calculated_count == level.order_count) {
                return false;
            }
            if (node->remaining_quantity > std::numeric_limits<Quantity>::max() - calculated_quantity) {
                return false;
            }

            calculated_quantity += node->remaining_quantity;
            ++calculated_count;
            expected_previous = index;
            index = node->next;
        }

        if (calculated_count != level.order_count || calculated_quantity != level.total_quantity
            || expected_previous != level.tail) {
            return false;
        }
    }

    return true;
}

template <typename Levels>
std::size_t occurrences_of(
    const Levels& levels,
    const OrderPool& pool,
    const OrderIndex sought_index) noexcept {
    std::size_t occurrences = 0;
    for (const auto& [price, level] : levels) {
        static_cast<void>(price);
        OrderIndex index = level.head;
        while (index != kInvalidOrderIndex) {
            if (index == sought_index) {
                ++occurrences;
            }
            const OrderNode* const node = pool.try_get(index);
            if (node == nullptr) {
                return 0;
            }
            index = node->next;
        }
    }
    return occurrences;
}

}  // namespace

OrderBook::OrderBook(const std::size_t maximum_orders) : pool_(maximum_orders) {
    order_lookup_.reserve(maximum_orders);
}

AddOrderStatus OrderBook::can_add_resting_order(const OrderRequest& request) const noexcept {
    if (validate_order_request(request) != OrderRequestError::None) {
        return AddOrderStatus::InvalidRequest;
    }
    if (request.type != OrderType::Limit) {
        return AddOrderStatus::MarketOrderCannotRest;
    }
    if (order_lookup_.contains(request.id)) {
        return AddOrderStatus::DuplicateOrderId;
    }
    if (pool_.size() == pool_.capacity()) {
        return AddOrderStatus::PoolExhausted;
    }

    const PriceLevel* const existing_level = find_level(request.side, request.price);
    if (existing_level != nullptr) {
        if (request.quantity > std::numeric_limits<Quantity>::max() - existing_level->total_quantity) {
            return AddOrderStatus::LevelQuantityOverflow;
        }
        if (existing_level->order_count == std::numeric_limits<std::uint32_t>::max()) {
            return AddOrderStatus::LevelOrderCountOverflow;
        }
    }

    return AddOrderStatus::Added;
}

AddOrderResult OrderBook::add_resting_order(const OrderRequest& request) {
    const AddOrderStatus admission_status = can_add_resting_order(request);
    if (admission_status != AddOrderStatus::Added) {
        return {.status = admission_status};
    }

    const AllocationResult allocation = pool_.allocate(request);
    if (allocation.status == AllocationStatus::InvalidRequest) {
        return {.status = AddOrderStatus::InvalidRequest};
    }
    if (allocation.status == AllocationStatus::Exhausted) {
        return {.status = AddOrderStatus::PoolExhausted};
    }

    try {
        order_lookup_.emplace(request.id, allocation.index);
    } catch (...) {
        const ReleaseStatus release_status = pool_.release(allocation.index);
        assert(release_status == ReleaseStatus::Released);
        static_cast<void>(release_status);
        throw;
    }

    try {
        if (request.side == Side::Buy) {
            auto [level_iterator, inserted] = bids_.try_emplace(
                request.price, PriceLevel{.price = request.price});
            static_cast<void>(inserted);
            append_to_level(level_iterator->second, allocation.index);
        } else {
            auto [level_iterator, inserted] = asks_.try_emplace(
                request.price, PriceLevel{.price = request.price});
            static_cast<void>(inserted);
            append_to_level(level_iterator->second, allocation.index);
        }
    } catch (...) {
        order_lookup_.erase(request.id);
        const ReleaseStatus release_status = pool_.release(allocation.index);
        assert(release_status == ReleaseStatus::Released);
        static_cast<void>(release_status);
        throw;
    }

    return {.status = AddOrderStatus::Added, .index = allocation.index};
}

RemoveOrderStatus OrderBook::remove_order(const OrderId id) noexcept {
    const auto lookup_iterator = order_lookup_.find(id);
    if (lookup_iterator == order_lookup_.end()) {
        return RemoveOrderStatus::UnknownOrderId;
    }

    const OrderIndex index = lookup_iterator->second;
    OrderNode* const node = pool_.try_get(index);
    assert(node != nullptr);

    PriceLevel* const level = find_level(node->side, node->price);
    assert(level != nullptr);
    unlink_from_level(*level, index);

    const Side side = node->side;
    const Price price = node->price;
    order_lookup_.erase(lookup_iterator);

    const ReleaseStatus release_status = pool_.release(index);
    assert(release_status == ReleaseStatus::Released);
    static_cast<void>(release_status);
    erase_level_if_empty(side, price);

    return RemoveOrderStatus::Removed;
}

ConsumeOrderResult OrderBook::consume_order(const OrderIndex index, const Quantity quantity) noexcept {
    OrderNode* const node = pool_.try_get(index);
    if (node == nullptr) {
        return {.status = ConsumeOrderStatus::InvalidOrderIndex};
    }
    if (quantity == 0U || quantity > node->remaining_quantity) {
        return {.status = ConsumeOrderStatus::InvalidQuantity};
    }

    PriceLevel* const level = find_level(node->side, node->price);
    assert(level != nullptr);
    level->total_quantity -= quantity;
    node->remaining_quantity -= quantity;

    if (node->remaining_quantity != 0U) {
        return {.status = ConsumeOrderStatus::Consumed, .removed = false};
    }

    const OrderId id = node->id;
    const RemoveOrderStatus remove_status = remove_order(id);
    assert(remove_status == RemoveOrderStatus::Removed);
    static_cast<void>(remove_status);
    return {.status = ConsumeOrderStatus::Consumed, .removed = true};
}

bool OrderBook::contains(const OrderId id) const noexcept {
    return order_lookup_.contains(id);
}

std::optional<Price> OrderBook::best_bid() const noexcept {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const noexcept {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

std::optional<RestingOrder> OrderBook::best_resting_order(const Side side) const noexcept {
    const PriceLevel* level = nullptr;
    if (side == Side::Buy) {
        if (bids_.empty()) {
            return std::nullopt;
        }
        level = &bids_.begin()->second;
    } else {
        if (asks_.empty()) {
            return std::nullopt;
        }
        level = &asks_.begin()->second;
    }

    assert(level->head != kInvalidOrderIndex);
    const OrderNode* const node = pool_.try_get(level->head);
    assert(node != nullptr);
    return RestingOrder{
        .id = node->id,
        .side = node->side,
        .price = node->price,
        .remaining_quantity = node->remaining_quantity,
        .index = level->head,
    };
}

std::size_t OrderBook::active_order_count() const noexcept {
    return order_lookup_.size();
}

std::size_t OrderBook::capacity() const noexcept {
    return pool_.capacity();
}

OrderBookSnapshot OrderBook::snapshot() const {
    OrderBookSnapshot book_snapshot;
    book_snapshot.bids.reserve(bids_.size());
    book_snapshot.asks.reserve(asks_.size());

    for (const auto& [price, level] : bids_) {
        static_cast<void>(price);
        book_snapshot.bids.push_back(snapshot_level(level));
    }
    for (const auto& [price, level] : asks_) {
        static_cast<void>(price);
        book_snapshot.asks.push_back(snapshot_level(level));
    }

    return book_snapshot;
}

bool OrderBook::validate_invariants() const noexcept {
    if (!pool_.validate_invariants() || !validate_levels(bids_, Side::Buy, pool_)
        || !validate_levels(asks_, Side::Sell, pool_)) {
        return false;
    }

    std::size_t active_pool_nodes = 0;
    for (std::size_t raw_index = 0; raw_index < pool_.capacity(); ++raw_index) {
        const OrderIndex index = static_cast<OrderIndex>(raw_index);
        const OrderNode* const node = pool_.try_get(index);
        if (node == nullptr) {
            continue;
        }

        ++active_pool_nodes;
        const auto lookup_iterator = order_lookup_.find(node->id);
        if (lookup_iterator == order_lookup_.end() || lookup_iterator->second != index) {
            return false;
        }
        if (occurrences_of(bids_, pool_, index) + occurrences_of(asks_, pool_, index) != 1U) {
            return false;
        }
    }

    return active_pool_nodes == pool_.size() && active_pool_nodes == order_lookup_.size();
}

PriceLevel* OrderBook::find_level(const Side side, const Price price) noexcept {
    if (side == Side::Buy) {
        const auto iterator = bids_.find(price);
        return iterator == bids_.end() ? nullptr : &iterator->second;
    }

    const auto iterator = asks_.find(price);
    return iterator == asks_.end() ? nullptr : &iterator->second;
}

const PriceLevel* OrderBook::find_level(const Side side, const Price price) const noexcept {
    if (side == Side::Buy) {
        const auto iterator = bids_.find(price);
        return iterator == bids_.end() ? nullptr : &iterator->second;
    }

    const auto iterator = asks_.find(price);
    return iterator == asks_.end() ? nullptr : &iterator->second;
}

void OrderBook::append_to_level(PriceLevel& level, const OrderIndex index) noexcept {
    OrderNode* const node = pool_.try_get(index);
    assert(node != nullptr);
    assert(node->previous == kInvalidOrderIndex);
    assert(node->next == kInvalidOrderIndex);

    node->previous = level.tail;
    if (level.tail == kInvalidOrderIndex) {
        level.head = index;
    } else {
        OrderNode* const tail = pool_.try_get(level.tail);
        assert(tail != nullptr);
        tail->next = index;
    }
    level.tail = index;
    level.total_quantity += node->remaining_quantity;
    ++level.order_count;
}

void OrderBook::unlink_from_level(PriceLevel& level, const OrderIndex index) noexcept {
    OrderNode* const node = pool_.try_get(index);
    assert(node != nullptr);

    if (node->previous == kInvalidOrderIndex) {
        level.head = node->next;
    } else {
        OrderNode* const previous = pool_.try_get(node->previous);
        assert(previous != nullptr);
        previous->next = node->next;
    }

    if (node->next == kInvalidOrderIndex) {
        level.tail = node->previous;
    } else {
        OrderNode* const next = pool_.try_get(node->next);
        assert(next != nullptr);
        next->previous = node->previous;
    }

    level.total_quantity -= node->remaining_quantity;
    --level.order_count;
    node->previous = kInvalidOrderIndex;
    node->next = kInvalidOrderIndex;
}

void OrderBook::erase_level_if_empty(const Side side, const Price price) noexcept {
    if (side == Side::Buy) {
        const auto iterator = bids_.find(price);
        if (iterator != bids_.end() && iterator->second.order_count == 0U) {
            bids_.erase(iterator);
        }
        return;
    }

    const auto iterator = asks_.find(price);
    if (iterator != asks_.end() && iterator->second.order_count == 0U) {
        asks_.erase(iterator);
    }
}

PriceLevelSnapshot OrderBook::snapshot_level(const PriceLevel& level) const {
    PriceLevelSnapshot level_snapshot{
        .price = level.price,
        .total_quantity = level.total_quantity,
    };
    level_snapshot.order_ids.reserve(level.order_count);

    OrderIndex index = level.head;
    while (index != kInvalidOrderIndex) {
        const OrderNode* const node = pool_.try_get(index);
        assert(node != nullptr);
        level_snapshot.order_ids.push_back(node->id);
        index = node->next;
    }

    assert(level_snapshot.order_ids.size() == level.order_count);
    return level_snapshot;
}

}  // namespace matching_engine
