#ifdef BUILD_RS232
#ifndef TRS80M1FUJI_H
#define TRS80M1FUJI_H

#include "rs232Fuji.h"

// The TRS-80 Model I card. Its CONFIG boots from slot 1 as a JV1 disk, which
// the card's FD1771 serves to the Level II ROM like any other.
class trs80m1Fuji : public rs232Fuji
{
public:
    std::string config_image() override { return "/autorun.jv1"; }
};

#endif /* TRS80M1FUJI_H */
#endif /* BUILD_RS232 */
