/* chromecast_connection.h -- This file is part of pulseaudio-chromecast-sink
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

#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

#include <asio/connect.hpp>
#include <asio/io_service.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ssl.hpp>
#include <asio/strand.hpp>

#include "proto/cast_channel.pb.h"

class ChromecastConnection {
  public:
    typedef std::function<void(std::string)> ErrorHandler;
    typedef std::function<void(bool)> ConnectedHandler;
    typedef std::function<void(cast_channel::CastMessage)> MessagesHandler;

    ChromecastConnection(const ChromecastConnection&) = delete;

    ChromecastConnection(asio::io_service& io_service_, asio::ip::tcp::endpoint endpoint,
                         ErrorHandler error_handler_, MessagesHandler messages_handler_,
                         ConnectedHandler connected_handler_, const char* logger_name = "default");

    ~ChromecastConnection();

    void send_message(const cast_channel::CastMessage& message) {
        connection_implementation->send_message(message);
    }

  private:
    class Implementation : public std::enable_shared_from_this<Implementation> {
      private:
        struct private_tag {};

      public:
        Implementation(const Implementation&) = delete;

        static std::shared_ptr<Implementation> create(asio::io_service& io_service_,
                                                      ErrorHandler error_handler_,
                                                      MessagesHandler messages_handler_,
                                                      ConnectedHandler connected_handler_,
                                                      const char* logger_name);

        Implementation(asio::io_service& io_service_, ErrorHandler error_handler_,
                       MessagesHandler messages_handler_, ConnectedHandler connected_handler_,
                       const char* logger_name, private_tag);

        void send_message(const cast_channel::CastMessage& message);

        void connect(asio::ip::tcp::endpoint endpoint_);
        void disconnect();

      private:
        void connect_handler(const asio::error_code& error);
        void handshake_handler(const asio::error_code& error);
        void report_error(std::string message);
        bool is_connection_end(const asio::error_code& error);
        void read_op_handler_error(const asio::error_code& error);
        void shutdown_tls();
        void write_from_queue();
        void read_message();
        void handle_header_read(const asio::error_code& error);
        void handle_message_data_read(const asio::error_code& error, std::size_t size);

        std::shared_ptr<spdlog::logger> logger;
        asio::io_service& io_service;
        asio::io_service::strand write_strand;
        asio::ssl::context ssl_context;
        asio::ssl::stream<asio::ip::tcp::socket> socket;
        ErrorHandler error_handler;
        MessagesHandler messages_handler;
        ConnectedHandler connected_handler;
        asio::ip::tcp::endpoint endpoint;

        std::deque<std::pair<std::shared_ptr<char>, std::size_t>> write_queue;
        union {
            char data[4];
            uint32_t be_length;
        } message_header;
        std::unique_ptr<char[]> read_buffer;
        std::size_t read_buffer_size;
    };

    std::shared_ptr<Implementation> connection_implementation;
};
