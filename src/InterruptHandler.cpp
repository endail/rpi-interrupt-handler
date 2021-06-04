
#include "../include/InterruptHandler.h"

#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdexcept>
#include <thread>
#include <algorithm>
#include <cstdlib>

namespace InterruptHandler {

std::string InterruptHandler::edgeToStr(const Edge e) {

    switch(e) {
        case Edge::NONE: return "none";
        case Edge::RISING: return "rising";
        case Edge::FALLING: return "falling";
        case Edge::BOTH: return "both";
    }

    throw std::invalid_argument("unknown edge type");

}

std::string InterruptHandler::getClassNodePath(const int gpioPin) {
    return std::string("/sys/class/gpio/gpio")
        + std::to_string(gpioPin);
}

void InterruptHandler::_watchPin(const Entry* const e) {

    int epollFd;
    int ready;
    bool success = false;
    struct epoll_event inevents;
    struct epoll_event outevents;
    uint8_t c;

    inevents.events = EPOLLPRI | EPOLLERR | EPOLLONESHOT | EPOLLWAKEUP;

    epollFd = ::epoll_create(1);
    ::epoll_ctl(epollFd, EPOLL_CTL_ADD, e->fd, &inevents);

    //looping means onInterrupt will trigger for each interrupt
    //and will only end when watch is false. in this case,
    //the fds are closed and the thread ends
    while(e->watch) {
        if(::epoll_wait(epollFd, &outevents, 1, -1) == 1) {

            //wiringpi does this to "reset" the interrupt
            //https://github.com/WiringPi/WiringPi/blob/master/wiringPi/wiringPi.c#L1947-L1954
            ::lseek(e->fd, 0, SEEK_SET);
            ::read(e->fd, &c, 1);
            
            //call the user interrupt handler func
            e->onInterrupt();

        }
        else {
            e->watch = false;
            e->onError();
        }
    }

    ::close(epollFd);
    ::close(e->fd);
    _entries.erase(std::find(_entries.begin(), _entries.end(), *e));

}

void InterruptHandler::_setupInterrupt(const Entry e) {

    ::pid_t pid = ::fork();
    int status;
    std::string gpioPinStr;
    std::string edgeTypeStr;
    int count;
    char c;

    if(pid < 0) {
        throw std::runtime_error("failed to setup interrupt");
    }

    //https://projects.drogon.net/raspberry-pi/wiringpi/the-gpio-utility/
    if(pid == 0) {

        gpioPinStr = std::string(e.gpioPin);
        edgeTypeStr = _edgeToStr(e.type);

        //run the gpio prog to setup an interrupt on the pin
        ::execl(
            _GPIO_PROG,
            "gpio",
            "edge",
            gpioPinStr.c_str(),
            edgeTypeStr.c_str(),
            nullptr);

        //should not reach here
        ::exit(EXIT_FAILURE);

    }

    //parent will wait for child
    ::waitpid(pid, &status, 0);

    if(status != EXIT_SUCCESS) {
        //gpio edge type failed to set
        throw std::runtime_error("failed to setup interrupt");
    }

    //open file to watch for value change
    e.fd = ::open(getClassNodePath(e.gpioPin) + "/value");

    if(e.fd < 0) {
        throw std::runtime_error("failed to setup interrupt");
    }

    //at this point, wiringpi appears to "clear" an interrupt
    //merely by reading the value file to the end?
    ::ioctl(e.fd, FIONREAD, &count);
    for(int i = 0; i < count; ++i) {
        ::read(e.fd, &c, 1);
    }

    _entries.push_back(e);

    //spawn a thread and let it watch for the pin change
    std::thread(&InterruptHandler::_watchPin, &e).detach();

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

        struct Entry e;

        e.gpioPin = gpioPin;
        e.edge = type;
        e.onInterrupt = onInterrupt;

        _setupInterrupt(e);

}

void InterruptHandler::removeInterrupt(
    const int gpioPin,
    const _INTERRUPT_CALLBACK onInterrupt) {

        //first, find the config in the vector
        auto it = std::find_if(
            _configs.begin(),
            _configs.end(),
            [gpioPin](const EdgeConfig &c) {
                return c.gpioPin == gpioPin; });

        //no config found
        if(it == _configs.end()) {
            return;
        }

        //second, find the interrupt callback in the vector
        auto it2 = std::find_if(
            it->begin(),
            it->end(),
            [onInterrupt](const _INTERRUPT_CALLBACK& cb) {
                return cb == onInterrupt; });

        //no callback found
        if(it2 == it->end()) {
            return;
        }

        //remove the interrupt callback
        it2->erase(onInterrupt);

        //if the vector of callbacks is now empty,
        //disable listening for callbacks
        if(it2->empty()) {
            //stop listening
            //gpio utility...
        }

        //the config can stay; it doesn't matter

}

};
