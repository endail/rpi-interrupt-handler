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

#ifndef RPI_INTERRUPTER_H_87004B4F_3EBC_4756_BDC5_01DE911A84F8
#define RPI_INTERRUPTER_H_87004B4F_3EBC_4756_BDC5_01DE911A84F8

#include <string>
#include <list>
#include <functional>
#include <mutex>

namespace endail {
class RpiInterrupter {
public:

    typedef std::function<void()> INTERRUPT_CALLBACK;

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
        int gpioPin;
        Edge edge;
        INTERRUPT_CALLBACK onInterrupt;
        int gpioPinValFd = -1;
        int cancelEvFd = -1;
        bool enabled = true;

        EdgeConfig() = default;

        EdgeConfig(const int pin, const Edge e, INTERRUPT_CALLBACK cb) noexcept
            :   gpioPin(pin), edge(e), onInterrupt(cb) { }
    };

    static void init();
    static void close();
    static const std::list<EdgeConfig>& getInterrupts() noexcept;
    static void removeInterrupt(const int gpioPin);
    static void disableInterrupt(const int gpioPin);
    static void enableInterrupt(const int gpioPin);
    static void attachInterrupt(
        const int gpioPin,
        const Edge type,
        const INTERRUPT_CALLBACK onInterrupt);


protected:

    static const char* const _GPIO_SYS_PATH;
    static const char* const _EDGE_STRINGS[];
    static const char* const _DIRECTION_STRINGS[];

    static std::list<EdgeConfig> _configs;
    static std::mutex _configMtx;
    static int _exportFd;
    static int _unexportFd;

    RpiInterrupter() noexcept;
    virtual ~RpiInterrupter() = default;

    static const char* const _edgeToStr(const Edge e) noexcept;
    static const char* const _directionToStr(const Direction d) noexcept;
    static std::string _getClassNodePath(const int gpioPin) noexcept;

    static void _set_gpio_interrupt(const int gpioPin, const Edge e);
    static void _clear_gpio_interrupt(const int fd);

    static void _export_gpio(const int gpioPin);
    static void _export_gpio(const int gpioPin, const int fd);
    static void _unexport_gpio(const int gpioPin);
    static void _unexport_gpio(const int gpioPin, const int fd);
    static void _set_gpio_direction(const int gpioPin, const Direction d);
    static void _set_gpio_direction(const Direction d, const int fd);
    static void _set_gpio_edge(const int gpioPin, const Edge e);
    static void _set_gpio_edge(const Edge e, const int fd);
    static bool _get_gpio_value(const int gpioPin);
    static bool _get_gpio_value_fd(const int fd);

    static EdgeConfig* _get_config(const int gpioPin) noexcept;
    static void _remove_config(const int gpioPin) noexcept;

    static void _setupInterrupt(EdgeConfig e);
    static void _watchPinValue(EdgeConfig* const e) noexcept;
    static void _stopWatching(const EdgeConfig* const e) noexcept;

};
};
#endif
