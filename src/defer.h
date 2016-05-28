/* defer.h -- This file is part of pulseaudio-chromecast-sink
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

#include <utility>

template <typename F>
class defer_finalizer {
  public:
    template <typename T>
    defer_finalizer(T&& f_) : f(std::forward<T>(f_)), moved(false) {}

    defer_finalizer(const defer_finalizer&) = delete;

    defer_finalizer(defer_finalizer&& other) : f(std::move(other.f)), moved(other.moved) {
        other.moved = true;
    }

    ~defer_finalizer() {
        if (!moved) f();
    }

  private:
    F f;
    bool moved;
};

class deferrer {
  public:
    template <typename F>
    defer_finalizer<F> operator<<(F&& f) {
        return defer_finalizer<F>(std::forward<F>(f));
    }
};

extern deferrer deferrer_object;

#define TOKENPASTE(x, y) x##y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)
#define defer auto TOKENPASTE2(__deferred_lambda_call, __COUNTER__) = deferrer_object << [&]
