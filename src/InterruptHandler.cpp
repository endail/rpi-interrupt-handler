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

#include "../include/InterruptHandler.h"
#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace InterruptHandler {

const char* const InterruptHandler::EDGE_TO_STR[] = {
    "none",
    "rising",
    "falling",
    "both"
};

const char* const InterruptHandler::GPIO_PATHS[] = {
    "/usr/bin/gpio",
    "/usr/local/bin/gpio"
};

void InterruptHandler::_set_gpio_pin(const int pin, const Edge e) {

    const pid_t pid = ::fork();

    if(pid < 0) {
        throw std::runtime_error("failed to setup interrupt");
    }

    if(pid == 0) {

        const std::string gpioPinStr = std::to_string(pin);
        const std::string edgeTypeStr = _edgeToStr(e);

        //run the gpio prog to setup an interrupt on the pin
        ::execl(
            _gpio_prog.c_str(),
            "gpio",
            "edge",
            gpioPinStr.c_str(),
            edgeTypeStr.c_str(),
            nullptr);

        //should not reach here
        ::exit(EXIT_FAILURE);

    }

    //parent will wait for child
    int status;
    ::waitpid(pid, &status, 0);

    if(status != EXIT_SUCCESS) {
        //gpio edge type failed to set
        throw std::runtime_error("failed to setup interrupt");
    }

}

void InterruptHandler::_clear_interrupt(const int fd) {

    int count;
    uint8_t c;

    ::lseek(fd, 0, SEEK_SET);
    ::ioctl(fd, FIONREAD, &count);

    for(int i = 0; i < count; ++i) {
        ::read(fd, &c, 1);
    }

}

_EDGE_CONF_ITER InterruptHandler::_get_config(const int pin) {
    return std::find_if(_configs.begin(), _configs.end(), 
        [pin](const EdgeConfig& e) { e.gpioPin == pin; });
}

void InterruptHandler::_setupInterrupt(EdgeConfig e) {

    _set_gpio_pin(e.gpioPin, e.edgeType);

    //open file to watch for value change
    e.fd = ::open(_getClassNodePath(e.gpioPin).append("/value").c_str());

    if(e.fd < 0) {
        throw std::runtime_error("failed to setup interrupt");
    }

    //at this point, wiringpi appears to "clear" an interrupt
    //merely by reading the value file to the end?
    _clear_interrupt(e.fd);

    _entries.push_back(e);

    //spawn a thread and let it watch for the pin change
    e.th = std::thread(&InterruptHandler::_watchPin, &e);
    e.th.detach();

}

std::string InterruptHandler::_edgeToStr(const Edge e) {
    return std::string(EDGE_TO_STR[static_cast<uint8_t>(e)]);
}

std::string InterruptHandler::_getClassNodePath(const int gpioPin) {
    return std::string("/sys/class/gpio/gpio").append(
        std::to_string(gpioPin));
}

void InterruptHandler::_watchPin(EdgeConfig* const e) {

    int epollFd;
    struct epoll_event inevents;
    struct epoll_event outevents;

    e->watch = false;

    inevents.events = EPOLLPRI | EPOLLWAKEUP;

    if((epollFd = ::epoll_create(1)) >= 0) {
        if(::epoll_ctl(epollFd, EPOLL_CTL_ADD, e->fd, &inevents) == 0) {
            //only allow watching to occur if epoll creation is
            //successful
            e->watch = true;
        }
    }
    
    //looping means onInterrupt will trigger for each interrupt
    //and will only end when watch is false. in this case,
    //the fds are closed and the thread ends
    while(e->watch) {
        if(::epoll_wait(epollFd, &outevents, 1, -1) == 1) {

            //wiringpi does this to "reset" the interrupt
            //https://github.com/WiringPi/WiringPi/blob/master/wiringPi/wiringPi.c#L1947-L1954
            _clear_interrupt(e->fd);

            //call the user interrupt handler func
            e->onInterrupt();

        }
    }

    ::close(epollFd);

    removeInterrupt(e->gpioPin);

}

void InterruptHandler::init() {

    auto it = std::begin(GPIO_PATHS);
    const auto end = std::end(GPIO_PATHS);

    for(; it != end; ++it) {
        if(::access(*it, X_OK) == 0) {
            _gpio_prog = std::string(*it);
            break;
        }
    }

    if(it == end) {
        throw std::runtime_error("gpio program not found");
    }

}

void InterruptHandler::attachInterrupt(
    const int gpioPin,
    const Edge type,
    const _INTERRUPT_CALLBACK onInterrupt) {
    
        //there can only be one edge type for a given pin
        //eg. it's not possible to have an interrupt for
        //rising and falling simultaneously
        //
        //need to check whether there is an existing pin
        //config and whether the existing pin config's edge type
        //is different from this edge type

        const _EDGE_CONF_ITER it = _get_config(gpioPin);

        if(it != _configs.end() && it->edgeType != Edge::NONE) {
            //an existing config exists
            //and the edge type is either rising, falling, or both
            //and therefore cannot be overwritten
            throw std::invalid_argument("interrupt already set");
        }

        struct EdgeConfig e(gpioPin, type, onInterrupt);

        _setupInterrupt(e);

}

void InterruptHandler::removeInterrupt(const int gpioPin) {

    _EDGE_CONF_ITER it = _get_config(gpioPin);

    if(it == _configs.end()) {
        return;
    }

    //remove the interrupt
    _set_gpio_pin(gpioPin, Edge::NONE);
    ::close(it->fd);
    _configs.erase(it);

}

};
