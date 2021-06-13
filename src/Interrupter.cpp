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

#include "../include/Interrupter.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <iostream>
#include <errno.h>

namespace RpiGpioInterrupter {

const char* const Interrupter::_GPIO_SYS_PATH = "/sys/class/gpio";

const char* const Interrupter::_EDGE_STRINGS[] = {
    "none",
    "rising",
    "falling",
    "both"
};

const char* const Interrupter::_DIRECTION_STRINGS[] = {
    "in",
    "out"
};

std::list<EdgeConfig> Interrupter::_configs;
std::mutex Interrupter::_configMtx;
int Interrupter::_exportFd;
int Interrupter::_unexportFd;

void Interrupter::init() {

    _exportFd = ::open(
        std::string(_GPIO_SYS_PATH).append("/export").c_str(),
        O_WRONLY);

    if(_exportFd < 0) {
        throw std::runtime_error("unable to export gpio pins");
    }

    _unexportFd = ::open(
        std::string(_GPIO_SYS_PATH).append("/unexport").c_str(),
        O_WRONLY);

    if(_unexportFd < 0) {
        throw std::runtime_error("unable to unexport gpio pins");
    }

}

void Interrupter::close() {

    for(auto c : _configs) {
        removeInterrupt(c.pin);
        _unexport_gpio(c.pin, _unexportFd);
    }

    ::close(_exportFd);
    ::close(_unexportFd);

}

const std::list<EdgeConfig>& Interrupter::getInterrupts() noexcept {
    return _configs;
}

void Interrupter::removeInterrupt(const GPIO_PIN pin) {

    const EdgeConfig* const c = _get_config(pin);

    if(c == nullptr) {
        return;
    }

    //first, stop the thread watching the pin state
    std::cout << "stopping watch" << std::endl;
    _stopWatching(c);

    sleep(1);

    //second, use the gpio prog to "reset" the interrupt
    //condition
    _set_gpio_interrupt(pin, Edge::NONE);

    //finally, close any open fds and remove the local
    //interrupt config
    ::close(c->pinValFd);
    ::close(c->cancelEvFd);

    _remove_config(c->pin);

}

void Interrupter::disableInterrupt(const GPIO_PIN pin) {
    
    EdgeConfig* const c = _get_config(pin);
    
    if(c == nullptr) {
        throw std::runtime_error("interrupt does not exist");
    }

    c->enabled = false;

}

void Interrupter::enableInterrupt(const GPIO_PIN pin) {

    EdgeConfig* const c = _get_config(pin);
    
    if(c == nullptr) {
        throw std::runtime_error("interrupt does not exist");
    }
    
    c->enabled = true;

}

void Interrupter::attachInterrupt(
    const GPIO_PIN pin,
    const Edge edge,
    const INTERRUPT_CALLBACK onInterrupt) {
    
        //there can only be one edge type for a given pin
        //eg. it's not possible to have an interrupt for
        //rising and falling simultaneously
        //
        //need to check whether there is an existing pin
        //config and whether the existing pin config's edge type
        //is different from this edge type

        if(_get_config(pin) != nullptr) {
            throw std::invalid_argument("interrupt already set");
        }

        EdgeConfig e(pin, edge, onInterrupt);

        _setupInterrupt(e);

}

Interrupter::Interrupter() noexcept {
}

const char* const Interrupter::_edgeToStr(const Edge e) noexcept {
    return _EDGE_STRINGS[static_cast<size_t>(e)];
}

const char* const Interrupter::_directionToStr(const Direction d) noexcept {
    return _DIRECTION_STRINGS[static_cast<size_t>(d)];
}

std::string Interrupter::_getClassNodePath(const GPIO_PIN pin) noexcept {
    return std::string(_GPIO_SYS_PATH)
        .append("/gpio")
        .append(std::to_string(pin));
}

void Interrupter::_set_gpio_interrupt(
    const GPIO_PIN pin,
    const Edge e) {
        _set_gpio_direction(pin, Direction::IN);
        _set_gpio_edge(pin, e);
}

void Interrupter::_clear_gpio_interrupt(const int fd) {
    _get_gpio_value_fd(fd);
}

void Interrupter::_export_gpio(const GPIO_PIN pin) {
    
    const std::string path = std::string(_GPIO_SYS_PATH).append("/export");
    const int fd = ::open(path.c_str(), O_WRONLY);
    
    if(fd < 0) {
        throw std::runtime_error("unable to export pin");
    }

    _export_gpio(pin, fd);
    
    ::close(fd);

}

void Interrupter::_export_gpio(const GPIO_PIN pin, const int fd) {
    
    const std::string str = std::to_string(pin);
    
    if(::write(fd, str.c_str(), str.size()) < 0) {
        throw std::runtime_error("pin export failed");
    }

}

void Interrupter::_unexport_gpio(const GPIO_PIN pin) {
    
    const std::string path = std::string(_GPIO_SYS_PATH).append("/unexport");
    const int fd = ::open(path.c_str(), O_WRONLY);
    
    if(fd < 0) {
        throw std::runtime_error("unable to unexport pin");
    }

    _unexport_gpio(pin, fd);
    
    ::close(fd);

}

void Interrupter::_unexport_gpio(const GPIO_PIN pin, const int fd) {
    
    const std::string str = std::to_string(pin);
    
    if(::write(fd, str.c_str(), str.size()) < 0) {
        throw std::runtime_error("pin unexport failed");
    }

}

bool Interrupter::_gpio_exported(const GPIO_PIN pin) {
    const std::string path = _getClassNodePath(pin).append("/edge");
    return ::access(path.c_str(), R_OK | W_OK) == 0;
}

void Interrupter::_set_gpio_direction(const GPIO_PIN pin, const Direction d) {

    const std::string path = _getClassNodePath(pin).append("/direction");
    const int fd = ::open(path.c_str(), O_WRONLY);

    if(fd < 0) {
        throw std::runtime_error("unable to change gpio direction");
    }

    _set_gpio_direction(d, fd);

    ::close(fd);

}

void Interrupter::_set_gpio_direction(const Direction d, const int fd) {

    const char* const str = _directionToStr(d);
    const size_t len = ::strlen(str);

    if(::write(fd, str, len) < 0) {
        throw std::runtime_error("pin direction change failed");
    }

}

void Interrupter::_set_gpio_edge(const GPIO_PIN pin, const Edge e) {
    
    const std::string path = _getClassNodePath(pin).append("/edge");
    const int fd = ::open(path.c_str(), O_WRONLY);
    
    if(fd < 0) {
        throw std::runtime_error("unable to change pin edge");
    }

    _set_gpio_edge(e, fd);
    
    ::close(fd);

}

void Interrupter::_set_gpio_edge(const Edge e, const int fd) {
    
    const char* const str = _edgeToStr(e);
    const size_t len = ::strlen(str);
    
    if(::write(fd, str, len) < 0) {
        throw std::runtime_error("failed to change gpio edge");
    }

}

bool Interrupter::_get_gpio_value(const GPIO_PIN pin) {

    const std::string path = _getClassNodePath(pin).append("/value");
    const int fd = ::open(path.c_str(), O_RDONLY);
    
    if(fd < 0) {
        throw std::runtime_error("unable to get pin value");
    }
    
    const bool v = _get_gpio_value_fd(fd);

    ::close(fd);

    return v;

}

bool Interrupter::_get_gpio_value_fd(const int fd) {
    
    char v;
    
    if(::read(fd, &v, 1) != 1) {
        throw std::runtime_error("failed to get pin value");
    }
    
    //don't test result of this
    ::lseek(fd, 0, SEEK_SET);
    
    return v == '1';

}

EdgeConfig* Interrupter::_get_config(const GPIO_PIN pin) noexcept {

    auto it = std::find_if(
        _configs.begin(),
        _configs.end(), 
        [pin](const EdgeConfig& e) {
            return e.pin == pin; });

    return it != _configs.end() ? &(*it) : nullptr;

}

void Interrupter::_remove_config(const GPIO_PIN pin) noexcept {

    auto it = std::find_if(
        _configs.begin(),
        _configs.end(),
        [pin](const EdgeConfig& ec) {
            return ec.pin == pin; });

    if(it != _configs.end()) {
        _configs.erase(it);
    }

}

void Interrupter::_setupInterrupt(EdgeConfig e) {

    while(!_gpio_exported(e.pin)) {
        try {
            _export_gpio(e.pin, _exportFd);
        }
        catch(const std::runtime_error& ex) {
            ::usleep(500);
        }
    }

    _set_gpio_interrupt(e.pin, e.edge);
   
    const std::string pinValPath = _getClassNodePath(e.pin)
        .append("/value");

    //open file to watch for value change
    if((e.pinValFd = ::open(pinValPath.c_str(), O_RDONLY)) < 0) {
        throw std::runtime_error("failed to setup interrupt");
    }

    //create event fd for cancelling the thread watch routine
    if((e.cancelEvFd = ::eventfd(0, EFD_SEMAPHORE)) < 0) {
        throw std::runtime_error("failed to setup interrupt");
    }

    //at this point, wiringpi appears to "clear" an interrupt
    //merely by reading the value file to the end?
    _clear_gpio_interrupt(e.pinValFd);

    //need to grab the pointer to the config in the list
    std::unique_lock<std::mutex> lck(_configMtx);
    _configs.push_back(e);
    EdgeConfig* const ptr = &_configs.back();
    lck.unlock();

    //spawn a thread and let it watch for the pin value change
    std::thread(&Interrupter::_watchPinValue, ptr).detach();

}

void Interrupter::_watchPinValue(EdgeConfig* const e) noexcept {

    int epollFd;
    struct epoll_event valevin = {0};
    struct epoll_event canevin = {0};
    struct epoll_event outevent;

    valevin.events = EPOLLPRI | EPOLLWAKEUP;
    canevin.events = EPOLLPRI | EPOLLWAKEUP | EPOLLHUP | EPOLLIN;

    valevin.data.fd = e->pinValFd;
    canevin.data.fd = e->cancelEvFd;

    if(!(
        (epollFd = ::epoll_create(2)) >= 0 &&
        ::epoll_ctl(epollFd, EPOLL_CTL_ADD, e->pinValFd, &valevin) == 0 &&
        ::epoll_ctl(epollFd, EPOLL_CTL_ADD, e->cancelEvFd, &canevin) == 0
        )) {
            //something has gone horribly wrong
            //cannot wait for cancel event; must clean up now
            //ignore exceptions; thread cannot handle them
            try {
                ::close(epollFd);
                removeInterrupt(e->pin);
            }
            catch(...) { }
            return;
    }

    //thread loop
    while(true) {

        //reset the outevent struct
        ::memset(&outevent, 0, sizeof(outevent));

        //maxevents set to 1 means only 1 fd will be processed
        //at a time - this is simpler!
        if(::epoll_wait(epollFd, &outevent, 1, -1) < 0) {
            continue;
        }

        std::cout << "epoll success on " << outevent.data.fd << std::endl;

        //check if the fd is the cancel event
        if(outevent.data.fd == e->cancelEvFd) {
            std::cout << "interrupt cancelled" << std::endl << std::flush;
            ::close(epollFd);
            break;
        }

        //interrupt has occurred
        if(outevent.data.fd == e->pinValFd) {
            
            //wiringpi does this to "reset" the interrupt
            //https://github.com/WiringPi/WiringPi/blob/master/wiringPi/wiringPi.c#L1947-L1954
            //should this go before or after the onInterrupt call?
            try {
                _clear_gpio_interrupt(e->pinValFd);
            }
            catch(...) { }

            //handler is not responsible for dealing with
            //exceptions arising from user code and should
            //ignore them for the sake of efficiency
            try {
                if(e->onInterrupt && e->enabled) {
                    std::thread(e->onInterrupt).detach();
                    //e->onInterrupt();
                }
            }
            catch(...) { }

        }

    }

}

void Interrupter::_stopWatching(const EdgeConfig* const e) noexcept {
    //https://man7.org/linux/man-pages/man2/eventfd.2.html
    //this will raise an event on the fd which will be picked up
    //by epoll_wait
    //eventfd_t v = 1;
    //::eventfd_write(e->cancelEvFd, v);
    const eventfd_t val = 2;
    int result;
    std::cout << "sending cancel to fd: " << e->cancelEvFd << std::endl;

    if((result = ::write(e->cancelEvFd, &val, sizeof(eventfd_t))) != sizeof(eventfd_t)) {
        int err = errno;
        std::cout << "Err: " << err << std::endl;
        std::cout << strerror(err) << std::endl;
        throw std::runtime_error("failed to send cancel");
    }
    std::cout << "wrote to cancel ev fd" << std::endl;
}

};
