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
#include <cstring>
#include <stdexcept>
#include <thread>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <unistd.h>

#include <iostream>
#include <errno.h>

namespace RpiGpioInterrupter {

CALLBACK_ID CallbackEntry::_id = 0;

CallbackEntry::CallbackEntry(const INTERRUPT_CALLBACK cb) noexcept
    : id(_genId()), enabled(true), onInterrupt(cb) { }

CALLBACK_ID CallbackEntry::_genId() noexcept {
    return _id++;
}

PinConfig::PinConfig(const GPIO_PIN p, const Edge e) noexcept
    : pin(p), edge(e), pinValFd(-1), enabled(true) { }

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

std::unordered_map<GPIO_PIN, PINCONF_PTR> Interrupter::_configs;
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

    //TODO: change to pthread
    _epollThread = std::thread(_watchEpoll);

    //TODO: increase thread priority!
    struct sched_param schParams = {0};
    schParams.sched_priority = sched_get_priority_max(SCHED_RR);

    if(::pthread_setschedparam(
        _epollThread.native_handle(),
        SCHED_RR,
        &schParams) < 0) {
            throw std::runtime_error("unable to set shceduler");
    }

}

void Interrupter::close() {

    for(const auto& pair : _configs) {
        removePin(pair.second->pin);
        _unexport_gpio(pair.second->pin, _unexportFd);
    }

    ::close(_epollFd);
    ::close(_exportFd);
    ::close(_unexportFd);

}

CALLBACK_ID Interrupter::attach(
    const GPIO_PIN pin,
    const Edge edge,
    const INTERRUPT_CALLBACK cb) {

        PINCONF_PTR conf;
        CALLBACK_ENT_PTR ce;

        try {
            conf = _configs.at(pin);
        }
        catch(std::out_of_range& ex) {
            conf = _setup_pin(pin, edge);
            _configs[pin] = conf;
        }

        if(conf->edge == edge) {
            ce = std::make_shared<CallbackEntry>(cb);
            conf->callbacks[ce->id] = ce;
            return ce->id;
        }
        
        //an interrupt can only exist for one type of edge
        //per pin
        throw std::runtime_error("interrupt already set");

}

void Interrupter::disable(const CALLBACK_ID id) {
    _get_callback_by_id(id)->enabled = false;
}

void Interrupter::enable(const CALLBACK_ID id) {
    _get_callback_by_id(id)->enabled = true;
}

void Interrupter::remove(const CALLBACK_ID id) {
    _get_config_by_callback_id(id)->callbacks.erase(id);
}

void Interrupter::disablePin(const GPIO_PIN pin) {
    _configs.at(pin)->enabled = false;
}

void Interrupter::enablePin(const GPIO_PIN pin) {
    _configs.at(pin)->enabled = true;
}

void Interrupter::removePin(const GPIO_PIN pin) {
    PINCONF_PTR conf = _configs.at(pin);
    _close_pin(conf);
    _configs.erase(pin);
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
    std::string pinValPath;

    //TODO: I don't like this!
    //better way to handle the - POTENTIAL - infinite loop?
    while(!_gpio_exported(pin)) {
        try {
            _export_gpio(pin, _exportFd);
        }
        catch(const std::runtime_error& ex) { }
    }

    _set_gpio_interrupt(pin, edge);
   
    conf = std::make_shared<PinConfig>(pin, edge);
    pinValPath = _getClassNodePath(pin).append("/value");

    //open file to watch for value change
    if((conf->pinValFd = ::open(pinValPath.c_str(), O_RDONLY)) < 0) {
        throw std::runtime_error("failed to setup interrupt");
    }

    //put the pin number in the event struct
    inev.data.u64 = static_cast<uint64_t>(conf->pin);
    inev.events = EPOLLPRI | EPOLLWAKEUP;

    //at this point, wiringpi appears to "clear" an interrupt
    //merely by reading the value file to the end?
    _clear_gpio_interrupt(conf->pinValFd);

    if(::epoll_ctl(_epollFd, EPOLL_CTL_ADD, conf->pinValFd, &inev) != 0) {
        throw std::runtime_error("failed to add to epoll");
    }

    return conf;

}

void Interrupter::_close_pin(const PINCONF_PTR conf) {

    //first, remove the file descriptors from the epoll
    ::epoll_ctl(_epollFd, EPOLL_CTL_DEL, conf->pinValFd, nullptr);

    //second, set the interrupt to none
    //this could fail and be messy if left unhandled
    //technically doesn't need to be done since nothing
    //will be watching for the interrupt any more
    _set_gpio_interrupt(conf->pin, Edge::NONE);

    //third, close the file descriptors
    ::close(conf->pinValFd);

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

PINCONF_PTR Interrupter::_get_config_by_callback_id(const CALLBACK_ID id) noexcept {

    for(const auto& confPair : _configs) {
        for(const auto& callbackPair : confPair.second->callbacks) {
            if(callbackPair.second->id == id) {
                return confPair.second;
            }
        }
    }

    return nullptr;

}

CALLBACK_ENT_PTR Interrupter::_get_callback_by_id(const CALLBACK_ID id) noexcept {

    for(const auto& confPair : _configs) {
        for(const auto& callbackPair : confPair.second->callbacks) {
            if(callbackPair.second->id == id) {
                return callbackPair.second;
            }
        }
    }

    return nullptr;

}

void Interrupter::_watchEpoll() {

    //TODO: this needs to be put in a different spot!
    ::setpriority(
        PRIO_PROCESS,
        0,
        PRIO_MIN);

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

        //this will need to be modified if the task of processing
        //events is delegated to another thread
        _processEpollEvent(&outevent);

    }

}

void Interrupter::_processEpollEvent(const epoll_event* const ev) {

    PINCONF_PTR conf;
    
    try {
        conf = _configs.at(static_cast<GPIO_PIN>(ev->data.u64));
    }
    catch(const std::out_of_range& ex) {
        //it is possible that between the hardware interrupt occurring
        //and being handled that the pin config is removed. this should
        //not stop the thread
        return;
    }

    try {
        //TODO: should this be the first thing to occur?
        _clear_gpio_interrupt(conf->pinValFd);
    }
    catch(const std::runtime_error& ex) {
        //it is possible this could fail, but it should not
        //prevent the interrupt handler functions from being
        //called
    }

    if(!conf->enabled) {
        return;
    }

    for(const auto& ce : conf->callbacks) {
        if(ce.second->enabled && ce.second->onInterrupt) {
            try {
                ce.second->onInterrupt();
            }
            catch(...) {
                //this function is not responsible for handling
                //exceptions, and any exceptions thrown should not
                //stop the thread
            }
        }
    }

}

};
