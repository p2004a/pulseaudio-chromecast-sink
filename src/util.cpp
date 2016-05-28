/* util.cpp -- This file is part of pulseaudio-chromecast-sink
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

#include <algorithm>
#include <cassert>
#include <random>
#include <string>

#include "util.h"

std::string generate_random_string(std::size_t length, std::string characters) {
    assert(characters.size() > 0);
    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd());
    std::uniform_int_distribution<int> char_dist(0, characters.size() - 1);
    std::string result(length, '\0');
    for (std::size_t i = 0; i < length; ++i) {
        result[i] = characters[char_dist(gen)];
    }
    return result;
}

std::string replace_all(const std::string& str, const std::string& what, const std::string& to) {
    std::string result;
    result.reserve(str.size());
    auto prev_it = str.begin();
    for (auto curr_it = std::search(str.begin(), str.end(), what.begin(), what.end());
         curr_it != str.end();
         curr_it = std::search(curr_it, str.end(), what.begin(), what.end())) {
        result.append(prev_it, curr_it);
        result.append(to.begin(), to.end());
        std::advance(curr_it, what.size());
        prev_it = curr_it;
    }
    result.append(prev_it, str.end());
    return result;
}
