#pragma once

#include "matching_engine/domain.hpp"
#include "matching_engine/order_pool.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace matching_engine {

enum class AddOrderStatus {
    Added,
    InvalidRequest,
    MarketOrderCannotRest,
    DuplicateOrderId,
    PoolExhausted,
    LevelQuantityOverflow,
    LevelOrderCountOverflow,
};

struct AddOrderResult {
    AddOrderStatus status{AddOrderStatus::InvalidRequest};
    OrderIndex index{kInvalidOrderIndex};

    [[nodiscard]] constexpr bool succeeded() const noexcept {
        return status == AddOrderStatus::Added;
    }
};

enum class RemoveOrderStatus {
    Removed,
    UnknownOrderId,
};

struct PriceLevelSnapshot {
    Price price{};
    Quantity total_quantity{};
    std::vector<OrderId> order_ids{};
};

struct OrderBookSnapshot {
    std::vector<PriceLevelSnapshot> bids{};
    std::vector<PriceLevelSnapshot> asks{};
};

struct RestingOrder {
    OrderId id{};
    Side side{Side::Buy};
    Price price{};
    Quantity remaining_quantity{};
    OrderIndex index{kInvalidOrderIndex};
};

enum class ConsumeOrderStatus {
    Consumed,
    InvalidOrderIndex,
    InvalidQuantity,
};

struct ConsumeOrderResult {
    ConsumeOrderStatus status{ConsumeOrderStatus::InvalidOrderIndex};
    bool removed{false};
};

class OrderBook {
public:
    explicit OrderBook(std::size_t maximum_orders);

    [[nodiscard]] AddOrderStatus can_add_resting_order(const OrderRequest& request) const noexcept;
    [[nodiscard]] AddOrderResult add_resting_order(const OrderRequest& request);
    [[nodiscard]] RemoveOrderStatus remove_order(OrderId id) noexcept;
    [[nodiscard]] ConsumeOrderResult consume_order(OrderIndex index, Quantity quantity) noexcept;

    [[nodiscard]] bool contains(OrderId id) const noexcept;
    [[nodiscard]] std::optional<Price> best_bid() const noexcept;
    [[nodiscard]] std::optional<Price> best_ask() const noexcept;
    [[nodiscard]] std::optional<RestingOrder> best_resting_order(Side side) const noexcept;
    [[nodiscard]] std::size_t active_order_count() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] OrderBookSnapshot snapshot() const;

private:
    using BidLevels = std::map<Price, PriceLevel, std::greater<>>;
    using AskLevels = std::map<Price, PriceLevel, std::less<>>;

    [[nodiscard]] PriceLevel* find_level(Side side, Price price) noexcept;
    [[nodiscard]] const PriceLevel* find_level(Side side, Price price) const noexcept;
    void append_to_level(PriceLevel& level, OrderIndex index) noexcept;
    void unlink_from_level(PriceLevel& level, OrderIndex index) noexcept;
    void erase_level_if_empty(Side side, Price price) noexcept;
    [[nodiscard]] PriceLevelSnapshot snapshot_level(const PriceLevel& level) const;

    OrderPool pool_;
    BidLevels bids_;
    AskLevels asks_;
    std::unordered_map<OrderId, OrderIndex> order_lookup_;
};

}  // namespace matching_engine
