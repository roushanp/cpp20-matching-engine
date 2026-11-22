#include "matching_engine/matching_engine.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <vector>

namespace {

using namespace matching_engine;

int failures = 0;

void check(const bool condition, const char* const expression, const char* const test_name) {
    if (!condition) {
        std::cerr << test_name << ": check failed: " << expression << '\n';
        ++failures;
    }
}

#define CHECK(test_name, expression) check((expression), #expression, test_name)

OrderRequest limit(const OrderId id, const Side side, const Price price, const Quantity quantity) {
    return {.id = id, .side = side, .type = OrderType::Limit, .price = price, .quantity = quantity};
}

OrderRequest market(const OrderId id, const Side side, const Quantity quantity) {
    return {.id = id, .side = side, .type = OrderType::Market, .price = 0, .quantity = quantity};
}

struct ReferenceOrder {
    OrderId id{};
    Side side{Side::Buy};
    Price price{};
    Quantity quantity{};
};

class ReferenceBook {
public:
    explicit ReferenceBook(const std::size_t capacity) : capacity_(capacity) {}

    ExecutionReport submit(const OrderRequest& request) {
        ExecutionReport report{.order_id = request.id, .remaining_quantity = request.quantity};
        if (validate_order_request(request) != OrderRequestError::None) {
            report.validation_error = validate_order_request(request);
            return report;
        }
        if (contains(request.id)) {
            report.status = ExecutionStatus::RejectedDuplicateOrderId;
            return report;
        }
        if (request.type == OrderType::Limit && orders_.size() == capacity_) {
            report.status = ExecutionStatus::RejectedPoolExhausted;
            return report;
        }

        report.status = ExecutionStatus::Accepted;
        Quantity remaining = request.quantity;
        while (remaining != 0U) {
            const auto maker = best_maker(request);
            if (!maker.has_value()) {
                break;
            }

            ReferenceOrder& resting = orders_[*maker];
            const Quantity traded = std::min(remaining, resting.quantity);
            report.trades.push_back({
                .maker_order_id = resting.id,
                .taker_order_id = request.id,
                .price = resting.price,
                .quantity = traded,
            });
            remaining -= traded;
            resting.quantity -= traded;
            if (resting.quantity == 0U) {
                orders_.erase(orders_.begin() + static_cast<std::ptrdiff_t>(*maker));
            }
        }

        report.executed_quantity = request.quantity - remaining;
        report.remaining_quantity = remaining;
        if (request.type == OrderType::Limit && remaining != 0U) {
            orders_.push_back({.id = request.id, .side = request.side, .price = request.price, .quantity = remaining});
            report.rested = true;
        }
        return report;
    }

    RemoveOrderStatus cancel(const OrderId id) {
        const auto iterator = std::find_if(orders_.begin(), orders_.end(), [id](const ReferenceOrder& order) {
            return order.id == id;
        });
        if (iterator == orders_.end()) {
            return RemoveOrderStatus::UnknownOrderId;
        }
        orders_.erase(iterator);
        return RemoveOrderStatus::Removed;
    }

    [[nodiscard]] OrderBookSnapshot snapshot() const {
        std::map<Price, PriceLevelSnapshot, std::greater<>> bids;
        std::map<Price, PriceLevelSnapshot, std::less<>> asks;
        for (const ReferenceOrder& order : orders_) {
            const auto append_order = [&order](auto& levels) {
                auto [iterator, inserted] = levels.try_emplace(
                    order.price, PriceLevelSnapshot{.price = order.price});
                static_cast<void>(inserted);
                iterator->second.total_quantity += order.quantity;
                iterator->second.order_ids.push_back(order.id);
            };
            if (order.side == Side::Buy) {
                append_order(bids);
            } else {
                append_order(asks);
            }
        }

        OrderBookSnapshot result;
        for (const auto& [price, level] : bids) {
            static_cast<void>(price);
            result.bids.push_back(level);
        }
        for (const auto& [price, level] : asks) {
            static_cast<void>(price);
            result.asks.push_back(level);
        }
        return result;
    }

    [[nodiscard]] const std::vector<ReferenceOrder>& orders() const noexcept { return orders_; }

private:
    [[nodiscard]] bool contains(const OrderId id) const {
        return std::any_of(orders_.begin(), orders_.end(), [id](const ReferenceOrder& order) {
            return order.id == id;
        });
    }

    [[nodiscard]] std::optional<std::size_t> best_maker(const OrderRequest& taker) const {
        std::optional<std::size_t> best;
        for (std::size_t index = 0; index < orders_.size(); ++index) {
            const ReferenceOrder& candidate = orders_[index];
            if (candidate.side == taker.side || !crosses(taker, candidate.price)) {
                continue;
            }
            if (!best.has_value()
                || (taker.side == Side::Buy && candidate.price < orders_[*best].price)
                || (taker.side == Side::Sell && candidate.price > orders_[*best].price)) {
                best = index;
            }
        }
        return best;
    }

    [[nodiscard]] static bool crosses(const OrderRequest& taker, const Price maker_price) {
        return taker.type == OrderType::Market
            || (taker.side == Side::Buy ? taker.price >= maker_price : taker.price <= maker_price);
    }

    std::size_t capacity_{};
    std::vector<ReferenceOrder> orders_;
};

bool same_snapshot(const OrderBookSnapshot& actual, const OrderBookSnapshot& expected) {
    const auto same_side = [](const auto& left, const auto& right) {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            if (left[index].price != right[index].price
                || left[index].total_quantity != right[index].total_quantity
                || left[index].order_ids != right[index].order_ids) {
                return false;
            }
        }
        return true;
    };
    return same_side(actual.bids, expected.bids) && same_side(actual.asks, expected.asks);
}

bool same_report(const ExecutionReport& actual, const ExecutionReport& expected) {
    if (actual.status != expected.status || actual.validation_error != expected.validation_error
        || actual.executed_quantity != expected.executed_quantity
        || actual.remaining_quantity != expected.remaining_quantity || actual.rested != expected.rested
        || actual.trades.size() != expected.trades.size()) {
        return false;
    }
    for (std::size_t index = 0; index < actual.trades.size(); ++index) {
        const Trade& left = actual.trades[index];
        const Trade& right = expected.trades[index];
        if (left.maker_order_id != right.maker_order_id || left.taker_order_id != right.taker_order_id
            || left.price != right.price || left.quantity != right.quantity) {
            return false;
        }
    }
    return true;
}

void check_book(const MatchingEngine& engine, const ReferenceBook& reference, const char* test_name) {
    const OrderBookSnapshot actual = engine.order_book().snapshot();
    const OrderBookSnapshot expected = reference.snapshot();
    CHECK(test_name, engine.order_book().validate_invariants());
    CHECK(test_name, same_snapshot(actual, expected));
    CHECK(test_name, engine.order_book().active_order_count() == reference.orders().size());
    if (expected.bids.empty()) {
        CHECK(test_name, !engine.order_book().best_bid().has_value());
    } else {
        CHECK(test_name, engine.order_book().best_bid() == expected.bids.front().price);
    }
    if (expected.asks.empty()) {
        CHECK(test_name, !engine.order_book().best_ask().has_value());
    } else {
        CHECK(test_name, engine.order_book().best_ask() == expected.asks.front().price);
    }
}

void directed_semantics_test() {
    constexpr const char* test_name = "directed semantics";
    MatchingEngine engine{6};
    ReferenceBook reference{6};
    check_book(engine, reference, test_name);

    for (const OrderRequest request : {
             limit(1, Side::Buy, 100, 10),
             limit(2, Side::Sell, 105, 20),
             limit(3, Side::Sell, 103, 15),
             limit(4, Side::Sell, 103, 5),
             market(5, Side::Buy, 25),
         }) {
        const auto actual = engine.submit(request);
        const auto expected = reference.submit(request);
        CHECK(test_name, same_report(actual, expected));
        check_book(engine, reference, test_name);
    }

    CHECK(test_name, engine.cancel(4) == reference.cancel(4));
    check_book(engine, reference, test_name);
    CHECK(test_name, engine.cancel(3) == reference.cancel(3));
    check_book(engine, reference, test_name);
    CHECK(test_name, engine.cancel(1) == reference.cancel(1));
    check_book(engine, reference, test_name);
}

void empty_and_single_level_test() {
    constexpr const char* test_name = "empty and single levels";
    MatchingEngine engine{3};
    ReferenceBook reference{3};
    check_book(engine, reference, test_name);

    auto actual = engine.submit(limit(1, Side::Buy, 100, 10));
    auto expected = reference.submit(limit(1, Side::Buy, 100, 10));
    CHECK(test_name, same_report(actual, expected));
    check_book(engine, reference, test_name);
    CHECK(test_name, engine.cancel(1) == reference.cancel(1));
    check_book(engine, reference, test_name);

    actual = engine.submit(limit(2, Side::Sell, 101, 10));
    expected = reference.submit(limit(2, Side::Sell, 101, 10));
    CHECK(test_name, same_report(actual, expected));
    check_book(engine, reference, test_name);
}

void cancellation_head_tail_and_levels_test() {
    constexpr const char* test_name = "cancellation head tail and levels";
    MatchingEngine engine{4};
    ReferenceBook reference{4};

    for (const OrderRequest request : {
             limit(1, Side::Buy, 100, 10),
             limit(2, Side::Buy, 100, 20),
             limit(3, Side::Buy, 100, 30),
             limit(4, Side::Buy, 99, 40),
         }) {
        CHECK(test_name, same_report(engine.submit(request), reference.submit(request)));
        check_book(engine, reference, test_name);
    }

    CHECK(test_name, engine.cancel(1) == reference.cancel(1));
    check_book(engine, reference, test_name);
    CHECK(test_name, engine.cancel(3) == reference.cancel(3));
    check_book(engine, reference, test_name);
    CHECK(test_name, engine.cancel(4) == reference.cancel(4));
    check_book(engine, reference, test_name);
    CHECK(test_name, engine.cancel(2) == reference.cancel(2));
    check_book(engine, reference, test_name);
}

void duplicate_pool_and_level_removal_test() {
    constexpr const char* test_name = "duplicate pool and level removal";
    MatchingEngine engine{2};
    ReferenceBook reference{2};

    for (const OrderRequest request : {
             limit(1, Side::Buy, 100, 10),
             limit(2, Side::Buy, 100, 20),
             limit(1, Side::Sell, 101, 10),
             limit(3, Side::Sell, 101, 10),
         }) {
        const auto actual = engine.submit(request);
        const auto expected = reference.submit(request);
        CHECK(test_name, same_report(actual, expected));
        check_book(engine, reference, test_name);
    }

    CHECK(test_name, engine.cancel(1) == reference.cancel(1));
    check_book(engine, reference, test_name);
    CHECK(test_name, engine.cancel(2) == reference.cancel(2));
    check_book(engine, reference, test_name);
}

void randomized_differential_test() {
    constexpr const char* test_name = "randomized differential";
    constexpr std::uint64_t seed = 0xC0FFEE1234ULL;
    constexpr std::size_t event_count = 1'000;
    MatchingEngine engine{16};
    ReferenceBook reference{16};
    std::mt19937_64 random{seed};
    OrderId next_id = 1;

    for (std::size_t event = 0; event < event_count; ++event) {
        const std::uint64_t action = random() % 100U;
        if (action < 75U) {
            OrderId id = next_id++;
            if (action < 10U && !reference.orders().empty()) {
                id = reference.orders()[static_cast<std::size_t>(random() % reference.orders().size())].id;
            }
            const Side side = (random() & 1U) == 0U ? Side::Buy : Side::Sell;
            const Quantity quantity = 1U + (random() % 10U);
            const bool is_market = (random() % 4U) == 0U;
            const OrderRequest request = is_market
                ? market(id, side, quantity)
                : limit(id, side, 95 + static_cast<Price>(random() % 11U), quantity);

            const auto actual = engine.submit(request);
            const auto expected = reference.submit(request);
            CHECK(test_name, same_report(actual, expected));
        } else {
            OrderId id = next_id + static_cast<OrderId>(random() % 100U);
            if (!reference.orders().empty() && (random() % 4U) != 0U) {
                id = reference.orders()[static_cast<std::size_t>(random() % reference.orders().size())].id;
            }
            CHECK(test_name, engine.cancel(id) == reference.cancel(id));
        }
        check_book(engine, reference, test_name);
    }
}

}  // namespace

int main() {
    directed_semantics_test();
    empty_and_single_level_test();
    cancellation_head_tail_and_levels_test();
    duplicate_pool_and_level_removal_test();
    randomized_differential_test();

    if (failures != 0) {
        std::cerr << failures << " test check(s) failed\n";
        return 1;
    }

    std::cout << "All system matching tests passed\n";
    return 0;
}
