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
