//
// Copyright (c) 2026 Ivica Siladic, Bruno Iljazovic, Korina Simicevic
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "test_common/message_exchange.hpp"
#include "test_common/packet_util.hpp"
#include "test_common/test_stream.hpp"

#include <boost/mqtt5/mqtt_client.hpp>
#include <boost/mqtt5/types.hpp>

#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/bind_immediate_executor.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace boost::mqtt5;

BOOST_AUTO_TEST_SUITE(allocators)

template <typename T>
class counting_allocator {
    template <typename U>
    friend class counting_allocator;

    int* _num_allocations;

public:
    using value_type = T;

    explicit counting_allocator(int& num_allocations) noexcept :
        _num_allocations(&num_allocations)
    {}

    template <typename U>
    counting_allocator(const counting_allocator<U>& other) noexcept :
        _num_allocations(other._num_allocations)
    {}

    T* allocate(std::size_t n) {
        ++*_num_allocations;
        return std::allocator<T> {}.allocate(n);
    }

    void deallocate(T* p, std::size_t n) {
        --*_num_allocations;
        std::allocator<T> {}.deallocate(p, n);
    }

    template <typename U>
    bool operator==(const counting_allocator<U>& other) const noexcept {
        return _num_allocations == other._num_allocations;
    }

    template <typename U>
    bool operator!=(const counting_allocator<U>& other) const noexcept {
        return !(*this == other);
    }
};

struct shared_test_data {
    error_code success {};

    const std::string connect = encoders::encode_connect(
        "", std::nullopt, std::nullopt, 60, false, test::dflt_cprops, std::nullopt
    );
    const std::string connack = encoders::encode_connack(
        false, reason_codes::success.value(), {}
    );
    const std::string connack_max_packet_size = encoders::encode_connack(
        false, reason_codes::success.value(),
        std::invoke([] {
            connack_props cprops;
            cprops[prop::maximum_packet_size] = 32;
            return cprops;
        })
    );

    const std::string topic = "topic";
    const std::string payload = "payload";
    const std::string large_payload = std::string(64, 'p');
    const std::string large_topic = std::string(64, 't');

    const std::string publish_qos0 = encoders::encode_publish(
        0, topic, payload, qos_e::at_most_once, retain_e::no, dup_e::no, {}
    );
    const std::string publish_qos1 = encoders::encode_publish(
        1, topic, payload, qos_e::at_least_once, retain_e::no, dup_e::no, {}
    );
    const std::string publish_qos2 = encoders::encode_publish(
        1, topic, payload, qos_e::exactly_once, retain_e::no, dup_e::no, {}
    );
    const std::string puback = encoders::encode_puback(1, uint8_t(0x00), {});
    const std::string pubrec = encoders::encode_pubrec(1, uint8_t(0x00), {});
    const std::string pubrel = encoders::encode_pubrel(1, uint8_t(0x00), {});
    const std::string pubcomp = encoders::encode_pubcomp(1, uint8_t(0x00), {});

    const std::string subscribe = encoders::encode_subscribe(
        1, std::vector<subscribe_topic> { { topic, subscribe_options {} } },
        subscribe_props {}
    );
    const std::string suback = encoders::encode_suback(
        1, std::vector<uint8_t> { uint8_t(0x00) }, suback_props {}
    );
    const std::string unsubscribe = encoders::encode_unsubscribe(
        1, std::vector<std::string> { topic }, unsubscribe_props {}
    );
    const std::string unsuback = encoders::encode_unsuback(
        1, std::vector<uint8_t> { uint8_t(0x00) }, unsuback_props {}
    );
};

using test::after;
using namespace std::chrono_literals;

using client_type = mqtt_client<test::test_stream>;

template <typename TestBody>
void run_test(
    test::msg_exchange broker_side, error_code expected_ec,
    TestBody&& test_body
) {
    constexpr int expected_handlers_called = 1;
    int handlers_called = 0;
    int num_allocations = 0;

    asio::io_context ioc;
    auto executor = ioc.get_executor();
    auto& broker = asio::make_service<test::test_broker>(
        ioc, executor, std::move(broker_side)
    );

    client_type c(executor);
    c.brokers("127.0.0.1")
        .async_run(asio::detached);

    test_body(
        c,
        asio::bind_allocator(
            counting_allocator<void>(num_allocations),
            [&](error_code ec, auto&&...) {
                ++handlers_called;
                BOOST_TEST(ec == expected_ec);
                BOOST_TEST(num_allocations == 0);
                c.cancel();
            }
        )
    );

    broker.run(ioc);
    BOOST_TEST(handlers_called == expected_handlers_called);
    BOOST_TEST(num_allocations == 0);
    BOOST_TEST(broker.received_all_expected());
}

BOOST_FIXTURE_TEST_CASE(async_publish_qos0, shared_test_data) {
    test::msg_exchange broker_side;
    broker_side
        .expect(connect)
            .complete_with(success, after(0ms))
            .reply_with(connack, after(0ms))
        .expect(publish_qos0)
            .complete_with(success, after(0ms));

    run_test(
        std::move(broker_side), success,
        [this](client_type& c, auto handler) {
            c.async_publish<qos_e::at_most_once>(
                topic, payload, retain_e::no, publish_props {},
                std::move(handler)
            );
        }
    );
}

BOOST_FIXTURE_TEST_CASE(async_publish_qos1, shared_test_data) {
    test::msg_exchange broker_side;
    broker_side
        .expect(connect)
            .complete_with(success, after(0ms))
            .reply_with(connack, after(0ms))
        .expect(publish_qos1)
            .complete_with(success, after(0ms))
            .reply_with(puback, after(0ms));

    run_test(
        std::move(broker_side), success,
        [this](client_type& c, auto handler) {
            c.async_publish<qos_e::at_least_once>(
                topic, payload, retain_e::no, publish_props {},
                std::move(handler)
            );
        }
    );
}

BOOST_FIXTURE_TEST_CASE(async_publish_qos2, shared_test_data) {
    test::msg_exchange broker_side;
    broker_side
        .expect(connect)
            .complete_with(success, after(0ms))
            .reply_with(connack, after(0ms))
        .expect(publish_qos2)
            .complete_with(success, after(0ms))
            .reply_with(pubrec, after(0ms))
        .expect(pubrel)
            .complete_with(success, after(0ms))
            .reply_with(pubcomp, after(0ms));

    run_test(
        std::move(broker_side), success,
        [this](client_type& c, auto handler) {
            c.async_publish<qos_e::exactly_once>(
                topic, payload, retain_e::no, publish_props {},
                std::move(handler)
            );
        }
    );
}

BOOST_FIXTURE_TEST_CASE(async_subscribe, shared_test_data) {
    test::msg_exchange broker_side;
    broker_side
        .expect(connect)
            .complete_with(success, after(0ms))
            .reply_with(connack, after(0ms))
        .expect(subscribe)
            .complete_with(success, after(0ms))
            .reply_with(suback, after(0ms));

    run_test(
        std::move(broker_side), success,
        [this](client_type& c, auto handler) {
            c.async_subscribe(
                subscribe_topic { topic, subscribe_options {} },
                subscribe_props {}, std::move(handler)
            );
        }
    );
}

BOOST_FIXTURE_TEST_CASE(async_unsubscribe, shared_test_data) {
    test::msg_exchange broker_side;
    broker_side
        .expect(connect)
            .complete_with(success, after(0ms))
            .reply_with(connack, after(0ms))
        .expect(unsubscribe)
            .complete_with(success, after(0ms))
            .reply_with(unsuback, after(0ms));

    run_test(
        std::move(broker_side), success,
        [this](client_type& c, auto handler) {
            c.async_unsubscribe(
                topic, unsubscribe_props {}, std::move(handler)
            );
        }
    );
}

template <typename TestBody>
void run_immediate_test(const shared_test_data& data, TestBody&& test_body) {
    test::msg_exchange broker_side;
    broker_side
        .expect(data.connect)
            .complete_with(data.success, after(0ms))
            .reply_with(data.connack_max_packet_size, after(0ms))
        .expect(data.publish_qos0)
            .complete_with(data.success, after(0ms));

    run_test(
        std::move(broker_side), client::error::packet_too_large,
        [&data, &test_body](client_type& c, auto handler) {
            c.async_publish<qos_e::at_most_once>(
                data.topic, data.payload, retain_e::no, publish_props {},
                [&c, &test_body, h = std::move(handler)](error_code) mutable {
                    test_body(
                        c,
                        asio::bind_immediate_executor(
                            c.get_executor(), std::move(h)
                        )
                    );
                }
            );
        }
    );
}

BOOST_FIXTURE_TEST_CASE(immediate_async_publish, shared_test_data) {
    run_immediate_test(*this, [this](client_type& c, auto handler) {
        c.async_publish<qos_e::at_most_once>(
            topic, large_payload, retain_e::no, publish_props {},
            std::move(handler)
        );
    });
}

BOOST_FIXTURE_TEST_CASE(immediate_async_subscribe, shared_test_data) {
    run_immediate_test(*this, [this](client_type& c, auto handler) {
        c.async_subscribe(
            subscribe_topic { large_topic, subscribe_options {} },
            subscribe_props {}, std::move(handler)
        );
    });
}

BOOST_FIXTURE_TEST_CASE(immediate_async_unsubscribe, shared_test_data) {
    run_immediate_test(*this, [this](client_type& c, auto handler) {
        c.async_unsubscribe(
            large_topic, unsubscribe_props {}, std::move(handler)
        );
    });
}

BOOST_AUTO_TEST_SUITE_END()
