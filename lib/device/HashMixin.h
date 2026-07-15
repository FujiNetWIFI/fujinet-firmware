#ifndef HASHMIXIN_H
#define HASHMIXIN_H

#include "FujiDeviceMixin.h"
#include "hash.h"

#ifdef FUJI_MIXINS_ENABLED
#define FUJI_HASH_MIXIN_ENABLED

class HashMixin : public FujiDeviceMixin
{
protected:
    Hash::Algorithm _algorithm = Hash::Algorithm::UNKNOWN;

    void hash_input(uint16_t len);
    void hash_compute(bool clear_data, Hash::Algorithm algo);
    void hash_length(bool as_hex);
    void hash_output(bool as_hex);
    void hash_clear();

public:
    bool processCommand(const FUJI_COMMAND_PACKET &packet) override;
};

#endif // FUJI_MIXINS_ENABLED

#endif /* HASHMIXIN_H */
