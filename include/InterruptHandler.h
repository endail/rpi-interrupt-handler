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

namespace endail {

typedef std::function<void()> _INTERRUPT_CALLBACK;
typedef std::function<void()> _ERROR_CALLBACK;

class InterruptHandler {

public:
    enum class Edge {
        NONE = 0,
        RISING = 1,
        FALLING = 2,
        BOTH = 3
    };

protected:

    struct EdgeConfig {
    public:
        int gpioPin;
        Edge edgeType;
        _INTERRUPT_CALLBACK onInterrupt;
        int pinValEvFd;
        int cancelEvFd;

        EdgeConfig(
            int pin,
            Edge e,
            _INTERRUPT_CALLBACK cb)
                :   gpioPin(pin),
                    edgeType(e),
                    onInterrupt(cb),
                    pinValEvFd(-1),
                    cancelEvFd(-1) {
        }

    };

    typedef std::vector<EdgeConfig>::iterator _EDGE_CONF_ITER;

    static const char* const EDGE_TO_STR[];
    static const char* const GPIO_PATHS[];

    static std::string _gpio_prog;
    static std::vector<EdgeConfig> _configs;
    InterruptHandler() { }

    static void _set_gpio_pin(const int gpioPin, const Edge e);
    static void _clear_interrupt(const int fd);
    static _EDGE_CONF_ITER _get_config(const int gpioPin);
    static void _setupInterrupt(EdgeConfig e);
    static std::string _edgeToStr(const Edge e);
    static std::string _getClassNodePath(const int gpioPin);
    static void _watchPin(EdgeConfig* const e);
    static void _stopWatching(EdgeConfig* const e);


public:
    static void init();
    static void attachInterrupt(
        int gpioPin,
        Edge type,
        _INTERRUPT_CALLBACK onInterrupt);
    static void removeInterrupt(const int gpioPin);

    //return a vector of... structs?
    //with gpio pin, edge type, and callback func?
    static const std::vector<EdgeConfig>& getInterrupts() {
        return _configs;
    }

};
};
#endif
