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
#include <memory>
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

/*
 * Simple wrapper that wraps any callback handler with std::weak_ptr to make sure that callback
 * target object exists.
 */
template <class F, class T>
class weak_ptr_wrapper {
  public:
    weak_ptr_wrapper(F&& f_, std::weak_ptr<T> ptr_) : f(std::forward<F>(f_)), weak_ptr(ptr_) {}

    template <class... Arg>
    void operator()(Arg... arg) {
        if (auto ptr = weak_ptr.lock()) {
            f(arg...);
        }
    }

  private:
    F f;
    std::weak_ptr<T> weak_ptr;
};

template <class F, class T>
weak_ptr_wrapper<F, T> wrap_weak_ptr(F&& f, std::weak_ptr<T> ptr) {
    return weak_ptr_wrapper<F, T>(std::forward<F>(f), ptr);
};

template <class F, class T>
weak_ptr_wrapper<F, T> wrap_weak_ptr(F&& f, std::shared_ptr<T> ptr) {
    return wrap_weak_ptr<F, T>(std::forward<F>(f), std::weak_ptr<T>{ptr});
};

template <class F, class T>
weak_ptr_wrapper<F, T> wrap_weak_ptr(F&& f, T* ptr) {
    return wrap_weak_ptr<F, T>(std::forward<F>(f), ptr->shared_from_this());
};
