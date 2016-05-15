#pragma once

#include <functional>

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
