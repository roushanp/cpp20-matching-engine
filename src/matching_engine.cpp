#include "matching_engine/matching_engine.hpp"

#include <algorithm>
#include <cassert>

namespace matching_engine {

MatchingEngine::MatchingEngine(const std::size_t maximum_resting_orders)
    : order_book_(maximum_resting_orders) {}

ExecutionReport MatchingEngine::submit(const OrderRequest& request) {
    ExecutionReport report{
        .order_id = request.id,
        .validation_error = validate_order_request(request),
        .remaining_quantity = request.quantity,
    };
    if (report.validation_error != OrderRequestError::None) {
        return report;
    }
    if (order_book_.contains(request.id)) {
        report.status = ExecutionStatus::RejectedDuplicateOrderId;
        return report;
    }

    if (request.type == OrderType::Limit) {
        const AddOrderStatus admission_status = order_book_.can_add_resting_order(request);
        if (admission_status != AddOrderStatus::Added) {
            report.status = execution_status_from(admission_status);
            return report;
        }
    }

    report.status = ExecutionStatus::Accepted;
    Quantity remaining_quantity = request.quantity;
    const Side maker_side = request.side == Side::Buy ? Side::Sell : Side::Buy;

    while (remaining_quantity != 0U) {
        const std::optional<RestingOrder> maker = order_book_.best_resting_order(maker_side);
        if (!maker.has_value() || !crosses(request, maker->price)) {
            break;
        }

        const Quantity trade_quantity = std::min(remaining_quantity, maker->remaining_quantity);
        report.trades.push_back(Trade{
            .maker_order_id = maker->id,
            .taker_order_id = request.id,
            .price = maker->price,
            .quantity = trade_quantity,
        });

        const ConsumeOrderResult consume_result = order_book_.consume_order(maker->index, trade_quantity);
        assert(consume_result.status == ConsumeOrderStatus::Consumed);
        static_cast<void>(consume_result);
        remaining_quantity -= trade_quantity;
    }

    report.executed_quantity = request.quantity - remaining_quantity;
    report.remaining_quantity = remaining_quantity;

    if (request.type == OrderType::Limit && remaining_quantity != 0U) {
        OrderRequest resting_request = request;
        resting_request.quantity = remaining_quantity;
        const AddOrderResult add_result = order_book_.add_resting_order(resting_request);
        assert(add_result.status == AddOrderStatus::Added);
        static_cast<void>(add_result);
        report.rested = true;
    }

    return report;
}

RemoveOrderStatus MatchingEngine::cancel(const OrderId id) noexcept {
    return order_book_.remove_order(id);
}

const OrderBook& MatchingEngine::order_book() const noexcept {
    return order_book_;
}

bool MatchingEngine::crosses(const OrderRequest& taker, const Price maker_price) noexcept {
    if (taker.type == OrderType::Market) {
        return true;
    }
    return taker.side == Side::Buy ? taker.price >= maker_price : taker.price <= maker_price;
}

ExecutionStatus MatchingEngine::execution_status_from(const AddOrderStatus status) noexcept {
    switch (status) {
    case AddOrderStatus::PoolExhausted:
        return ExecutionStatus::RejectedPoolExhausted;
    case AddOrderStatus::LevelQuantityOverflow:
        return ExecutionStatus::RejectedLevelQuantityOverflow;
    case AddOrderStatus::LevelOrderCountOverflow:
        return ExecutionStatus::RejectedLevelOrderCountOverflow;
    case AddOrderStatus::DuplicateOrderId:
        return ExecutionStatus::RejectedDuplicateOrderId;
    case AddOrderStatus::Added:
    case AddOrderStatus::InvalidRequest:
    case AddOrderStatus::MarketOrderCannotRest:
        return ExecutionStatus::RejectedInvalidRequest;
    }

    return ExecutionStatus::RejectedInvalidRequest;
}

}  // namespace matching_engine
