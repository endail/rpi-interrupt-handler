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

//std::list<PinConfig> Interrupter::_configs;
std::vector<PINCONF_PTR> Interrupter::_configs;
std::thread Interrupter::_epollThread;
int Interrupter::_epollFd;
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

    _epollFd = ::epoll_create(1);

    if(_epollFd < 0) {
        throw std::runtime_error("unable to create epoll");
    }

    _epollThread = std::thread(_watchEpoll);
    _epollThread.detach();

}

void Interrupter::close() {

    for(auto conf : _configs) {
        removePin(conf->pin);
        _unexport_gpio(conf->pin, _unexportFd);
    }

    ::close(_epollFd);
    ::close(_exportFd);
    ::close(_unexportFd);

}

const std::vector<PINCONF_PTR>& Interrupter::getInterrupts() noexcept {
    return _configs;
}

void Interrupter::attach(const GPIO_PIN pin, const Edge edge, const INTERRUPT_CALLBACK cb) {

    //first, check if a config for the pin exists
    //  if it does, check if the edge matches
    //      if it does, add the callback
    //      else, throw ex
    //  else, setup the pin/edge and add the callback

    PINCONF_PTR conf = _get_config(pin);

    //reverse these tests?
    if(conf != nullptr) {
        if(conf->edge == edge) {
            conf->_callbacks.push_back(CallbackEntry(cb));
        }
        else {
            throw std::runtime_error("interrupt already set");
        }
    }
    else {
        conf = _setup_pin(pin, edge);
        conf->_callbacks.push_back(CallbackEntry(cb));
        _configs.push_back(conf);
    }

}

void Interrupter::disable(const GPIO_PIN pin, const INTERRUPT_CALLBACK cb) {

    PINCONF_PTR conf = _get_config(pin);

    if(conf == nullptr) {
        return;
    }

    auto it = std::find_if(
        conf->_callbacks.begin(),
        conf->_callbacks.end(), 
        [cb](const CallbackEntry& ce) {
            //needs better equality testing between function objects... somehow...
            return cb.target<void()>() == ce.onInterrupt.target<void()>(); });

    if(it != conf->_callbacks.end()) {
        it->enabled = false;
    }

}

void Interrupter::enable(const GPIO_PIN pin, const INTERRUPT_CALLBACK cb) {

    PINCONF_PTR conf = _get_config(pin);

    if(conf == nullptr) {
        return;
    }

    auto it = std::find_if(
        conf->_callbacks.begin(),
        conf->_callbacks.end(), 
        [cb](const CallbackEntry& ce) {
            return cb.target<void()>() == ce.onInterrupt.target<void()>(); });

    if(it != conf->_callbacks.end()) {
        it->enabled = true;
    }

}

void Interrupter::remove(const GPIO_PIN pin, const INTERRUPT_CALLBACK cb) {

    PINCONF_PTR conf = _get_config(pin);

    if(conf == nullptr) {
        return;
    }

    auto it = std::find_if(
        conf->_callbacks.begin(),
        conf->_callbacks.end(), 
        [cb](const CallbackEntry& ce) {
            return cb.target<void()>() == ce.onInterrupt.target<void()>(); });

    if(it != conf->_callbacks.end()) {
        conf->_callbacks.erase(it);
    }

}

void Interrupter::disablePin(const GPIO_PIN pin) {
    
    PINCONF_PTR conf = _get_config(pin);

    if(conf != nullptr) {
        conf->enabled = false;
    }

}

void Interrupter::enablePin(const GPIO_PIN pin) {
    
    PINCONF_PTR conf = _get_config(pin);

    if(conf != nullptr) {
        conf->enabled = true;
    }

}

void Interrupter::removePin(const GPIO_PIN pin) {

    PINCONF_PTR conf = _get_config(pin);

    if(conf == nullptr) {
        return;
    }

    _close_pin(conf);

    auto it = std::find_if(
        _configs.begin(),
        _configs.end(),
        [conf](const PINCONF_PTR& p) {
            return conf == p; });

    _configs.erase(it);

}

Interrupter::Interrupter() noexcept {
}

const char* const Interrupter::_edgeToStr(const Edge e) noexcept {
    return _EDGE_STRINGS[static_cast<size_t>(e)];
}

const char* const Interrupter::_directionToStr(const Direction d) noexcept {
    return _DIRECTION_STRINGS[static_cast<size_t>(d)];
}

PINCONF_PTR Interrupter::_setup_pin(const GPIO_PIN pin, const Edge edge) {

    struct epoll_event inev = {0};
    PINCONF_PTR conf = nullptr;

    //this is an infinite loop!
    while(!_gpio_exported(pin)) {
        try {
            _export_gpio(pin, _exportFd);
        }
        catch(const std::runtime_error& ex) { }
    }

    _set_gpio_interrupt(pin, edge);
   
    conf = std::make_shared<PinConfig>(pin, edge);

    const std::string pinValPath = _getClassNodePath(pin)
        .append("/value");

    //open file to watch for value change
    if((conf->pinValFd = ::open(pinValPath.c_str(), O_RDONLY)) < 0) {
        throw std::runtime_error("failed to setup interrupt");
    }

    inev.events = EPOLLPRI;
    inev.data.fd = conf->pinValFd;
    inev.data.ptr = conf.get();

    //at this point, wiringpi appears to "clear" an interrupt
    //merely by reading the value file to the end?
    _clear_gpio_interrupt(conf->pinValFd);

    if(::epoll_ctl(_epollFd, EPOLL_CTL_ADD, conf->pinValFd, &inev) != 0) {
        throw std::runtime_error("failed to add to epoll");
    }

    return conf;

}

void Interrupter::_close_pin(PINCONF_PTR conf) {

    //first, remove the file descriptors from the epoll
    ::epoll_ctl(_epollFd, EPOLL_CTL_DEL, conf->pinValFd, nullptr);

    //second, set the interrupt to none
    //***this could fail and be messy if left unhandled***
    _set_gpio_interrupt(conf->pin, Edge::NONE);

    //third, close the file descriptors
    ::close(conf->pinValFd);

    //set some defaults for the ptr?
    //conf->pin = -1;
    //conf->edge = Edge::NONE;
    //conf->_callbacks.clear();
    //conf->pinValFd = -1;
    //conf->enabled = false;

    //delete conf; //???

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

PINCONF_PTR Interrupter::_get_config(const GPIO_PIN pin) noexcept {

    auto it = std::find_if(
        _configs.begin(),
        _configs.end(), 
        [pin](const PINCONF_PTR& e) {
            return e->pin == pin; });

    return it != _configs.end() ? *it : nullptr;

}

void Interrupter::_remove_config(const GPIO_PIN pin) noexcept {

    auto it = std::find_if(
        _configs.begin(),
        _configs.end(),
        [pin](const PINCONF_PTR& ec) {
            return ec->pin == pin; });

    if(it != _configs.end()) {
        _configs.erase(it);
    }

}

void Interrupter::_watchEpoll() {

    struct epoll_event outevent;

    //thread loop
    while(true) {

        //reset the outevent struct
        ::memset(&outevent, 0, sizeof(outevent));

        //maxevents set to 1 means only 1 fd will be processed
        //at a time - this is simpler!
        if(::epoll_wait(_epollFd, &outevent, 1, -1) < 0) {
            continue;
        }

        //this will need to be modified if delegated to another
        //thread
        _processEpollEvent(&outevent);

    }

}

void Interrupter::_processEpollEvent(const epoll_event* const ev) {

    PINCONF_PTR conf = *static_cast<PINCONF_PTR*>(ev->data.ptr);

    if(conf == nullptr) {
        return;
    }

    //wiringpi does this to "reset" the interrupt
    //https://github.com/WiringPi/WiringPi/blob/master/wiringPi/wiringPi.c#L1947-L1954
    //should this go before or after the onInterrupt call?
    try {
        _clear_gpio_interrupt(conf->pinValFd);
    }
    catch(...) { }

    if(!conf->enabled) {
        return;
    }

    auto it = conf->_callbacks.cbegin();
    auto end = conf->_callbacks.cend();

    for(; it != end; ++it) {
        if(it->enabled && it->onInterrupt) {
            
            //handler is not responsible for dealing with
            //exceptions arising from user code and should
            //ignore them for the sake of efficiency
            try {
                it->onInterrupt();
            }
            catch(...) { }

        }
    }

}

};
