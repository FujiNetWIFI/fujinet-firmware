
#ifndef RC2014CPM_H
#define RC2014CPM_H

#include "../cpm/cpm.h"

class rc2014CPM : public cpmDevice
{
private:

    void rc2014_status();
    void rc2014_process(uint32_t commanddata, uint8_t checksum) override;
};

#endif /* RC2014CPM_H */
