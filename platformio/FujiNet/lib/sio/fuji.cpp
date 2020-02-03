#include "fuji.h"

void sioFuji::sio_status()
{
    return;
}

void sioFuji::sio_process()
{
    //   cmdPtr[0xFD] = sio_net_scan_networks;
    //   cmdPtr[0xFC] = sio_net_scan_result;
    //   cmdPtr[0xFB] = sio_net_set_ssid;
    //   cmdPtr[0xFA] = sio_net_get_wifi_status;
    //   cmdPtr[0xF9] = sio_tnfs_mount_host;
    //   cmdPtr[0xF8] = sio_disk_image_mount;
    //   cmdPtr[0xF7] = sio_tnfs_open_directory;
    //   cmdPtr[0xF6] = sio_tnfs_read_directory_entry;
    //   cmdPtr[0xF5] = sio_tnfs_close_directory;
    //   cmdPtr[0xF4] = sio_read_hosts_slots;
    //   cmdPtr[0xF3] = sio_write_hosts_slots;
    //   cmdPtr[0xF2] = sio_read_device_slots;
    //   cmdPtr[0xF1] = sio_write_device_slots;
    //   cmdPtr[0xE9] = sio_disk_image_umount;
    //   cmdPtr[0xE8] = sio_get_adapter_config;
    //   cmdPtr[0xE7] = sio_new_disk;
    switch (cmdFrame.comnd)
    {
    case 'S':
        sio_ack();
        sio_status();
        break;
    default:
        sio_nak();
    }
}