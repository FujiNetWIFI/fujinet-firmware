#ifndef CDC_ACM_H
#define CDC_ACM_H

class virtualDevice
{
public:
    bool device_active = true;
};

class systemBus
{
public:
    void setup() {};
    void service() {};
    void shutdown() {};
    bool getShuttingDown() { return false; };
};

extern systemBus CDC_ACM;

#endif /* CDC_ACM_H */
