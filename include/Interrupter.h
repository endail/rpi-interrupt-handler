// MIT License
//
// Copyright (c) 2021 Daniel Robertson
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef INTERRUPTER_H_87004B4F_3EBC_4756_BDC5_01DE911A84F8
#define INTERRUPTER_H_87004B4F_3EBC_4756_BDC5_01DE911A84F8

#include <string>
#include <list>
#include <functional>
#include <mutex>

namespace RpiGpioInterrupter {

typedef std::function<void()> INTERRUPT_CALLBACK;
typedef int GPIO_PIN;

enum class Direction {
    IN = 0,
    OUT = 1
};

enum class Edge {
    NONE = 0,
    RISING = 1,
    FALLING = 2,
    BOTH = 3
};

struct EdgeConfig {
public:
    GPIO_PIN pin;
    Edge edge;
    INTERRUPT_CALLBACK onInterrupt;
    int pinValFd = -1;
    int cancelEvFd = -1;
    bool enabled = true;

    EdgeConfig() = default;

    EdgeConfig(const GPIO_PIN p, const Edge e, INTERRUPT_CALLBACK cb) noexcept
        :   pin(p), edge(e), onInterrupt(cb) { }
};

class Interrupter {
public:

    static void init();
    static void close();
    static const std::list<EdgeConfig>& getInterrupts() noexcept;
    static void removeInterrupt(const GPIO_PIN pin);
    static void disableInterrupt(const GPIO_PIN pin);
    static void enableInterrupt(const GPIO_PIN pin);
    static void attachInterrupt(
        const GPIO_PIN pin,
        const Edge edge,
        const INTERRUPT_CALLBACK onInterrupt);


protected:

    static const char* const _GPIO_SYS_PATH;
    static const char* const _EDGE_STRINGS[];
    static const char* const _DIRECTION_STRINGS[];

    static std::list<EdgeConfig> _configs;
    static std::mutex _configMtx;
    static int _exportFd;
    static int _unexportFd;

    Interrupter() noexcept;
    virtual ~Interrupter() = default;

    static const char* const _edgeToStr(const Edge e) noexcept;
    static const char* const _directionToStr(const Direction d) noexcept;
    static std::string _getClassNodePath(const GPIO_PIN pin) noexcept;

    static void _set_gpio_interrupt(const GPIO_PIN pin, const Edge e);
    static void _clear_gpio_interrupt(const int fd);

    static void _export_gpio(const GPIO_PIN pin);
    static void _export_gpio(const GPIO_PIN pin, const int fd);
    static void _unexport_gpio(const GPIO_PIN pin);
    static void _unexport_gpio(const GPIO_PIN pin, const int fd);
    static bool _gpio_exported(const GPIO_PIN pin);
    static void _set_gpio_direction(const GPIO_PIN pin, const Direction d);
    static void _set_gpio_direction(const Direction d, const int fd);
    static void _set_gpio_edge(const GPIO_PIN pin, const Edge e);
    static void _set_gpio_edge(const Edge e, const int fd);
    static bool _get_gpio_value(const GPIO_PIN pin);
    static bool _get_gpio_value_fd(const int fd);

    static EdgeConfig* _get_config(const GPIO_PIN pin) noexcept;
    static void _remove_config(const GPIO_PIN pin) noexcept;

    static void _setupInterrupt(EdgeConfig e);
    static void _watchPinValue(EdgeConfig* const e) noexcept;
    static void _stopWatching(const EdgeConfig* const e) noexcept;

};
};
#endif
