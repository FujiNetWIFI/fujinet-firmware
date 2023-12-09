#ifndef DLOAD_H
#define DLOAD_H

#include "../../include/pinmap.h"
#include "bus.h"
#include "fnSystem.h"

class drivewireDload : public virtualDevice
{
public:
    /**
     * @brief DLOAD state machine, called from service loop.
     */
    virtual void dload_process();

protected:

private:

    /**
     * @brief internal file pointer
     */
    FILE *fp = NULL;

    /**
     * @brief current block number
     */
    uint16_t blockNum = 0;

    /**
     * @brief calculate xor sum from buf of len
     * @param buf ptr to The buffer to check
     * @param len length of buffer, must be <= size of buf
     */
    static inline uint8_t xor_sum(uint8_t *buf, uint16_t len);

    /**
     * @brief Get PFILR (file request) from COCO
     */
    virtual void pfilr_from_coco();

    /**
     * @brief Put PFILR (file request) to COCO
     */
    virtual void pfilr_to_coco();

    /**
     * @brief retrieve the filename and its XOR checksum
     */
    virtual void get_filename();

    /**
     * @brief filename XOR checksum is invalid, send P.NAK
     */
    virtual void invalid_filename();

    /**
     * @brief filename XOR checksum is valid, send P.ACK
     */
    virtual void valid_filename();

    /**
     * @brief Send filetype and ascii flag, along with XOR sum
     */
    virtual void send_filetype();

    /**
     * @brief reply with file not found
     */
    virtual void send_filetype_file_not_found();

    /**
     * @brief send binary filetype binary reply
     */
    virtual void send_filetype_binary();

    /**
     * @brief wait for block request
     */
    virtual void pblkr_from_coco();

    /**
     * @brief acknowledge block request
     */
    virtual void pblkr_to_coco();

    /**
     * @brief get block number
     */
    virtual void get_block_number();

    /**
     * @brief return P.NAK on block request (checksum failure)
     */
    virtual void get_p_nak();
    
    /**
     * @brief return P.ACK on block request (valid block #)
     */
    virtual void get_p_ack();

    /**
     * @brief Put requested block to CoCo
     */
    virtual void put_block();

    /**
     * @brief CoCo Asked for an abort.
     */
    virtual void pabrt_from_coco();

};

#endif /* DLOAD_H */