/* chromecast_channel.cpp -- This file is part of pulseaudio-chromecast-sink
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

#include "chromecast_channel.h"

MainChromecastChannel::MainChromecastChannel(asio::io_service& io_service, std::string name_,
                                             std::string destination_, MessageFunc send_func_,
                                             const char* logger_name, private_tag tag)
        : BasicChromecastChannel<MainChromecastChannel>(io_service, name_, destination_, send_func_,
                                                        logger_name, tag) {
    curr_request_id = 623453;

    register_namespace_callback(CHCHANNS_RECEIVER,
                                [this](nlohmann::json msg) { handle_receiver_channel(msg); });
}

void MainChromecastChannel::handle_receiver_channel(nlohmann::json msg) try {
    auto req_it = pending_requests.find(msg["requestId"].get<int>());
    if (req_it != pending_requests.end()) {
        req_it->second(msg);
        pending_requests.erase(req_it);
    }
} catch (std::domain_error) {
    logger->warn("(MainChromecastChannel) JSON receiver ns, didn't have expected fields");
}

void MainChromecastChannel::load_app(std::string app_id, StatusCb loaded_callback) {
    weak_dispatch([=] {
        int request_id = curr_request_id++;
        nlohmann::json load_msg = {
                {"type", "LAUNCH"}, {"appId", app_id}, {"requestId", request_id}};
        send_message(CHCHANNS_RECEIVER, load_msg);
        pending_requests[request_id] = loaded_callback;
    });
}

void MainChromecastChannel::stop_app(std::string session_id, StatusCb stopped_callback) {
    weak_dispatch([=] {
        int request_id = curr_request_id++;
        nlohmann::json load_msg = {
                {"type", "LAUNCH"}, {"sessionId", session_id}, {"requestId", request_id}};
        send_message(CHCHANNS_RECEIVER, load_msg);
        pending_requests[request_id] = stopped_callback;
    });
}

void MainChromecastChannel::get_status(StatusCb status_callback) {
    weak_dispatch([=] {
        int request_id = curr_request_id++;
        nlohmann::json load_msg = {{"type", "GET_STATUS"}, {"requestId", request_id}};
        send_message(CHCHANNS_RECEIVER, load_msg);
        pending_requests[request_id] = status_callback;
    });
}

AppChromecastChannel::AppChromecastChannel(asio::io_service& io_service, std::string name_,
                                           std::string destination_, MessageFunc send_func_,
                                           const char* logger_name, private_tag tag)
        : BasicChromecastChannel<AppChromecastChannel>(io_service, name_, destination_, send_func_,
                                                       logger_name, tag) {
    curr_request_id = 1;

    register_namespace_callback(CHCHANNS_STREAM_APP,
                                [this](nlohmann::json msg) { handle_app_channel(msg); });
}

void AppChromecastChannel::handle_app_channel(nlohmann::json msg) try {
    int request_id = msg["requestId"];
    auto req_it = pending_requests.find(request_id);
    if (req_it != pending_requests.end()) {
        std::string type = msg["type"];
        if (type == "OK") {
            req_it->second(Result(msg["data"]));
        } else if (type == "ERROR") {
            req_it->second(Result(msg["message"].get<std::string>()));
        } else {
            logger->error("(AppChromecastChannel) Unknown app ns response type '{}'", type);
        }
        pending_requests.erase(req_it);
    } else {
        logger->error("(AppChromecastChannel) Unexpected requestId '{}'", request_id);
    }
} catch (std::domain_error) {
    logger->error("(AppChromecastChannel) JSON app ns, didn't have expected fields");
}
