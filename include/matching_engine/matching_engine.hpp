#pragma once

#include "matching_engine/domain.hpp"
#include "matching_engine/order_book.hpp"

#include <cstddef>

namespace matching_engine {

class MatchingEngine {
public:
    explicit MatchingEngine(std::size_t maximum_resting_orders);

    [[nodiscard]] ExecutionReport submit(const OrderRequest& request);
    [[nodiscard]] RemoveOrderStatus cancel(OrderId id) noexcept;
    [[nodiscard]] const OrderBook& order_book() const noexcept;

private:
    [[nodiscard]] static bool crosses(const OrderRequest& taker, Price maker_price) noexcept;
    [[nodiscard]] static ExecutionStatus execution_status_from(AddOrderStatus status) noexcept;

    OrderBook order_book_;
};

}  // namespace matching_engine
