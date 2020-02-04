#include "fuji.h"

void sioFuji::sio_status()
{
    return;
}

/**
   Scan for networks
*/
void sioFuji::sio_net_scan_networks()
{
    char ret[4] = {0, 0, 0, 0};

    // Scan to computer
    WiFi.mode(WIFI_STA);
    totalSSIDs = WiFi.scanNetworks();
    ret[0] = totalSSIDs;

    sio_to_computer((byte *)ret, 4, false);
}

/**
   Return scanned network entry
*/
void sioFuji::sio_net_scan_result()
{
  bool err = false;
  if (cmdFrame.aux1 < totalSSIDs)
  {
    strcpy(ssidInfo.ssid, WiFi.SSID(cmdFrame.aux1).c_str());
    ssidInfo.rssi = (char)WiFi.RSSI(cmdFrame.aux1);
  }
  else
  {
    memset(ssidInfo.rawData, 0x00, sizeof(ssidInfo.rawData));
    err = true;
  }

  sio_to_computer(ssidInfo.rawData, sizeof(ssidInfo.rawData), err);
}


/**
   Set SSID
*/
void sioFuji::sio_net_set_ssid()
{
  byte ck = sio_to_peripheral((byte *)&netConfig.rawData, sizeof(netConfig.rawData));

  if (sio_checksum(netConfig.rawData, sizeof(netConfig.rawData)) != ck)
  {
    sio_error();
  }
  else
  {
#ifdef DEBUG
    Debug_printf("Connecting to net: %s password: %s\n", netConfig.ssid, netConfig.password);
#endif
    WiFi.begin(netConfig.ssid, netConfig.password);
    // UDP.begin(16384); // move to TNFS.begin
    sio_complete();
  }
}

/**
   SIO get WiFi Status
*/
void sioFuji::sio_net_get_wifi_status()
{
  char wifiStatus = WiFi.status();

  // Update WiFi Status LED
  if (wifiStatus == WL_CONNECTED)
    wifi_led(true);
  else
    wifi_led(false);

  sio_to_computer((byte *)&wifiStatus, 1, false);
}

void sioFuji::sio_process()
{
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
    case 0xFD:
        sio_ack();
        sio_net_scan_networks();
        break;
    case 0xFC:
        sio_ack();
        sio_net_scan_result();
        break;
    case 0xFB:
        sio_ack();
        sio_net_set_ssid();  
        break;
    case 0xFA:
        sio_ack();
        sio_net_get_wifi_status();
        break;
    default:
        sio_nak();
    }
}

/**
   Set WiFi LED
*/
void sioFuji::wifi_led(bool onOff)
{
#ifdef ESP8266
  digitalWrite(PIN_LED, (onOff ? LOW : HIGH));
#elif defined(ESP32)
  digitalWrite(PIN_LED1, (onOff ? LOW : HIGH));
#endif
}
