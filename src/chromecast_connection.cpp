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

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/write.hpp>

#include "proto/cast_channel.pb.h"

#include "chromecast_connection.h"

ChromecastConnection::~ChromecastConnection() {
    connection_implementation->disconnect();
}

ChromecastConnection::Implementation::Implementation(boost::asio::io_service& io_service_,
                                                     ErrorHandler error_handler_,
                                                     MessagesHandler messages_handler_,
                                                     ConnectedHandler connected_handler_,
                                                     const char* logger_name, private_tag)
        : io_service(io_service_), write_strand(io_service),
          ssl_context(boost::asio::ssl::context::sslv23_client), socket(io_service, ssl_context),
          error_handler(error_handler_), messages_handler(messages_handler_),
          connected_handler(connected_handler_), read_buffer_size(0) {
    logger = spdlog::get(logger_name);
}

std::shared_ptr<ChromecastConnection::Implementation> ChromecastConnection::Implementation::create(
        boost::asio::io_service& io_service_, ErrorHandler error_handler_,
        MessagesHandler messages_handler_, ConnectedHandler connected_handler_,
        const char* logger_name) {
    return std::make_shared<Implementation>(io_service_, error_handler_, messages_handler_,
                                            connected_handler_, logger_name, private_tag{});
}

void ChromecastConnection::Implementation::disconnect() {
    logger->trace("ChromecastConnection) Disconnecting");
    connected_handler(false);
    socket.lowest_layer().cancel();
}

void ChromecastConnection::Implementation::connect_handler(const boost::system::error_code& error) {
    if (error) {
        if (error != boost::asio::error::operation_aborted) {
            error_handler("Failed to connect to Chromecast: " + error.message());
        }
    } else {
        socket.async_handshake(boost::asio::ssl::stream_base::client, [
            this, this_ptr = shared_from_this()
        ](const boost::system::error_code& error_) { handshake_handler(error_); });
    }
}

void ChromecastConnection::Implementation::handshake_handler(
        const boost::system::error_code& error) {
    if (error) {
        if (error != boost::asio::error::operation_aborted) {
            error_handler("TLS handshake failed: " + error.message());
        }
    } else {
        connected_handler(true);
        read_message();
    }
}

void ChromecastConnection::Implementation::send_message(const cast_channel::CastMessage& message) {
    std::size_t buffer_size = sizeof(uint32_t) + message.ByteSize();
    std::shared_ptr<char> data(new char[buffer_size], std::default_delete<char[]>());

    *reinterpret_cast<uint32_t*>(data.get()) = htobe32(static_cast<uint32_t>(message.ByteSize()));
    message.SerializeToArray(data.get() + sizeof(uint32_t), message.ByteSize());

    write_strand.dispatch(
            [ this, data{std::move(data)}, buffer_size, this_ptr = shared_from_this() ] {
                write_queue.emplace_back(std::move(data), buffer_size);
                if (write_queue.size() == 1) {
                    write_from_queue();
                }
            });
}

void ChromecastConnection::Implementation::shutdown_tls() {
    socket.async_shutdown(
            [ this, this_ptr = shared_from_this() ](const boost::system::error_code& error) {
                if (error.category() == boost::asio::error::get_ssl_category() &&
                    error.value() == ERR_PACK(ERR_LIB_SSL, 0, SSL_R_SHORT_READ)) {
                    logger->trace("ChromecastConnection) TLS connection closed");
                } else {
                    error_handler("Shutting down TLS connection failed " + error.message());
                }
                socket.lowest_layer().close();
            });
}

void ChromecastConnection::Implementation::write_from_queue() {
    logger->trace("(ChromecastConnection) Queuing write message");
    auto& buff = write_queue.front();

    boost::asio::async_write(socket, boost::asio::buffer(buff.first.get(), buff.second), [
        this, this_ptr = shared_from_this()
    ](const boost::system::error_code& error, const size_t) {
        if (error) {
            if (error != boost::asio::error::operation_aborted) {
                error_handler("Writing data to socket failed: " + error.message());
            }
        } else {
            logger->trace("(ChromecastConnection) Wrote message!");
            write_queue.pop_front();
            if (!write_queue.empty()) {
                write_from_queue();
            }
        }
    });
}

void ChromecastConnection::Implementation::read_message() {
    boost::asio::async_read(socket, boost::asio::buffer(&message_header.data, 4), [
        this, this_ptr = shared_from_this()
    ](const boost::system::error_code& error, std::size_t) { handle_header_read(error); });
}

void ChromecastConnection::Implementation::handle_header_read(
        const boost::system::error_code& error) {
    if (error) {
        if (error == boost::asio::error::operation_aborted) {
            shutdown_tls();
        } else if (error == boost::asio::error::eof) {
            connected_handler(false);
        } else {
            error_handler("Read message header operation failed: " + error.message());
        }
    } else {
        uint32_t length = be32toh(message_header.be_length);
        logger->trace("(ChromecastConnection) We have header! size: {}", length);
        logger->trace("{} < {} ?", read_buffer_size, length);
        if (read_buffer_size < length) {
            read_buffer_size = static_cast<std::size_t>(length * 1.5);
            logger->trace("buffer size: {}", read_buffer_size);
            read_buffer.reset(new char[read_buffer_size]);
        }
        assert(read_buffer != nullptr);
        boost::asio::async_read(socket, boost::asio::buffer(read_buffer.get(), length),
                                [ this, this_ptr = shared_from_this() ](
                                        const boost::system::error_code& error_, std::size_t size) {
                                    handle_message_data_read(error_, size);
                                });
    }
}

void ChromecastConnection::Implementation::handle_message_data_read(
        const boost::system::error_code& error, std::size_t size) {
    if (error) {
        if (error == boost::asio::error::operation_aborted) {
            shutdown_tls();
        } else if (error == boost::asio::error::eof) {
            logger->warn(
                    "(ChromecastConnection) Got message header but reading body ended with eof");
            connected_handler(false);
        } else {
            error_handler("Read message body operation failed: " + error.message());
        }
    } else {
        logger->trace("(ChromecastConnection) We got message body! size: {}", size);
        cast_channel::CastMessage message;
        message.ParseFromArray(read_buffer.get(), size);
        messages_handler(std::move(message));
        read_message();
    }
}
