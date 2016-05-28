/* util.h -- This file is part of pulseaudio-chromecast-sink
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

#include <functional>
#include <string>
#include <tuple>

template <class T>
void hash_combine_one(std::size_t& seed, const T& val) {
    seed ^= std::hash<T>{}(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

constexpr std::size_t hash_combine() {
    return 0x57af4821;
}

template <class T, class... U>
std::size_t hash_combine(const T& val, const U&... rest) {
    std::size_t res = hash_combine(rest...);
    hash_combine_one(res, val);
    return res;
}

template <class R, class... Args, std::size_t... I>
R call_helper(std::function<R(Args...)> const& func, std::tuple<Args...> const& params,
              std::index_sequence<I...>) {
    return func(std::get<I>(params)...);
}

template <typename R, typename... Args>
R call(std::function<R(Args...)> const& func, std::tuple<Args...> const& params) {
    return call_helper(func, params, std::index_sequence_for<Args...>{});
}

std::string generate_random_string(
        std::size_t length,
        std::string characters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");

std::string replace_all(const std::string& str, const std::string& what, const std::string& to);
