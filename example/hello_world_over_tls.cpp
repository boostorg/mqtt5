//
// Copyright (c) 2023-2024 Ivica Siladic, Bruno Iljazovic, Korina Simicevic
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)
//

//[hello_world_over_tls
#include <boost/mqtt5/logger.hpp>
#include <boost/mqtt5/mqtt_client.hpp>
#include <boost/mqtt5/types.hpp>

#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>

#include <iostream>
#include <string>

struct config {
    std::string brokers = "broker.hivemq.com";
    uint16_t port = 8883; // 8883 is the default TLS MQTT port.
    std::string client_id = "async_mqtt5_tester";
};

// External customization point.
namespace boost::mqtt5 {

// Specify that the TLS handshake will be performed as a client.
template <typename StreamBase>
struct tls_handshake_type<boost::asio::ssl::stream<StreamBase>> {
    static constexpr auto client = boost::asio::ssl::stream_base::client;
};

// This client uses this function to indicate which hostname it is
// attempting to connect to at the start of the handshaking process.
template <typename StreamBase>
void assign_tls_sni(
    const authority_path& ap,
    boost::asio::ssl::context& /* ctx */,
    boost::asio::ssl::stream<StreamBase>& stream
) {
    SSL_set_tlsext_host_name(stream.native_handle(), ap.host.c_str());
}

} // end namespace boost::mqtt5

int main(int argc, char** argv) {
    config cfg;

    if (argc == 4) {
        cfg.brokers = argv[1];
        cfg.port = uint16_t(std::stoi(argv[2]));
        cfg.client_id = argv[3];
    }

    boost::asio::io_context ioc;

    // TLS context that the underlying SSL stream will use.
    // The purpose of the context is to allow us to set up TLS/SSL-related options. 
    // See ``__SSL__`` for more information and options.
    boost::asio::ssl::context context(boost::asio::ssl::context::tls_client);

    // Set up the TLS context.
    // This step is highly dependent on the specific requirements of the Broker you are connecting to.
    // Each broker may have its own standards and expectations for establishing a secure TLS/SSL connection. 
    // This can include verifying certificates, setting up private keys, PSK authentication, and others.

    // Construct the Client with ``__SSL_STREAM__`` as the underlying stream
    // with ``__SSL_CONTEXT__`` as the ``__TlsContext__`` type
    // and logging enabled.
    boost::mqtt5::mqtt_client<
        boost::asio::ssl::stream<boost::asio::ip::tcp::socket>,
        boost::asio::ssl::context,
        boost::mqtt5::logger
    > client(ioc, std::move(context), boost::mqtt5::logger(boost::mqtt5::log_level::info));


    // If you want to use the Client without logging, initialise it with the following line instead.
    //boost::mqtt5::mqtt_client<
    //	boost::asio::ssl::stream<boost::asio::ip::tcp::socket>,
    //	boost::asio::ssl::context
    //> client(ioc, std::move(context));

    client.brokers(cfg.brokers, cfg.port) // Broker that we want to connect to.
        .credentials(cfg.client_id) // Set the Client Identifier. (optional)
        .async_run(boost::asio::detached); // Start the Client.

    client.async_publish<boost/mqtt5::qos_e::at_most_once>(
        "async-mqtt5/test", "Hello world!",
        boost::mqtt5::retain_e::no, boost::mqtt5::publish_props{},
        [&client](boost::mqtt5::error_code ec) {
            std::cout << ec.message() << std::endl;
            client.async_disconnect(boost::asio::detached);
        }
    );

    ioc.run();
}
//]
