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
#include <thread>
#include <mutex>

//https://github.com/WiringPi/WiringPi/blob/master/wiringPi/wiringPi.c#L1924-L2081

namespace endail {
class RpiInterrupter {
public:

    typedef std::function<void()> INTERRUPT_CALLBACK;

    enum class Edge {
        NONE = 0,
        RISING = 1,
        FALLING = 2,
        BOTH = 3
    };

    struct EdgeConfig {
    public:
        int gpioPin;
        Edge edgeType;
        INTERRUPT_CALLBACK onInterrupt;
        int pinValEvFd = -1;
        int cancelEvFd = -1;

        EdgeConfig() = default;

        EdgeConfig(const int pin, const Edge e, INTERRUPT_CALLBACK cb)
            :   gpioPin(pin),
                edgeType(e),
                onInterrupt(cb) { }

    };

    static void init();
    static const std::list<EdgeConfig>& getInterrupts();
    static void removeInterrupt(const int gpioPin);
    static void attachInterrupt(
        int gpioPin,
        Edge type,
        INTERRUPT_CALLBACK onInterrupt);


protected:

    static const char* const _EDGE_STRINGS[];
    static const char* const _GPIO_PATHS[];
    static const char* _gpioProgPath;

    static std::list<EdgeConfig> _configs;
    static std::mutex _configMtx;

    RpiInterrupter();
    virtual ~RpiInterrupter() = default;

    static const char* const _edgeToStr(const Edge e);
    static std::string _getClassNodePath(const int gpioPin);

    static void _set_gpio_interrupt(const int gpioPin, const Edge e);
    static void _clear_gpio_interrupt(const int fd);

    static EdgeConfig* _get_config(const int gpioPin);

    static void _setupInterrupt(EdgeConfig e);
    static void _watchPinValue(EdgeConfig* const e);
    static void _stopWatching(const EdgeConfig* const e);

};
};
#endif
