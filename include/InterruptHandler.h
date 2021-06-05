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

#ifndef _INTERRUPT_HANDLER_H_87004B4F_3EBC_4756_BDC5_01DE911A84F8
#define _INTERRUPT_HANDLER_H_87004B4F_3EBC_4756_BDC5_01DE911A84F8

#include <string>
#include <vector>
#include <functional>
#include <thread>

//https://github.com/WiringPi/WiringPi/blob/master/wiringPi/wiringPi.c#L1924-L2081

namespace InterruptHandler {

typedef std::function<void()> _INTERRUPT_CALLBACK;
typedef std::function<void()> _ERROR_CALLBACK;

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
    _INTERRUPT_CALLBACK onInterrupt;
    int fd;
    volatile bool watch;
    std::thread th;

    EdgeConfig(
        const int pin,
        const Edge e,
        const _INTERRUPT_CALLBACK cb)
            :   gpioPin(pin),
                edgeType(e),
                onInterrupt(cb),
                fd(-1),
                watch(false) {
    }

};

typedef std::vector<EdgeConfig>::iterator _EDGE_CONF_ITER;

static const char* const EDGE_TO_STR[4];
static const char* const GPIO_PATHS[2];

class InterruptHandler {

protected:
    static std::string _gpio_prog;
    static std::vector<EdgeConfig> _configs;
    InterruptHandler() { }

    static void _set_gpio_pin(const int pin, const Edge e);
    static void _clear_interrupt(const int fd);
    static _EDGE_CONF_ITER _get_config(const int pin);
    static void _setupInterrupt(EdgeConfig e);
    static std::string _edgeToStr(const Edge e);
    static std::string _getClassNodePath(const int gpioPin);
    static void _watchPin(EdgeConfig* const e);

public:

    static void init();

    static void attachInterrupt(
        const int gpioPin,
        const Edge type,
        const _INTERRUPT_CALLBACK onInterrupt);

    static void removeInterrupt(const int gpioPin);

};
};
#endif
