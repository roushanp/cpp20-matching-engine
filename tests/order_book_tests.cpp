#include "matching_engine/order_book.hpp"

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
    const matching_engine::Quantity quantity = 100) {
    return {
        .id = id,
        .side = side,
        .type = matching_engine::OrderType::Limit,
        .price = price,
        .quantity = quantity,
    };
}

void add_and_best_price_test() {
    constexpr const char* test_name = "add and best price";
    matching_engine::OrderBook book{6};

    CHECK(test_name, book.add_resting_order(limit_order(1, matching_engine::Side::Buy, 100)).succeeded());
    CHECK(test_name, book.add_resting_order(limit_order(2, matching_engine::Side::Buy, 102)).succeeded());
    CHECK(test_name, book.add_resting_order(limit_order(3, matching_engine::Side::Sell, 105)).succeeded());
    CHECK(test_name, book.add_resting_order(limit_order(4, matching_engine::Side::Sell, 103)).succeeded());

    CHECK(test_name, book.best_bid().has_value());
    CHECK(test_name, book.best_bid() == 102);
    CHECK(test_name, book.best_ask().has_value());
    CHECK(test_name, book.best_ask() == 103);

    const auto snapshot = book.snapshot();
    CHECK(test_name, snapshot.bids.size() == 2U);
    CHECK(test_name, snapshot.asks.size() == 2U);
    CHECK(test_name, snapshot.bids[0].price == 102);
    CHECK(test_name, snapshot.bids[1].price == 100);
    CHECK(test_name, snapshot.asks[0].price == 103);
    CHECK(test_name, snapshot.asks[1].price == 105);
}

void fifo_and_middle_removal_test() {
    constexpr const char* test_name = "fifo and middle removal";
    matching_engine::OrderBook book{3};

    CHECK(test_name, book.add_resting_order(limit_order(1, matching_engine::Side::Buy, 100)).succeeded());
    CHECK(test_name, book.add_resting_order(limit_order(2, matching_engine::Side::Buy, 100)).succeeded());
    CHECK(test_name, book.add_resting_order(limit_order(3, matching_engine::Side::Buy, 100)).succeeded());

    auto snapshot = book.snapshot();
    CHECK(test_name, snapshot.bids[0].order_ids.size() == 3U);
    CHECK(test_name, snapshot.bids[0].order_ids[0] == 1U);
    CHECK(test_name, snapshot.bids[0].order_ids[1] == 2U);
    CHECK(test_name, snapshot.bids[0].order_ids[2] == 3U);
    CHECK(test_name, snapshot.bids[0].total_quantity == 300U);

    CHECK(test_name, book.remove_order(2) == matching_engine::RemoveOrderStatus::Removed);
    snapshot = book.snapshot();
    CHECK(test_name, snapshot.bids[0].order_ids.size() == 2U);
    CHECK(test_name, snapshot.bids[0].order_ids[0] == 1U);
    CHECK(test_name, snapshot.bids[0].order_ids[1] == 3U);
    CHECK(test_name, snapshot.bids[0].total_quantity == 200U);
}

void empty_level_removal_test() {
    constexpr const char* test_name = "empty level removal";
    matching_engine::OrderBook book{2};

    CHECK(test_name, book.add_resting_order(limit_order(1, matching_engine::Side::Buy, 101)).succeeded());
    CHECK(test_name, book.add_resting_order(limit_order(2, matching_engine::Side::Buy, 100)).succeeded());
    CHECK(test_name, book.remove_order(1) == matching_engine::RemoveOrderStatus::Removed);
    CHECK(test_name, book.best_bid() == 100);
    CHECK(test_name, book.remove_order(2) == matching_engine::RemoveOrderStatus::Removed);
    CHECK(test_name, !book.best_bid().has_value());
    CHECK(test_name, book.snapshot().bids.empty());
}

void lookup_and_pool_reuse_test() {
    constexpr const char* test_name = "lookup and pool reuse";
    matching_engine::OrderBook book{1};
    const auto first = book.add_resting_order(limit_order(1, matching_engine::Side::Buy, 100));

    CHECK(test_name, first.succeeded());
    CHECK(test_name, book.add_resting_order(limit_order(1, matching_engine::Side::Buy, 100)).status
        == matching_engine::AddOrderStatus::DuplicateOrderId);
    CHECK(test_name, book.add_resting_order(limit_order(2, matching_engine::Side::Buy, 100)).status
        == matching_engine::AddOrderStatus::PoolExhausted);
    CHECK(test_name, book.active_order_count() == 1U);
    CHECK(test_name, book.remove_order(1) == matching_engine::RemoveOrderStatus::Removed);
    CHECK(test_name, book.remove_order(1) == matching_engine::RemoveOrderStatus::UnknownOrderId);

    const auto second = book.add_resting_order(limit_order(2, matching_engine::Side::Sell, 101));
    CHECK(test_name, second.succeeded());
    CHECK(test_name, second.index == first.index);
    CHECK(test_name, book.active_order_count() == 1U);
}

void invalid_and_market_orders_test() {
    constexpr const char* test_name = "invalid and market orders";
    matching_engine::OrderBook book{2};
    auto invalid = limit_order(0, matching_engine::Side::Buy, 100);

    CHECK(test_name, book.add_resting_order(invalid).status == matching_engine::AddOrderStatus::InvalidRequest);
    invalid = limit_order(1, matching_engine::Side::Buy, 100);
    invalid.quantity = 0;
    CHECK(test_name, book.add_resting_order(invalid).status == matching_engine::AddOrderStatus::InvalidRequest);

    const matching_engine::OrderRequest market_order{
        .id = 2,
        .side = matching_engine::Side::Buy,
        .type = matching_engine::OrderType::Market,
        .price = 0,
        .quantity = 100,
    };
    CHECK(test_name, book.add_resting_order(market_order).status
        == matching_engine::AddOrderStatus::MarketOrderCannotRest);
    CHECK(test_name, book.active_order_count() == 0U);
}

}  // namespace

int main() {
    add_and_best_price_test();
    fifo_and_middle_removal_test();
    empty_level_removal_test();
    lookup_and_pool_reuse_test();
    invalid_and_market_orders_test();

    if (failures != 0) {
        std::cerr << failures << " test check(s) failed\n";
        return 1;
    }

    std::cout << "All order book tests passed\n";
    return 0;
}
