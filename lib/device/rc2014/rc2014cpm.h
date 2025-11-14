
#ifndef RC2014CPM_H
#define RC2014CPM_H

#include "bus.h"


#define FOLDERCHAR '/'

class rc2014CPM : public virtualDevice
{
private:

    void rc2014_status();
    void rc2014_process(uint32_t commanddata, uint8_t checksum) override;

public:
    bool cpmActive = false;
    void init_cpm(int baud);
    void rc2014_handle_cpm();

};

#endif /* RC2014CPM_H */
