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
#include <cstring>
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

const char* const RpiInterrupter::_GPIO_SYS_PATH = "/sys/class/gpio";

const char* const RpiInterrupter::_EDGE_STRINGS[] = {
    "none",
    "rising",
    "falling",
    "both"
};

const char* const RpiInterrupter::_DIRECTION_STRINGS[] = {
    "in",
    "out"
};

std::list<RpiInterrupter::EdgeConfig> RpiInterrupter::_configs;
std::mutex RpiInterrupter::_configMtx;

void RpiInterrupter::init() {
}

const std::list<RpiInterrupter::EdgeConfig>& RpiInterrupter::getInterrupts() {
    return _configs;
}

void RpiInterrupter::removeInterrupt(const int gpioPin) {

    const EdgeConfig* const c = _get_config(gpioPin);

    if(c == nullptr) {
        return;
    }

    //first, stop the thread watching the pin state
    _stopWatching(c);

    //second, use the gpio prog to "reset" the interrupt
    //condition
    _set_gpio_interrupt(gpioPin, Edge::NONE);

    //finally, close any open fds and remove the local
    //interrupt config
    ::close(c->pinValEvFd);
    ::close(c->cancelEvFd);

    _remove_config(c);

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

        if(_get_config(gpioPin) != nullptr) {
            throw std::invalid_argument("interrupt already set");
        }

        RpiInterrupter::EdgeConfig e(gpioPin, type, onInterrupt);

        _setupInterrupt(e);

}

RpiInterrupter::RpiInterrupter() {
}

const char* const RpiInterrupter::_edgeToStr(const Edge e) {
    return _EDGE_STRINGS[static_cast<uint8_t>(e)];
}

const char* const RpiInterrupter::_directionToStr(const Direction d) {
    return _DIRECTION_STRINGS[static_cast<uint8_t>(d)];
}

std::string RpiInterrupter::_getClassNodePath(const int gpioPin) {
    return std::string(_GPIO_SYS_PATH)
        .append("/gpio")
        .append(std::to_string(gpioPin));
}

void RpiInterrupter::_set_gpio_interrupt(
    const int gpioPin,
    const RpiInterrupter::Edge e) {
        _export_gpio(gpioPin);
        _set_gpio_direction(gpioPin, Direction::IN);
        _set_gpio_edge(gpioPin, e);
}

void RpiInterrupter::_clear_gpio_interrupt(const int fd) {
    _get_gpio_value_fd(fd);
}

void RpiInterrupter::_export_gpio(const int gpioPin) {
    
    const std::string path = std::string(_GPIO_SYS_PATH).append("/export");
    const int fd = ::open(path.c_str(), O_WRONLY);
    
    if(fd < 0) {
        throw std::runtime_error("unable to export pin");
    }

    _export_gpio(gpioPin, fd);
    
    ::close(fd);

}

void RpiInterrupter::_export_gpio(const int gpioPin, const int fd) {
    
    const std::string pinStr = std::to_string(gpioPin);
    
    if(::write(fd, pinStr.c_str(), pinStr.size()) < 0) {
        throw std::runtime_error("pin export failed");
    }

}

void RpiInterrupter::_unexport_gpio(const int gpioPin) {
    
    const std::string path = std::string(_GPIO_SYS_PATH).append("/unexport");
    const int fd = ::open(path.c_str(), O_WRONLY);
    
    if(fd < 0) {
        throw std::runtime_error("unable to unexport pin");
    }

    _unexport_gpio(gpioPin, fd);
    
    ::close(fd);

}

void RpiInterrupter::_unexport_gpio(const int gpioPin, const int fd) {
    
    const std::string pinStr = std::to_string(gpioPin);
    
    if(::write(fd, pinStr.c_str(), pinStr.size()) < 0) {
        throw std::runtime_error("pin unexport failed");
    }

}

void RpiInterrupter::_set_gpio_direction(
    const int gpioPin,
    const RpiInterrupter::Direction d) {

        const std::string path = _getClassNodePath(gpioPin).append("/direction");
        const int fd = ::open(path.c_str(), O_WRONLY);

        if(fd < 0) {
            throw std::runtime_error("unable to change gpio direction");
        }

        _set_gpio_direction(d, fd);

        ::close(fd);

}

void RpiInterrupter::_set_gpio_direction(const RpiInterrupter::Direction d, const int fd) {
    
    const char* const dirStr = _directionToStr(d);
    
    if(::write(fd, dirStr, ::strlen(dirStr)) < 0) {
        throw std::runtime_error("pin direction change failed");
    }

}

void RpiInterrupter::_set_gpio_edge(const int gpioPin, const RpiInterrupter::Edge e) {
    
    const std::string path = _getClassNodePath(gpioPin).append("/edge");
    const int fd = ::open(path.c_str(), O_WRONLY);
    
    if(fd < 0) {
        throw std::runtime_error("unable to change pin edge");
    }

    _set_gpio_edge(e, fd);
    
    ::close(fd);

}

void RpiInterrupter::_set_gpio_edge(const RpiInterrupter::Edge e, const int fd) {
    
    const char* const edgeStr = _edgeToStr(e);
    
    if(::write(fd, edgeStr, ::strlen(edgeStr)) < 0) {
        throw std::runtime_error("failed to change gpio edge");
    }

}

bool RpiInterrupter::_get_gpio_value(const int gpioPin) {

    const std::string path = _getClassNodePath(gpioPin).append("/value");
    const int fd = ::open(path.c_str(), O_RDONLY);
    
    if(fd < 0) {
        throw std::runtime_error("unable to get pin value");
    }
    
    const bool v = _get_gpio_value_fd(fd);

    ::close(fd);

    return v;

}

bool RpiInterrupter::_get_gpio_value_fd(const int fd) {
    
    char v;
    
    if(::read(fd, &v, 1) != 1) {
        throw std::runtime_error("failed to get pin value");
    }
    
    //don't test result of this
    ::lseek(fd, 0, SEEK_SET);
    
    return v == '1' ? true : false;

}

void RpiInterrupter::_set_gpio_value(const int gpioPin, const bool v) {
    
    const std::string path = _getClassNodePath(gpioPin).append("/value");
    const int fd = ::open(path.c_str(), O_WRONLY);
    
    if(fd < 0) {
        throw std::runtime_error("unable to set pin value");
    }

    _set_gpio_value(v, fd);
    
    ::close(fd);

}

void RpiInterrupter::_set_gpio_value(const bool v, const int fd) {
    
    char b = v ? '1' : '0';

    if(::write(fd, &b, 1) < 0) {
        throw std::runtime_error("failed to set pin value");
    }
    
    //don't test return value of this
    ::lseek(fd, 0, SEEK_SET);

}

RpiInterrupter::EdgeConfig* RpiInterrupter::_get_config(const int gpioPin) {

    auto it = std::find_if(
        _configs.begin(),
        _configs.end(), 
        [gpioPin](const RpiInterrupter::EdgeConfig& e) {
            return e.gpioPin == gpioPin; });

    return it != _configs.end() ? &(*it) : nullptr;

}

void RpiInterrupter::_remove_config(RpiInterrupter::EdgeConfig* const e) {

    auto it = std::find(_configs.begin(), _configs.end(), *e);

    if(it != _configs.end()) {
        _configs.erase(it);
    }

}

void RpiInterrupter::_setupInterrupt(RpiInterrupter::EdgeConfig e) {

    _set_gpio_interrupt(e.gpioPin, e.edgeType);

    const std::string pinValPath = _getClassNodePath(e.gpioPin)
        .append("/value");

    //open file to watch for value change
    if((e.pinValEvFd = ::open(pinValPath.c_str(), O_RDONLY)) < 0) {
        throw std::runtime_error("failed to setup interrupt");
    }

    //create event fd for cancelling the thread watch routine
    if((e.cancelEvFd = ::eventfd(0, EFD_SEMAPHORE)) < 0) {
        throw std::runtime_error("failed to setup interrupt");
    }

    //at this point, wiringpi appears to "clear" an interrupt
    //merely by reading the value file to the end?
    _clear_gpio_interrupt(e.pinValEvFd);

    //need to grab the pointer to the config in the list
    std::unique_lock<std::mutex> lck(_configMtx);
    _configs.push_back(e);
    EdgeConfig* const ptr = &_configs.back();
    lck.unlock();

    //spawn a thread and let it watch for the pin value change
    std::thread(&RpiInterrupter::_watchPinValue, ptr).detach();

}

void RpiInterrupter::_watchPinValue(RpiInterrupter::EdgeConfig* const e) {

    int epollFd;
    struct epoll_event valevin = {0};
    struct epoll_event canevin = {0};
    struct epoll_event outevent = {0};

    valevin.events = EPOLLPRI | EPOLLWAKEUP;
    canevin.events = EPOLLHUP | EPOLLIN | EPOLLWAKEUP;

    valevin.data.fd = e->pinValEvFd;
    canevin.data.fd = e->cancelEvFd;

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

        //reset the outevent struct
        outevent = {0};

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

void RpiInterrupter::_stopWatching(const RpiInterrupter::EdgeConfig* const e) {
    //https://man7.org/linux/man-pages/man2/eventfd.2.html
    //this will raise an event on the fd which will be picked up
    //by epoll_wait
    ::eventfd_write(e->cancelEvFd, 1);
}

};
