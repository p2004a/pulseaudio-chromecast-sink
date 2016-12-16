/* chromecast_connection.cpp -- This file is part of pulseaudio-chromecast-sink
 * Copyright (C) 2016  Marek Rusinowski
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <endian.h>
#include <cassert>

#include <asio/io_service.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/ssl.hpp>
#include <asio/write.hpp>

#include "proto/cast_channel.pb.h"

#include "chromecast_connection.h"

ChromecastConnection::ChromecastConnection(asio::io_service& io_service_,
                                           asio::ip::tcp::endpoint endpoint_,
                                           const char* logger_name, private_tag)
        : io_service(io_service_), write_strand(io_service),
          ssl_context(asio::ssl::context::sslv23_client), socket(io_service, ssl_context),
          endpoint(endpoint_) {
    logger = spdlog::get(logger_name);

    asio::ip::tcp::no_delay option(true);
    socket.lowest_layer().set_option(option);
}

std::shared_ptr<ChromecastConnection> ChromecastConnection::create(asio::io_service& io_service,
                                                                   asio::ip::tcp::endpoint endpoint,
                                                                   const char* logger_name) {
    return std::make_shared<ChromecastConnection>(io_service, endpoint, logger_name, private_tag{});
}

void ChromecastConnection::start() {
    assert(connected_handler != nullptr);
    assert(error_handler != nullptr);
    assert(messages_handler != nullptr);

    logger->trace("(ChromecastConnection) Connecting to {}", endpoint.address().to_string());

    socket.lowest_layer().async_connect(
            endpoint, [ this, this_ptr = shared_from_this() ](const asio::error_code& error) {
                connect_handler(error);
            });
}

void ChromecastConnection::stop() {
    if (socket.lowest_layer().is_open()) {
        logger->trace("(ChromecastConnection) Disconnecting");
        socket.lowest_layer().cancel();
    }
}

void ChromecastConnection::report_error(std::string message) {
    socket.lowest_layer().close();
    error_handler(message);
}

void ChromecastConnection::connect_handler(const asio::error_code& error) {
    if (error) {
        if (error != asio::error::operation_aborted) {
            report_error("Failed to connect to Chromecast: " + error.message());
        }
    } else {
        socket.async_handshake(asio::ssl::stream_base::client, [
            this, this_ptr = shared_from_this()
        ](const asio::error_code& error_) { handshake_handler(error_); });
    }
}

void ChromecastConnection::handshake_handler(const asio::error_code& error) {
    if (error) {
        if (error != asio::error::operation_aborted) {
            report_error("TLS handshake failed: " + error.message());
        }
    } else {
        connected_handler(true);
        read_message();
    }
}

bool ChromecastConnection::is_connection_end(const asio::error_code& error) {
    if (error == asio::error::eof) {
        logger->warn("(ChromecastConnection) Got error::eof, that was unexpected");
    }

    return error == asio::ssl::error::stream_truncated || error == asio::error::eof;
}

void ChromecastConnection::read_op_handler_error(const asio::error_code& error) {
    if (error == asio::error::operation_aborted) {
        if (socket.lowest_layer().is_open()) {
            shutdown_tls();
        }
    } else if (is_connection_end(error)) {
        connected_handler(false);
    } else {
        report_error("Read message header operation failed: " + error.message());
    }
}

void ChromecastConnection::send_message(const cast_channel::CastMessage& message) {
    std::size_t buffer_size = sizeof(uint32_t) + message.ByteSize();
    std::shared_ptr<char> data(new char[buffer_size], std::default_delete<char[]>());

    *reinterpret_cast<uint32_t*>(data.get()) = htobe32(static_cast<uint32_t>(message.ByteSize()));
    message.SerializeToArray(data.get() + sizeof(uint32_t), message.ByteSize());

    logger->trace("(ChromecastConnection) Sending message\n{}", message.DebugString());

    write_strand.dispatch(
            [ this, data = std::move(data), buffer_size, this_ptr = shared_from_this() ] {
                write_queue.emplace_back(std::move(data), buffer_size);
                if (write_queue.size() == 1) {
                    write_from_queue();
                }
            });
}

void ChromecastConnection::shutdown_tls() {
    socket.async_shutdown([ this, this_ptr = shared_from_this() ](const asio::error_code& error) {
        if (is_connection_end(error)) {
            logger->trace("ChromecastConnection) TLS connection closed");
            socket.lowest_layer().close();
        } else {
            report_error("Shutting down TLS connection failed " + error.message());
        }
    });
}

void ChromecastConnection::write_from_queue() {
    auto& buff = write_queue.front();

    asio::async_write(
            socket, asio::buffer(buff.first.get(), buff.second),
            [ this, this_ptr = shared_from_this() ](const asio::error_code& error, const size_t) {
                if (error) {
                    if (error != asio::error::operation_aborted) {
                        report_error("Writing data to socket failed: " + error.message());
                    }
                } else {
                    write_queue.pop_front();
                    if (!write_queue.empty()) {
                        write_from_queue();
                    }
                }
            });
}

void ChromecastConnection::read_message() {
    asio::async_read(socket, asio::buffer(&message_header.data, 4), [
        this, this_ptr = shared_from_this()
    ](const asio::error_code& error, std::size_t) { handle_header_read(error); });
}

void ChromecastConnection::handle_header_read(const asio::error_code& error) {
    if (error) {
        read_op_handler_error(error);
    } else {
        uint32_t length = be32toh(message_header.be_length);
        if (length > (1 << 20) /* 1MB */) {
            report_error("Received too big message: " + std::to_string(length));
            return;
        }
        if (read_buffer_size < length) {
            read_buffer_size = static_cast<std::size_t>(length * 1.5);
            read_buffer.reset(new char[read_buffer_size]);
        }
        assert(read_buffer != nullptr);
        asio::async_read(socket, asio::buffer(read_buffer.get(), length),
                         [ this, this_ptr = shared_from_this() ](const asio::error_code& error_,
                                                                 std::size_t size) {
                             handle_message_data_read(error_, size);
                         });
    }
}

void ChromecastConnection::handle_message_data_read(const asio::error_code& error,
                                                    std::size_t size) {
    if (error) {
        read_op_handler_error(error);
    } else {
        cast_channel::CastMessage message;
        message.ParseFromArray(read_buffer.get(), size);
        logger->trace("(ChromecastConnection) Received message\n{}", message.DebugString());
        messages_handler(std::move(message));
        read_message();
    }
}
