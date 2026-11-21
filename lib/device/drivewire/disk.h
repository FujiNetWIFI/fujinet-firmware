#ifndef DISK_H
#define DISK_H

#include "FujiDiskDrive.h"
#include "rom/PMD85ROM.h"

#include <memory>

class drivewireDisk : public FujiDiskDrive
{
public:
    /**
     * @brief Is this virtualDevice holding the virtual disk drive used to boot CONFIG?
     */
    bool is_config_device = false;

    drivewireDisk();
    ~drivewireDisk();

    mediatype_t mediatype()
    {
        if (_module)
            return MEDIATYPE_MRM;
        return _media == nullptr ? MEDIATYPE_UNKNOWN : _media->_mediatype;
    };
    bool isMounted() const override { return _media != nullptr || _module != nullptr; }
    error_is_true mount(fnFile *f, const char *filename, uint32_t disksize,
                        disk_access_flags_t access_mode,
                        mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();

    void set_media_host(fujiHost *host);

    struct NewDiskRequest {
        uint8_t numDisks;
        uint8_t hostSlot;
        uint8_t deviceSlot;
        char filename[MAX_FILENAME_LEN];
    } __attribute__((packed));
    success_is_true write_blank(fnFile *f, const NewDiskRequest &req);

    error_is_true read(uint32_t sector, uint8_t *buf);
    error_is_true write(uint32_t sector, uint8_t *buf);

    void get_media_buffer(uint8_t **p_buffer, uint16_t *p_blk_size);
    uint8_t get_media_status();

private:
    // A PMD 85 module is read a page at a time and has no MediaType
    std::unique_ptr<PMD85ROM> _module;
    uint8_t _module_status = DISK_CTRL_STATUS_CLEAR;

    void release_module();
};

#endif
