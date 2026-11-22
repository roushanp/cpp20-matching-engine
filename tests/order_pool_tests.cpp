#include "matching_engine/order_pool.hpp"

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

matching_engine::OrderRequest limit_order(const matching_engine::OrderId id) {
    return {
        .id = id,
        .side = matching_engine::Side::Buy,
        .type = matching_engine::OrderType::Limit,
        .price = 10'000,
        .quantity = 100,
    };
}

void allocation_test() {
    constexpr const char* test_name = "allocation";
    matching_engine::OrderPool pool{2};

    const auto result = pool.allocate(limit_order(1));

    CHECK(test_name, result.succeeded());
    CHECK(test_name, result.index == 0U);
    CHECK(test_name, pool.capacity() == 2U);
    CHECK(test_name, pool.size() == 1U);
    CHECK(test_name, pool.is_allocated(result.index));

    const auto* const node = pool.try_get(result.index);
    CHECK(test_name, node != nullptr);
    if (node != nullptr) {
        CHECK(test_name, node->id == 1U);
        CHECK(test_name, node->price == 10'000);
        CHECK(test_name, node->remaining_quantity == 100U);
        CHECK(test_name, node->previous == matching_engine::kInvalidOrderIndex);
        CHECK(test_name, node->next == matching_engine::kInvalidOrderIndex);
    }
}

void release_test() {
    constexpr const char* test_name = "release";
    matching_engine::OrderPool pool{1};
    const auto allocation = pool.allocate(limit_order(1));

    const auto result = pool.release(allocation.index);

    CHECK(test_name, result == matching_engine::ReleaseStatus::Released);
    CHECK(test_name, pool.empty());
    CHECK(test_name, !pool.is_allocated(allocation.index));
    CHECK(test_name, pool.try_get(allocation.index) == nullptr);
}

void reallocation_test() {
    constexpr const char* test_name = "reallocation";
    matching_engine::OrderPool pool{1};
    const auto first = pool.allocate(limit_order(1));
    CHECK(test_name, pool.release(first.index) == matching_engine::ReleaseStatus::Released);

    const auto second = pool.allocate(limit_order(2));

    CHECK(test_name, second.succeeded());
    CHECK(test_name, second.index == first.index);
    const auto* const node = pool.try_get(second.index);
    CHECK(test_name, node != nullptr);
    if (node != nullptr) {
        CHECK(test_name, node->id == 2U);
        CHECK(test_name, node->remaining_quantity == 100U);
    }
}

void exhaustion_test() {
    constexpr const char* test_name = "exhaustion";
    matching_engine::OrderPool pool{1};
    const auto first = pool.allocate(limit_order(1));
    const auto second = pool.allocate(limit_order(2));

    CHECK(test_name, first.succeeded());
    CHECK(test_name, second.status == matching_engine::AllocationStatus::Exhausted);
    CHECK(test_name, second.index == matching_engine::kInvalidOrderIndex);
    CHECK(test_name, pool.size() == 1U);
}

void invalid_slot_handling_test() {
    constexpr const char* test_name = "invalid slot handling";
    matching_engine::OrderPool pool{1};

    CHECK(test_name, pool.release(1U) == matching_engine::ReleaseStatus::InvalidIndex);
    CHECK(test_name, pool.release(matching_engine::kInvalidOrderIndex)
        == matching_engine::ReleaseStatus::InvalidIndex);
    CHECK(test_name, !pool.is_allocated(1U));
    CHECK(test_name, pool.try_get(1U) == nullptr);
}

void double_release_test() {
    constexpr const char* test_name = "double release";
    matching_engine::OrderPool pool{1};
    const auto allocation = pool.allocate(limit_order(1));

    CHECK(test_name, pool.release(allocation.index) == matching_engine::ReleaseStatus::Released);
    CHECK(test_name, pool.release(allocation.index) == matching_engine::ReleaseStatus::DoubleRelease);
}

void invalid_request_test() {
    constexpr const char* test_name = "invalid request";
    matching_engine::OrderPool pool{1};
    auto request = limit_order(0);
    const auto result = pool.allocate(request);

    CHECK(test_name, result.status == matching_engine::AllocationStatus::InvalidRequest);
    CHECK(test_name, pool.empty());

    request = limit_order(1);
    request.quantity = 0;
    CHECK(test_name, pool.allocate(request).status == matching_engine::AllocationStatus::InvalidRequest);
}

}  // namespace

int main() {
    allocation_test();
    release_test();
    reallocation_test();
    exhaustion_test();
    invalid_slot_handling_test();
    double_release_test();
    invalid_request_test();

    if (failures != 0) {
        std::cerr << failures << " test check(s) failed\n";
        return 1;
    }

    std::cout << "All order pool tests passed\n";
    return 0;
}
