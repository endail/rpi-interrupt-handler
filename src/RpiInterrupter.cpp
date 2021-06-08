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

#include "../include/RpiInterrupter.h"
#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

namespace endail {

const char* const RpiInterrupter::_EDGE_STRINGS[] = {
    "none",
    "rising",
    "falling",
    "both"
};

const char* const RpiInterrupter::_GPIO_PATHS[] = {
    "/usr/bin/gpio",
    "/usr/local/bin/gpio"
};

const char* RpiInterrupter::_gpioProgPath;
std::vector<RpiInterrupter::EdgeConfig> RpiInterrupter::_configs;
std::mutex RpiInterrupter::_configVecMtx;

void RpiInterrupter::init() {

    for(uint8_t i = 0, l = sizeof(_GPIO_PATHS); i < l; ++i) {
        if(::access(_GPIO_PATHS[i], X_OK) == 0) {
            _gpioProgPath = _GPIO_PATHS[i];
            return;
        }
    }

    throw std::runtime_error("gpio program not found");

}

const std::vector<RpiInterrupter::EdgeConfig>& RpiInterrupter::getInterrupts() {
    return _configs;
}

void RpiInterrupter::removeInterrupt(const int gpioPin) {

    std::lock_guard<std::mutex> lck(_configVecMtx);

    _EDGE_CONF_ITER it = _get_config(gpioPin);

    if(it == _configs.end()) {
        return;
    }

    //first, stop the thread watching the pin state
    _stopWatching(&(*it));

    //second, use the gpio prog to "reset" the interrupt
    //condition
    _set_gpio_interrupt(gpioPin, Edge::NONE);

    //finally, close any open fds and remove the local
    //interrupt config
    ::close(it->pinValEvFd);
    ::close(it->cancelEvFd);

    _configs.erase(it);

}

void RpiInterrupter::attachInterrupt(
    int gpioPin,
    RpiInterrupter::Edge type,
    RpiInterrupter::INTERRUPT_CALLBACK onInterrupt) {
    
        //there can only be one edge type for a given pin
        //eg. it's not possible to have an interrupt for
        //rising and falling simultaneously
        //
        //need to check whether there is an existing pin
        //config and whether the existing pin config's edge type
        //is different from this edge type

        std::unique_lock<std::mutex> lck(_configVecMtx);

        const RpiInterrupter::_EDGE_CONF_ITER it = _get_config(gpioPin);

        //an existing config exists
        //and the edge type is either rising, falling, or both
        //and therefore cannot be overwritten
        if(it != _configs.end() && it->edgeType != RpiInterrupter::Edge::NONE) {
            throw std::invalid_argument("interrupt already set");
        }

        lck.unlock();

        RpiInterrupter::EdgeConfig e(gpioPin, type, onInterrupt);

        _setupInterrupt(e);

}

RpiInterrupter::RpiInterrupter() {
}

const char* const RpiInterrupter::_edgeToStr(const Edge e) {
    return _EDGE_STRINGS[static_cast<uint8_t>(e)];
}

std::string RpiInterrupter::_getClassNodePath(const int gpioPin) {
    return std::string("/sys/class/gpio/gpio").append(
        std::to_string(gpioPin));
}

void RpiInterrupter::_set_gpio_interrupt(
    const int gpioPin,
    const RpiInterrupter::Edge e) {

        const pid_t pid = ::fork();

        if(pid < 0) {
            throw std::runtime_error("failed to fork");
        }

        if(pid == 0) {

            //run the gpio prog to setup an interrupt on the pin
            ::execl(
                _gpioProgPath,
                "gpio",
                "edge",
                std::to_string(gpioPin).c_str(),
                _edgeToStr(e),
                nullptr);

            //should not reach here
            ::exit(EXIT_FAILURE);

        }

        //parent will wait for child
        int status;
        ::waitpid(pid, &status, 0);

        //gpio prog will exit with 0 if successful
        //https://github.com/WiringPi/WiringPi/blob/master/gpio/gpio.c#L1384
        if(status != 0) {
            throw std::runtime_error("failed to set gpio edge interrupt");
        }

}

void RpiInterrupter::_clear_gpio_interrupt(const int fd) {

    int count;
    uint8_t c;

    ::lseek(fd, 0, SEEK_SET);
    ::ioctl(fd, FIONREAD, &count);

    for(int i = 0; i < count; ++i) {
        if(::read(fd, &c, 1) < 0) {
            break;
        }
    }

}

RpiInterrupter::_EDGE_CONF_ITER RpiInterrupter::_get_config(const int gpioPin) {

    return std::find_if(
        _configs.begin(),
        _configs.end(), 
        [gpioPin](const RpiInterrupter::EdgeConfig& e) {
            return e.gpioPin == gpioPin; });

}

void RpiInterrupter::_setupInterrupt(RpiInterrupter::EdgeConfig e) {

    _set_gpio_interrupt(e.gpioPin, e.edgeType);

    const std::string pinValPath = _getClassNodePath(e.gpioPin)
        .append("/value");

    //open file to watch for value change
    if((e.pinValEvFd = ::open(pinValPath.c_str(), O_RDWR)) < 0) {
        throw std::runtime_error("failed to setup interrupt");
    }

    //create event fd for cancelling the thread watch routine
    if((e.cancelEvFd = ::eventfd(0, EFD_SEMAPHORE)) < 0) {
        throw std::runtime_error("failed to setup interrupt");
    }

    //at this point, wiringpi appears to "clear" an interrupt
    //merely by reading the value file to the end?
    _clear_gpio_interrupt(e.pinValEvFd);

    _configs.push_back(e);

    //spawn a thread and let it watch for the pin value change
    std::thread(&RpiInterrupter::_watchPinValue, &e).detach();

}

void RpiInterrupter::_watchPinValue(RpiInterrupter::EdgeConfig* const e) {

    int epollFd;
    struct epoll_event valevin;
    struct epoll_event canevin;
    struct epoll_event outevent;

    valevin.events = EPOLLPRI | EPOLLWAKEUP;
    canevin.events = EPOLLPRI | EPOLLWAKEUP;

    if(!(
        (epollFd = ::epoll_create(2)) >= 0 &&
        ::epoll_ctl(epollFd, EPOLL_CTL_ADD, e->pinValEvFd, &valevin) == 0 &&
        ::epoll_ctl(epollFd, EPOLL_CTL_ADD, e->cancelEvFd, &canevin) == 0
        )) {
            //something has gone horribly wrong
            //cannot wait for cancel event; must clean up now
            ::close(epollFd);
            removeInterrupt(e->gpioPin);
            return;
    }

    while(true) {

        //maxevents set to 1 means only 1 fd will be processed
        //at a time - this is simpler!
        if(::epoll_wait(epollFd, &outevent, 1, -1) < 0) {
            continue;
        }

        //check if the fd is the cancel event
        if(outevent.data.fd == e->cancelEvFd) {
            ::close(epollFd);
            break;
        }

        //interrupt has occurred
        if(outevent.data.fd == e->pinValEvFd) {
            
            //wiringpi does this to "reset" the interrupt
            //https://github.com/WiringPi/WiringPi/blob/master/wiringPi/wiringPi.c#L1947-L1954
            //TODO: should this go before or after the onInterrupt call?
            _clear_gpio_interrupt(e->pinValEvFd);

            //handler is not responsible for dealing with
            //exceptions arising from user code and should
            //ignore them for the sake of efficiency
            try {
                e->onInterrupt();
            }
            catch(...) { }

        }

    }

}

void RpiInterrupter::_stopWatching(RpiInterrupter::EdgeConfig* const e) {
    //https://man7.org/linux/man-pages/man2/eventfd.2.html
    //this will raise an event on the fd which will be picked up
    //by epoll_wait
    const uint8_t buf = 1;
    ::write(e->cancelEvFd, &buf, 1);
}

};
