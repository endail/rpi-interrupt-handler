#ifndef _INTERRUPT_HANDLER_H_
#define _INTERRUPT_HANDLER_H_

#include <string>
#include <vector>
#include <functional>

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
    int gpioPin;
    Edge edgeType;
    int fd;
    std::vector<_INTERRUPT_CALLBACK> listeners;
};

struct Entry {
    _INTERRUPT_CALLBACK onInterrupt;
    _ERROR_CALLBACK onError;
    volatile bool _watch;
    Edge edge;
    int gpioPin;
    int fd;
};

//static class
class InterruptHandler {

protected:
    static const char* const _GPIO_PROG = "/usr/bin/gpio";
    static std::vector<Entry> _entries;
    static std::vector<EdgeConfig> _configs;
    InterruptHandler() { }

public:

    //thread function
    static void _watchPin(const Entry* const e);
    static std::string edgeToStr(const Edge e);
    static std::string getClassNodePath(const int gpioPin);

    void InterruptHandler::_setupInterrupt(const Entry e);

    //use wpiPinToGpio and physPinToGpio to translate
    static void attachInterrupt(
        const int gpioPin,
        const Edge type,
        const _INTERRUPT_CALLBACK onInterrupt);

    static void removeInterrupt(
        const int gpioPin,
        const Edge type,
        const _INTERRUPT_CALLBACK onInterrupt);

};
};
#endif
