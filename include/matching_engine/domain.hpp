#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace matching_engine {

using OrderId = std::uint64_t;
using Price = std::int64_t;
using Quantity = std::uint64_t;
using OrderIndex = std::uint32_t;

inline constexpr OrderIndex kInvalidOrderIndex = std::numeric_limits<OrderIndex>::max();

enum class Side { Buy, Sell };
enum class OrderType { Limit, Market };

struct OrderRequest {
    OrderId id{};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    Price price{};
    Quantity quantity{};
};

enum class OrderRequestError {
    None,
    ZeroOrderId,
    ZeroQuantity,
    NonPositiveLimitPrice,
    MarketOrderHasPrice,
};

[[nodiscard]] constexpr OrderRequestError validate_order_request(const OrderRequest& request) noexcept {
    if (request.id == 0U) {
        return OrderRequestError::ZeroOrderId;
    }
    if (request.quantity == 0U) {
        return OrderRequestError::ZeroQuantity;
    }
    if (request.type == OrderType::Limit && request.price <= 0) {
        return OrderRequestError::NonPositiveLimitPrice;
    }
    if (request.type == OrderType::Market && request.price != 0) {
        return OrderRequestError::MarketOrderHasPrice;
    }
    return OrderRequestError::None;
}

struct OrderNode {
    OrderId id{};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    Price price{};
    Quantity original_quantity{};
    Quantity remaining_quantity{};
    OrderIndex previous{kInvalidOrderIndex};
    OrderIndex next{kInvalidOrderIndex};
    OrderIndex next_free{kInvalidOrderIndex};
    bool in_use{false};
};

struct Trade {
    OrderId maker_order_id{};
    OrderId taker_order_id{};
    Price price{};
    Quantity quantity{};
};

enum class ExecutionStatus {
    Accepted,
    RejectedInvalidRequest,
    RejectedDuplicateOrderId,
    RejectedPoolExhausted,
    RejectedLevelQuantityOverflow,
    RejectedLevelOrderCountOverflow,
};

struct ExecutionReport {
    OrderId order_id{};
    ExecutionStatus status{ExecutionStatus::RejectedInvalidRequest};
    OrderRequestError validation_error{OrderRequestError::None};
    Quantity executed_quantity{};
    Quantity remaining_quantity{};
    bool rested{false};
    std::vector<Trade> trades{};
};

struct PriceLevel {
    Price price{};
    Quantity total_quantity{};
    std::uint32_t order_count{};
    OrderIndex head{kInvalidOrderIndex};
    OrderIndex tail{kInvalidOrderIndex};
};

}  // namespace matching_engine
