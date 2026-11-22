#include "matching_engine/matching_engine.hpp"

#include <iostream>

namespace {

int failures = 0;

void check(const bool condition, const char* const expression, const char* const test_name) {
    if (!condition) {
        std::cerr << test_name << ": check failed: " << expression << '\n';
        ++failures;
    }
}

#define CHECK(test_name, expression) check((expression), #expression, test_name)

matching_engine::OrderRequest limit_order(
    const matching_engine::OrderId id,
    const matching_engine::Side side,
    const matching_engine::Price price,
    const matching_engine::Quantity quantity) {
    return {
        .id = id,
        .side = side,
        .type = matching_engine::OrderType::Limit,
        .price = price,
        .quantity = quantity,
    };
}

matching_engine::OrderRequest market_order(
    const matching_engine::OrderId id,
    const matching_engine::Side side,
    const matching_engine::Quantity quantity) {
    return {
        .id = id,
        .side = side,
        .type = matching_engine::OrderType::Market,
        .price = 0,
        .quantity = quantity,
    };
}

void partial_limit_execution_test() {
    constexpr const char* test_name = "partial limit execution";
    matching_engine::MatchingEngine engine{4};

    CHECK(test_name, engine.submit(limit_order(1, matching_engine::Side::Sell, 100, 100)).rested);
    const auto report = engine.submit(limit_order(2, matching_engine::Side::Buy, 105, 40));

    CHECK(test_name, report.status == matching_engine::ExecutionStatus::Accepted);
    CHECK(test_name, report.executed_quantity == 40U);
    CHECK(test_name, report.remaining_quantity == 0U);
    CHECK(test_name, !report.rested);
    CHECK(test_name, report.trades.size() == 1U);
    CHECK(test_name, report.trades[0].maker_order_id == 1U);
    CHECK(test_name, report.trades[0].taker_order_id == 2U);
    CHECK(test_name, report.trades[0].price == 100);
    CHECK(test_name, report.trades[0].quantity == 40U);

    const auto snapshot = engine.order_book().snapshot();
    CHECK(test_name, snapshot.asks.size() == 1U);
    CHECK(test_name, snapshot.asks[0].total_quantity == 60U);
    CHECK(test_name, snapshot.asks[0].order_ids[0] == 1U);
}

void multiple_levels_and_fifo_test() {
    constexpr const char* test_name = "multiple levels and fifo";
    matching_engine::MatchingEngine engine{5};

    CHECK(test_name, engine.submit(limit_order(1, matching_engine::Side::Sell, 100, 30)).rested);
    CHECK(test_name, engine.submit(limit_order(2, matching_engine::Side::Sell, 100, 20)).rested);
    CHECK(test_name, engine.submit(limit_order(3, matching_engine::Side::Sell, 101, 50)).rested);
    const auto report = engine.submit(market_order(4, matching_engine::Side::Buy, 70));

    CHECK(test_name, report.status == matching_engine::ExecutionStatus::Accepted);
    CHECK(test_name, report.executed_quantity == 70U);
    CHECK(test_name, report.remaining_quantity == 0U);
    CHECK(test_name, !report.rested);
    CHECK(test_name, report.trades.size() == 3U);
    CHECK(test_name, report.trades[0].maker_order_id == 1U);
    CHECK(test_name, report.trades[0].price == 100);
    CHECK(test_name, report.trades[0].quantity == 30U);
    CHECK(test_name, report.trades[1].maker_order_id == 2U);
    CHECK(test_name, report.trades[1].price == 100);
    CHECK(test_name, report.trades[1].quantity == 20U);
    CHECK(test_name, report.trades[2].maker_order_id == 3U);
    CHECK(test_name, report.trades[2].price == 101);
    CHECK(test_name, report.trades[2].quantity == 20U);

    const auto snapshot = engine.order_book().snapshot();
    CHECK(test_name, snapshot.asks.size() == 1U);
    CHECK(test_name, snapshot.asks[0].price == 101);
    CHECK(test_name, snapshot.asks[0].total_quantity == 30U);
    CHECK(test_name, snapshot.asks[0].order_ids.size() == 1U);
    CHECK(test_name, snapshot.asks[0].order_ids[0] == 3U);
}

void limit_sell_execution_test() {
    constexpr const char* test_name = "limit sell execution";
    matching_engine::MatchingEngine engine{2};

    CHECK(test_name, engine.submit(limit_order(1, matching_engine::Side::Buy, 101, 60)).rested);
    const auto report = engine.submit(limit_order(2, matching_engine::Side::Sell, 100, 60));

    CHECK(test_name, report.status == matching_engine::ExecutionStatus::Accepted);
    CHECK(test_name, report.executed_quantity == 60U);
    CHECK(test_name, report.remaining_quantity == 0U);
    CHECK(test_name, report.trades.size() == 1U);
    CHECK(test_name, report.trades[0].maker_order_id == 1U);
    CHECK(test_name, report.trades[0].taker_order_id == 2U);
    CHECK(test_name, report.trades[0].price == 101);
    CHECK(test_name, engine.order_book().snapshot().bids.empty());
}

void rest_discard_and_cancel_test() {
    constexpr const char* test_name = "rest discard and cancel";
    matching_engine::MatchingEngine engine{2};

    const auto resting = engine.submit(limit_order(1, matching_engine::Side::Buy, 99, 50));
    CHECK(test_name, resting.rested);
    const auto market_report = engine.submit(market_order(2, matching_engine::Side::Sell, 80));
    CHECK(test_name, market_report.executed_quantity == 50U);
    CHECK(test_name, market_report.remaining_quantity == 30U);
    CHECK(test_name, !market_report.rested);
    CHECK(test_name, engine.order_book().snapshot().bids.empty());

    CHECK(test_name, engine.submit(limit_order(3, matching_engine::Side::Buy, 98, 10)).rested);
    CHECK(test_name, engine.cancel(3) == matching_engine::RemoveOrderStatus::Removed);
    CHECK(test_name, engine.cancel(3) == matching_engine::RemoveOrderStatus::UnknownOrderId);
    CHECK(test_name, engine.order_book().snapshot().bids.empty());
}

void rejection_and_capacity_test() {
    constexpr const char* test_name = "rejection and capacity";
    matching_engine::MatchingEngine engine{1};

    CHECK(test_name, engine.submit(limit_order(1, matching_engine::Side::Buy, 100, 10)).rested);
    CHECK(test_name, engine.submit(limit_order(1, matching_engine::Side::Buy, 100, 10)).status
        == matching_engine::ExecutionStatus::RejectedDuplicateOrderId);
    CHECK(test_name, engine.submit(limit_order(2, matching_engine::Side::Sell, 101, 10)).status
        == matching_engine::ExecutionStatus::RejectedPoolExhausted);

    auto invalid = limit_order(3, matching_engine::Side::Buy, 100, 0);
    CHECK(test_name, engine.submit(invalid).status == matching_engine::ExecutionStatus::RejectedInvalidRequest);
    CHECK(test_name, engine.cancel(1) == matching_engine::RemoveOrderStatus::Removed);
    CHECK(test_name, engine.submit(limit_order(2, matching_engine::Side::Sell, 101, 10)).rested);
}

}  // namespace

int main() {
    partial_limit_execution_test();
    multiple_levels_and_fifo_test();
    limit_sell_execution_test();
    rest_discard_and_cancel_test();
    rejection_and_capacity_test();

    if (failures != 0) {
        std::cerr << failures << " test check(s) failed\n";
        return 1;
    }

    std::cout << "All matching engine tests passed\n";
    return 0;
}
