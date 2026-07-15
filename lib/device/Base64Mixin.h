#ifndef BASE64MIXIN_H
#define BASE64MIXIN_H

#include "FujiDeviceMixin.h"

#ifdef FUJI_MIXINS_ENABLED
#define FUJI_BASE64_MIXIN_ENABLED

class Base64Mixin : public FujiDeviceMixin
{
protected:
    void encode_input(uint16_t len);
    void encode_compute();
    void encode_length();
    void encode_output(uint16_t len);
    void decode_input(uint16_t len);
    void decode_compute();
    void decode_length();
    void decode_output(uint16_t len);

 public:
    bool processCommand(const FUJI_COMMAND_PACKET &packet) override;
};

#endif // FUJI_MIXINS_ENABLED

#endif /* BASE64MIXIN_H */
