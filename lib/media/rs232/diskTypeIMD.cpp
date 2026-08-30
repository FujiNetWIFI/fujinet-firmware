#ifdef BUILD_RS232

#include "diskTypeIMD.h"

#include <string.h>

#include "../../include/debug.h"

// Deliberately no disk.h/bus.h/fnSystem.h: keeping this file to the media layer
// is what lets tests/MediaTypeIMDTests.cpp link it without a bus.

// Drive sector N is IMD LBA N. The FujiBus client addresses sectors 0-based:
// data/BUILD_RS232/autorun.img is a raw FAT12 image whose boot sector is at byte
// 0, and MediaTypeImg maps sector n to offset n * 512.
static inline uint32_t sector_to_lba(uint32_t sectornum)
{
    return sectornum;
}

static uint8_t imd_ctrl_bits(IMDStatus st)
{
    switch (st)
    {
    case IMDStatus::NoSuchSector:
    case IMDStatus::Unavailable: // never readable on the physical disk
        return DISK_CTRL_STATUS_SECTOR_MISSING;
    case IMDStatus::ReadOnly:
    case IMDStatus::WriteRefused: // record cannot absorb the data in place
        return DISK_CTRL_STATUS_WRITE_PROTECT_ERROR;
    default:
        return DISK_CTRL_STATUS_DATA_LOST;
    }
}

// Max + 1 rather than a distinct count: the PERCOM block describes a geometry
// rectangle, and this stays right when tracks are missing from the image.
void MediaTypeIMD::_derive_geometry()
{
    IMDTrackInfo t;
    uint8_t maxcyl = 0;
    uint8_t maxhead = 0;

    _spt = 0;
    _mfm = false;

    for (uint32_t i = 0; i < _imd.track_count(); i++)
    {
        if (!_imd.track_info(i, t))
            continue;
        if (t.cyl > maxcyl)
            maxcyl = t.cyl;
        if (t.head > maxhead)
            maxhead = t.head;
        if (t.nsec > _spt)
            _spt = t.nsec;
        if (imd_mode_is_mfm(t.mode))
            _mfm = true;
    }

    _cyls = (uint16_t)maxcyl + 1;
    _heads = (uint8_t)(maxhead + 1);
}

// A mixed-sector-size image has no honest single answer here; report the
// geometry rectangle and LBA 0's size. sector_size() remains exact.
void MediaTypeIMD::_fill_percom()
{
    memset(&_percomBlock, 0, sizeof(_percomBlock));

    _percomBlock.num_tracks = (uint8_t)(_cyls > 255 ? 255 : _cyls);
    _percomBlock.step_rate = 0x01;
    _percomBlock.sectors_per_trackH = (uint8_t)(_spt >> 8);
    _percomBlock.sectors_per_trackL = (uint8_t)(_spt & 0xFF);
    _percomBlock.num_sides = (uint8_t)(_heads - 1); // 0 = SS, 1 = DS
    _percomBlock.density = _mfm ? DENSITY_MFM : DENSITY_FM;
    _percomBlock.sector_sizeH = (uint8_t)(_disk_sector_size >> 8);
    _percomBlock.sector_sizeL = (uint8_t)(_disk_sector_size & 0xFF);
    _percomBlock.drive_present = 0xFF;
}

mediatype_t MediaTypeIMD::mount(fnFile *f, uint32_t disksize, fujiHost *host, const char *filename)
{
    Debug_printf("IMD MOUNT %s (%lu bytes, %s)\r\n", filename != nullptr ? filename : "?",
                 (unsigned long)disksize, _writable ? "rw" : "ro");

    // Take the handle first: on a failed mount the caller leaves the file open
    // and relies on this MediaType being deleted to close it.
    _disk_fileh = f;
    _disk_image_size = disksize;

    IMDStatus st = _imd.open(f, disksize, _writable);
    if (st != IMDStatus::Ok)
    {
        Debug_printf("IMD MOUNT failed: %s\r\n", imd_status_str(st));
        return MEDIATYPE_UNKNOWN;
    }

    // rs232_write() reads sector_size() bytes into the 512-byte _disk_sectorbuff,
    // so a larger sector would overrun it -- and read() could not fill it either.
    if (_imd.max_sector_size() > DISK_SECTORBUF_SIZE)
    {
        Debug_printf("IMD MOUNT refused: %u-byte sectors exceed the %u-byte sector buffer\r\n",
                     _imd.max_sector_size(), (unsigned)DISK_SECTORBUF_SIZE);
        _imd.close();
        return MEDIATYPE_UNKNOWN;
    }

    if (_imd.lba_count() == 0)
    {
        Debug_print("IMD MOUNT refused: no sectors\r\n");
        _imd.close();
        return MEDIATYPE_UNKNOWN;
    }

    _derive_geometry();

    _disk_num_sectors = _imd.lba_count();
    _disk_sector_size = _imd.sector_size(0);
    _base_ctrl_status = _writable ? DISK_CTRL_STATUS_CLEAR : DISK_CTRL_STATUS_WRITE_PROTECT_ERROR;
    _disk_controller_status = _base_ctrl_status;
    _disktype = MEDIATYPE_IMD;

    _fill_percom();

    Debug_printf("IMD: %lu sectors, %u cyl x %u head, %u spt, %s, %s sector size (max %u)\r\n",
                 (unsigned long)_imd.lba_count(), _cyls, _heads, _spt, _mfm ? "MFM" : "FM",
                 _imd.uniform_sector_size() ? "uniform" : "mixed", _imd.max_sector_size());

    if (_imd.trailing_garbage() != 0)
        Debug_printf("IMD: %lu trailing bytes ignored\r\n", (unsigned long)_imd.trailing_garbage());

    if (_imd.comment()[0] != '\0')
        Debug_printf("IMD comment: %s\r\n", _imd.comment());

    return _disktype;
}

// rs232Disk::unmount() calls this and then leaves _disk alive, so the borrowed
// handle has to be dropped before the base closes it. The destructor path needs
// nothing: ~IMDImage runs before ~MediaType.
void MediaTypeIMD::unmount()
{
    _imd.close();
    MediaType::unmount();
}

// Returns TRUE if an error condition occurred
error_is_true MediaTypeIMD::read(uint32_t sectornum, uint32_t *readcount)
{
    // No _disk_last_sector bookkeeping: that only exists to skip an fseek for
    // sequential reads, and IMDImage always seeks absolutely.
    *readcount = 0;
    _disk_controller_status = _base_ctrl_status;

    if (sectornum >= _imd.lba_count())
    {
        Debug_printf("IMD::read sector %lu >= %lu\r\n", (unsigned long)sectornum,
                     (unsigned long)_imd.lba_count());
        _disk_controller_status |= DISK_CTRL_STATUS_SECTOR_MISSING;
        RETURN_ERROR_AS_TRUE();
    }

    memset(_disk_sectorbuff, 0, sizeof(_disk_sectorbuff));

    uint16_t len = 0;
    IMDStatus st = _imd.read_sector(sector_to_lba(sectornum), _disk_sectorbuff,
                                    sizeof(_disk_sectorbuff), &len);
    if (st != IMDStatus::Ok)
    {
        Debug_printf("IMD::read lba %lu: %s\r\n", (unsigned long)sectornum, imd_status_str(st));
        _disk_controller_status |= imd_ctrl_bits(st);
        RETURN_ERROR_AS_TRUE();
    }

    // A data-error record still carries its recovered contents and a deleted
    // address mark is ordinary CP/M data. Deliver both and flag them in the
    // controller status: an error reply carries no payload at all.
    IMDSectorInfo si;
    if (_imd.sector_info(sector_to_lba(sectornum), si))
    {
        if (si.had_error)
            _disk_controller_status |= DISK_CTRL_STATUS_CRC_ERROR;
        if (si.deleted)
            _disk_controller_status |= DISK_CTRL_STATUS_SECTOR_DELETED;
    }

    *readcount = len;

    RETURN_SUCCESS_AS_FALSE();
}

// Returns TRUE if an error condition occurred
error_is_true MediaTypeIMD::write(uint32_t sectornum, bool verify)
{
    // `verify` is ignored, as it is by every other media type here.
    _disk_controller_status = _base_ctrl_status;

    if (!_writable)
    {
        Debug_print("IMD::write refused: mounted read-only\r\n");
        _disk_controller_status |= DISK_CTRL_STATUS_WRITE_PROTECT_ERROR;
        RETURN_ERROR_AS_TRUE();
    }

    if (sectornum >= _imd.lba_count())
    {
        Debug_printf("IMD::write sector %lu >= %lu\r\n", (unsigned long)sectornum,
                     (unsigned long)_imd.lba_count());
        _disk_controller_status |= DISK_CTRL_STATUS_SECTOR_MISSING;
        RETURN_ERROR_AS_TRUE();
    }

    // Separate an unreadable sector from a record that cannot take the data
    IMDSectorInfo si;
    if (_imd.sector_info(sector_to_lba(sectornum), si) && si.unavailable)
    {
        Debug_printf("IMD::write lba %lu is unavailable\r\n", (unsigned long)sectornum);
        _disk_controller_status |= DISK_CTRL_STATUS_SECTOR_MISSING;
        RETURN_ERROR_AS_TRUE();
    }

    IMDStatus st = _imd.write_sector(sector_to_lba(sectornum), _disk_sectorbuff,
                                     _imd.sector_size(sector_to_lba(sectornum)));
    if (st != IMDStatus::Ok)
    {
        Debug_printf("IMD::write lba %lu: %s\r\n", (unsigned long)sectornum, imd_status_str(st));
        _disk_controller_status |= imd_ctrl_bits(st);
        RETURN_ERROR_AS_TRUE();
    }

    // IMDImage::write_sector() already flushed
    RETURN_SUCCESS_AS_FALSE();
}

// Returns TRUE if an error condition occurred
error_is_true MediaTypeIMD::format(uint32_t *responsesize)
{
    // IMD records are variable length and written in place, so there is no
    // in-place format. Fail rather than report success without having done it.
    Debug_print("IMD FORMAT not supported\r\n");

    *responsesize = 0;

    RETURN_ERROR_AS_TRUE();
}

// 0 when out of range, which rs232_write() turns into a zero-length get that
// write() then rejects
uint16_t MediaTypeIMD::sector_size(uint32_t sectornum)
{
    return _imd.sector_size(sector_to_lba(sectornum));
}

void MediaTypeIMD::status(uint8_t statusbuff[4])
{
    statusbuff[0] = DISK_DRIVE_STATUS_CLEAR;

    if (_mfm)
        statusbuff[0] |= DISK_DRIVE_STATUS_DOUBLE_DENSITY;

    if (_heads > 1)
        statusbuff[0] |= DISK_DRIVE_STATUS_DOUBLE_SIDED;

    if (_spt == 26)
        statusbuff[0] |= DISK_DRIVE_STATUS_ENHANCED_DENSITY;

    if (!_writable)
        statusbuff[0] |= DISK_DRIVE_STATUS_WRITE_PROTECT_ERROR;

    statusbuff[1] = ~_disk_controller_status; // Negate the controller status
}

#endif /* BUILD_RS232 */
