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

// TODO: add tcp and tls connection timeout

ChromecastConnection::ChromecastConnection(asio::io_service& io_service_,
                                           asio::ip::tcp::endpoint endpoint_,
                                           const char* logger_name, private_tag)
        : io_service(io_service_), strand(io_service),
          ssl_context(asio::ssl::context::sslv23_client), socket(io_service, ssl_context),
          endpoint(endpoint_) {
    logger = spdlog::get(logger_name);
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
    assert(!is_stopped);

    logger->trace("(ChromecastConnection) Connecting to {}", endpoint.address().to_string());

    strand.dispatch([ this, this_ptr = shared_from_this() ] {
        if (is_stopped) return;
        socket.lowest_layer().async_connect(
                endpoint, strand.wrap([ this, this_ptr = shared_from_this() ](
                                  const asio::error_code& error) { connect_handler(error); }));
    });
}

void ChromecastConnection::stop() {
    strand.dispatch([ this, this_ptr = shared_from_this() ] {
        if (is_stopped) {
            logger->warn("(ChromecastConnection) Requested to stop already stopped connection.");
        } else {
            logger->trace("(ChromecastConnection) Disconnecting");
            is_stopped = true;
            socket.lowest_layer().cancel();
        }
    });
}

void ChromecastConnection::report_error(std::string message) {
    assert(strand.running_in_this_thread());
    is_stopped = true;
    notify_disconnect = false;
    socket.lowest_layer().cancel();
    if (socket.lowest_layer().is_open()) {
        asio::error_code ec;
        socket.lowest_layer().close(ec);
        if (ec) {
            logger->error("(ChromecastConnection) Error while closing socket because of error: {}",
                          ec.message());
        }
    }
    error_handler(message);
}

void ChromecastConnection::shutdown_tcp() {
    if (!socket.lowest_layer().is_open()) {
        logger->warn("(ChromecastConnection) Asked to close closed connection");
    } else {
        asio::error_code ec;
        socket.lowest_layer().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        if (ec) {
            report_error("Failed to shutdown socket: " + ec.message());
            return;
        }
        socket.lowest_layer().close(ec);
        if (ec) {
            report_error("Failed to close socket: " + ec.message());
            return;
        }
        logger->trace("(ChromecastConnection) Closed TCP connection");
    }
}

void ChromecastConnection::shutdown_tls() {
    is_stopped = true;
    assert(strand.running_in_this_thread());
    socket.async_shutdown(
            strand.wrap([ this, this_ptr = shared_from_this() ](const asio::error_code& error) {
                if (error == asio::error::operation_aborted) {
                    shutdown_tls();
                } else if (is_connection_end(error)) {
                    logger->trace("ChromecastConnection) Closed TLS connection");
                    if (notify_disconnect) {
                        notify_disconnect = false;
                        connected_handler(false);
                    }
                    shutdown_tcp();
                } else if (error) {
                    report_error("Shutting down TLS connection failed " + error.message());
                } else {
                    assert(false && "This place should be unreachable");
                }
            }));
}

void ChromecastConnection::connect_handler(const asio::error_code& error) {
    assert(strand.running_in_this_thread());
    if (error) {
        if (error != asio::error::operation_aborted) {
            report_error("Failed to connect to Chromecast: " + error.message());
        }
        // If it is operation_aborted then we didn't connected so we can return.
        return;
    }

    logger->trace("(ChromecastConnection) Opened TCP connection");
    if (is_stopped) {
        shutdown_tcp();
    } else {
        asio::ip::tcp::no_delay option(true);
        socket.lowest_layer().set_option(option);

        socket.async_handshake(asio::ssl::stream_base::client, strand.wrap([
            this, this_ptr = shared_from_this()
        ](const asio::error_code& error_) { handshake_handler(error_); }));
    }
}

void ChromecastConnection::handshake_handler(const asio::error_code& error) {
    assert(strand.running_in_this_thread());
    if (error && error != asio::error::operation_aborted) {
        report_error("TLS handshake failed: " + error.message());
    } else if (error) {  // it is asio::error::operation_aborted
        shutdown_tcp();
    } else if (is_stopped) {
        shutdown_tls();
    } else {
        logger->trace("(ChromecastConnection) Opened TLS connection");
        notify_disconnect = true;
        connected_handler(true);
        read_message();
    }
}

bool ChromecastConnection::is_connection_end(const asio::error_code& error) {
    assert(strand.running_in_this_thread());
    if (error == asio::error::eof) {
        logger->warn("(ChromecastConnection) Got error::eof, that was unexpected");
    }

    return error == asio::ssl::error::stream_truncated || error == asio::error::eof;
}

void ChromecastConnection::read_op_handle_error_and_stop(const asio::error_code& error) {
    if (is_connection_end(error)) {
        assert(notify_disconnect);
        connected_handler(false);
        is_stopped = true;
        logger->trace("ChromecastConnection) Peer closed TLS connection");
        if (socket.lowest_layer().is_open()) {
            shutdown_tcp();
        } else {
            logger->trace("ChromecastConnection) Peer closed TCP connection");
        }
    } else if (error == asio::error::operation_aborted || is_stopped) {
        shutdown_tls();
    } else {
        report_error("Read message header operation failed: " + error.message());
    }
}

void ChromecastConnection::read_message() {
    assert(strand.running_in_this_thread());
    asio::async_read(socket, asio::buffer(&message_header.data, 4), strand.wrap([
        this, this_ptr = shared_from_this()
    ](const asio::error_code& error, std::size_t) { handle_header_read(error); }));
}

void ChromecastConnection::handle_header_read(const asio::error_code& error) {
    assert(strand.running_in_this_thread());
    if (error || is_stopped) {
        read_op_handle_error_and_stop(error);
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
                         strand.wrap([ this, this_ptr = shared_from_this() ](
                                 const asio::error_code& error_, std::size_t size) {
                             handle_message_data_read(error_, size);
                         }));
    }
}

void ChromecastConnection::handle_message_data_read(const asio::error_code& error,
                                                    std::size_t size) {
    assert(strand.running_in_this_thread());
    if (error || is_stopped) {
        read_op_handle_error_and_stop(error);
    } else {
        cast_channel::CastMessage message;
        message.ParseFromArray(read_buffer.get(), size);
        // TODO: what if deserialiation fails?
        logger->trace("(ChromecastConnection) Received message\n{}", message.DebugString());
        messages_handler(std::move(message));
        read_message();
    }
}

void ChromecastConnection::send_message(const cast_channel::CastMessage& message) {
    std::size_t buffer_size = sizeof(uint32_t) + message.ByteSize();
    std::shared_ptr<char> data(new char[buffer_size], std::default_delete<char[]>());

    *reinterpret_cast<uint32_t*>(data.get()) = htobe32(static_cast<uint32_t>(message.ByteSize()));
    // TODO: Can serialization fail?
    message.SerializeToArray(data.get() + sizeof(uint32_t), message.ByteSize());

    logger->trace("(ChromecastConnection) Sending message\n{}", message.DebugString());

    strand.dispatch([ this, data = std::move(data), buffer_size, this_ptr = shared_from_this() ] {
        if (is_stopped) return;
        write_queue.emplace_back(std::move(data), buffer_size);
        if (write_queue.size() == 1) {
            write_from_queue();
        }
    });
}

void ChromecastConnection::write_from_queue() {
    assert(strand.running_in_this_thread());
    auto& buff = write_queue.front();

    asio::async_write(socket, asio::buffer(buff.first.get(), buff.second),
                      strand.wrap([ this, this_ptr = shared_from_this() ](
                              const asio::error_code& error, const size_t) {
                          if (error) {
                              if (error != asio::error::operation_aborted) {
                                  report_error("Writing data to socket failed: " + error.message());
                              }
                          } else {
                              write_queue.pop_front();
                              if (!write_queue.empty() && !is_stopped) {
                                  write_from_queue();
                              }
                          }
                      }));
}
