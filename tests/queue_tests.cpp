#include "atomic_spsc_queue.hpp"
#include "simple_spsc_queue.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

namespace
{
    constexpr auto timeout = std::chrono::seconds(2);

    template <class QueueType>
    using queue_value_t = typename QueueType::value_type;

    // A helper function to create sample int / vector<int> values for the tests.
    template <class QueueType>
    queue_value_t<QueueType> make_queue_value(int seed)
    {
        using Value = queue_value_t<QueueType>;
        if constexpr (std::is_same_v<Value, int>)
        {
            return seed;
        }
        else
        {
            return Value{seed, seed + 1, seed + 2};
        }
    }

    using QueueImplementations = ::testing::Types<
        simple_spsc_queue<int>,
        atomic_spsc_queue<int>,
        simple_spsc_queue<std::vector<int>>,
        atomic_spsc_queue<std::vector<int>>>;

    template <class QueueType>
    class SpscQueueTest : public ::testing::Test
    {
    };

    TYPED_TEST_SUITE(SpscQueueTest, QueueImplementations);

    TYPED_TEST(SpscQueueTest, CapacityMustBePositive)
    {
        EXPECT_THROW(TypeParam(0), std::invalid_argument);
    }

    TYPED_TEST(SpscQueueTest, ClosingIsReflectedInClosedMethod)
    {
        TypeParam q(1);
        EXPECT_FALSE(q.closed());
        q.close();
        EXPECT_TRUE(q.closed());
    }

    TYPED_TEST(SpscQueueTest, ReportsConfiguredCapacity)
    {
        TypeParam q(7);
        EXPECT_EQ(q.capacity(), 7U);
    }

    TYPED_TEST(SpscQueueTest, TryPushReturnsFalseWhenFull)
    {
        TypeParam q(2);

        EXPECT_TRUE(q.try_push(make_queue_value<TypeParam>(1)));
        EXPECT_TRUE(q.try_push(make_queue_value<TypeParam>(2)));
        EXPECT_FALSE(q.try_push(make_queue_value<TypeParam>(3)));
    }

    TYPED_TEST(SpscQueueTest, TryPopReturnsNulloptWhenEmpty)
    {
        TypeParam q(2);
        EXPECT_FALSE(q.try_pop().has_value());
    }

    TYPED_TEST(SpscQueueTest, TryPushReturnsFalseAfterClose)
    {
        TypeParam q(1);

        q.close();

        EXPECT_FALSE(q.try_push(make_queue_value<TypeParam>(42)));
    }

    TYPED_TEST(SpscQueueTest, TryPopAllowsDrainingQueueAfterClose)
    {
        TypeParam q(2);

        EXPECT_TRUE(q.try_push(make_queue_value<TypeParam>(1)));
        EXPECT_TRUE(q.try_push(make_queue_value<TypeParam>(2)));

        q.close();

        auto first = q.try_pop();
        auto second = q.try_pop();
        auto empty = q.try_pop();

        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(*first, make_queue_value<TypeParam>(1));
        EXPECT_EQ(*second, make_queue_value<TypeParam>(2));
        EXPECT_FALSE(empty.has_value());
    }

    TYPED_TEST(SpscQueueTest, TryPushTryPopPreservesOrder)
    {
        TypeParam q(3);

        EXPECT_TRUE(q.try_push(make_queue_value<TypeParam>(1)));
        EXPECT_TRUE(q.try_push(make_queue_value<TypeParam>(2)));
        EXPECT_TRUE(q.try_push(make_queue_value<TypeParam>(3)));

        auto first = q.try_pop();
        auto second = q.try_pop();
        auto third = q.try_pop();
        auto empty = q.try_pop();

        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        ASSERT_TRUE(third.has_value());
        EXPECT_EQ(*first, make_queue_value<TypeParam>(1));
        EXPECT_EQ(*second, make_queue_value<TypeParam>(2));
        EXPECT_EQ(*third, make_queue_value<TypeParam>(3));
        EXPECT_FALSE(empty.has_value());
    }

    TYPED_TEST(SpscQueueTest, WrapAroundCapacityHasCorrectResults)
    {
        // Push more items than capacity to force wrap-around of internal indices and verify correct behavior after that.
        TypeParam q(2);

        EXPECT_TRUE(q.try_push(make_queue_value<TypeParam>(1)));
        EXPECT_TRUE(q.try_push(make_queue_value<TypeParam>(2)));

        auto first = q.try_pop();
        EXPECT_TRUE(first.has_value());
        EXPECT_EQ(*first, make_queue_value<TypeParam>(1));

        EXPECT_TRUE(q.try_push(make_queue_value<TypeParam>(3)));

        auto second = q.try_pop();
        auto third = q.try_pop();

        ASSERT_TRUE(second.has_value());
        ASSERT_TRUE(third.has_value());
        EXPECT_EQ(*second, make_queue_value<TypeParam>(2));
        EXPECT_EQ(*third, make_queue_value<TypeParam>(3));
    }

    TYPED_TEST(SpscQueueTest, BlockingPushReturnsFalseAfterClose)
    {
        TypeParam q(1);

        q.close();

        EXPECT_FALSE(q.push(make_queue_value<TypeParam>(42)));
    }

    TYPED_TEST(SpscQueueTest, BlockingPopReturnsNulloptAfterCloseWhenEmpty)
    {
        TypeParam q(1);

        q.close();

        EXPECT_FALSE(q.pop().has_value());
    }

    TYPED_TEST(SpscQueueTest, BlockingPopAllowsDrainingQueueAfterClose)
    {
        TypeParam q(3);

        ASSERT_TRUE(q.push(make_queue_value<TypeParam>(10)));
        ASSERT_TRUE(q.push(make_queue_value<TypeParam>(20)));
        q.close();

        auto first = q.pop();
        auto second = q.pop();
        auto empty = q.pop();

        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(*first, make_queue_value<TypeParam>(10));
        EXPECT_EQ(*second, make_queue_value<TypeParam>(20));
        EXPECT_FALSE(empty.has_value());
    }

    TYPED_TEST(SpscQueueTest, BlockingPushPopPreservesOrder)
    {
        TypeParam q(3);

        EXPECT_TRUE(q.push(make_queue_value<TypeParam>(1)));
        EXPECT_TRUE(q.push(make_queue_value<TypeParam>(2)));
        EXPECT_TRUE(q.push(make_queue_value<TypeParam>(3)));

        auto first = q.pop();
        auto second = q.pop();
        auto third = q.pop();

        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        ASSERT_TRUE(third.has_value());
        EXPECT_EQ(*first, make_queue_value<TypeParam>(1));
        EXPECT_EQ(*second, make_queue_value<TypeParam>(2));
        EXPECT_EQ(*third, make_queue_value<TypeParam>(3));
    }

    TYPED_TEST(SpscQueueTest, BlockingPopUnblocksWhenItemArrivesViaPush)
    {
        TypeParam q(1);

        std::promise<void> started;
        auto started_future = started.get_future();
        auto consumer = std::async(std::launch::async, [&]
                                   {
            started.set_value();
            return q.pop(); });

        ASSERT_EQ(started_future.wait_for(timeout), std::future_status::ready);
        ASSERT_TRUE(q.push(make_queue_value<TypeParam>(42)));
        ASSERT_EQ(consumer.wait_for(timeout), std::future_status::ready);

        auto value = consumer.get();
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(*value, make_queue_value<TypeParam>(42));
    }

    TYPED_TEST(SpscQueueTest, BlockingPopUnblocksWhenItemArrivesViaTryPush)
    {
        TypeParam q(1);

        std::promise<void> started;
        auto started_future = started.get_future();
        auto consumer = std::async(std::launch::async, [&]
                                   {
            started.set_value();
            return q.pop(); });

        ASSERT_EQ(started_future.wait_for(timeout), std::future_status::ready);
        ASSERT_TRUE(q.try_push(make_queue_value<TypeParam>(42)));
        ASSERT_EQ(consumer.wait_for(timeout), std::future_status::ready);

        auto value = consumer.get();
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(*value, make_queue_value<TypeParam>(42));
    }

    TYPED_TEST(SpscQueueTest, BlockingPushUnblocksWhenSpaceAvailableViaPop)
    {
        TypeParam q(1);

        ASSERT_TRUE(q.push(make_queue_value<TypeParam>(1)));

        std::promise<void> started;
        auto started_future = started.get_future();
        auto producer = std::async(std::launch::async, [&]
                                   {
            started.set_value();
            return q.push(make_queue_value<TypeParam>(2)); });

        ASSERT_EQ(started_future.wait_for(timeout), std::future_status::ready);
        auto first = q.pop();
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(*first, make_queue_value<TypeParam>(1));

        ASSERT_EQ(producer.wait_for(timeout), std::future_status::ready);
        EXPECT_TRUE(producer.get());

        auto second = q.pop();
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(*second, make_queue_value<TypeParam>(2));
    }

    TYPED_TEST(SpscQueueTest, BlockingPushUnblocksWhenSpaceAvailableViaTryPop)
    {
        TypeParam q(1);

        ASSERT_TRUE(q.push(make_queue_value<TypeParam>(1)));

        std::promise<void> started;
        auto started_future = started.get_future();
        auto producer = std::async(std::launch::async, [&]
                                   {
            started.set_value();
            return q.push(make_queue_value<TypeParam>(2)); });

        ASSERT_EQ(started_future.wait_for(timeout), std::future_status::ready);
        auto first = q.try_pop();
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(*first, make_queue_value<TypeParam>(1));

        ASSERT_EQ(producer.wait_for(timeout), std::future_status::ready);
        EXPECT_TRUE(producer.get());

        auto second = q.pop();
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(*second, make_queue_value<TypeParam>(2));
    }

    TYPED_TEST(SpscQueueTest, BlockingPopReturnsNulloptAfterCloseDuringWait)
    {
        TypeParam q(1);

        std::promise<void> started;
        auto started_future = started.get_future();
        auto consumer = std::async(std::launch::async, [&]
                                   {
            started.set_value();
            return q.pop(); });

        ASSERT_EQ(started_future.wait_for(timeout), std::future_status::ready);
        q.close();

        ASSERT_EQ(consumer.wait_for(timeout), std::future_status::ready);
        EXPECT_FALSE(consumer.get().has_value());
    }

    TYPED_TEST(SpscQueueTest, BlockingPushReturnsFalseAfterCloseWhenFull)
    {
        TypeParam q(1);

        ASSERT_TRUE(q.push(make_queue_value<TypeParam>(1)));

        std::promise<void> started;
        auto started_future = started.get_future();
        auto producer = std::async(std::launch::async, [&]
                                   {
            started.set_value();
            return q.push(make_queue_value<TypeParam>(2)); });

        ASSERT_EQ(started_future.wait_for(timeout), std::future_status::ready);
        q.close();

        ASSERT_EQ(producer.wait_for(timeout), std::future_status::ready);
        EXPECT_FALSE(producer.get());
    }

    TYPED_TEST(SpscQueueTest, BlockingProducerConsumerFunctionalTest)
    {
        constexpr int item_count = 1000;
        using Value = queue_value_t<TypeParam>;

        TypeParam q(64);
        std::atomic<bool> producer_ok{true};
        std::vector<Value> consumed;
        consumed.reserve(item_count);

        std::jthread producer([&]
                              {
            for (int i = 0; i < item_count; ++i)
            {
                if (!q.push(make_queue_value<TypeParam>(i)))
                {
                    producer_ok.store(false, std::memory_order_relaxed);
                    q.close();
                    return;
                }
            }
            q.close(); });

        std::jthread consumer([&]
                              {
            auto value = q.pop();
            while (value.has_value()) {
                consumed.push_back(*value);
                value = q.pop();
            } });

        producer.join();
        consumer.join();

        ASSERT_TRUE(producer_ok.load(std::memory_order_relaxed));
        ASSERT_EQ(static_cast<int>(consumed.size()), item_count);

        for (int i = 0; i < item_count; ++i)
        {
            EXPECT_EQ(consumed[i], make_queue_value<TypeParam>(i));
        }
    }

    TYPED_TEST(SpscQueueTest, NonblockingProducerConsumerFunctionalTest)
    {
        constexpr int item_count = 1000;
        using Value = queue_value_t<TypeParam>;

        TypeParam q(64);
        std::vector<Value> consumed;
        consumed.reserve(item_count);

        std::jthread producer([&]
                              {
            for (int i = 0; i < item_count; ++i)
            {
                while(!q.try_push(make_queue_value<TypeParam>(i))) {}
            }
            q.close(); });

        std::jthread consumer([&]
                              {
            while (true) {
                auto value = q.try_pop();
                
                if (!value.has_value())
                {
                    if (q.done())
                    {
                        break;
                    }
                    continue;
                }
                consumed.push_back(*value);
            } });

        producer.join();
        consumer.join();

        ASSERT_EQ(static_cast<int>(consumed.size()), item_count);

        for (int i = 0; i < item_count; ++i)
        {
            EXPECT_EQ(consumed[i], make_queue_value<TypeParam>(i));
        }
    }
} // namespace
