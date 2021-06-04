
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

const char* const InterruptHandler::_edgeToStr(const Edge e) {

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
    const _INTERRUPT_CALLBACK onInterrupt,
    const _ERROR_CALLBACK onError) {
    
        struct Entry e;

        e.gpioPin = gpioPin;
        e.edge = type;
        e.onInterrupt = onInterrupt;
        e.onError = onError;
        e.watch = true;

        _setupInterrupt(e);

}

};
