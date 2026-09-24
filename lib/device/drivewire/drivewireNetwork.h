#ifndef DRIVEWIRENETWORK_H
#define DRIVEWIRENETWORK_H

#include "NDevice.h"

class drivewireNetwork : public NDevice
{
protected:
    void fujidev_set_parser(const FUJI_COMMAND_PACKET &packet) override;
};

#endif /* DRIVEWIRENETWORK_H */
