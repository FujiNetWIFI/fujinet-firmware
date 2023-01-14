# WIFI Passphrase Encryption

This is disabled by default, and will reset to disabled on CONFIG reset (Power ON + B button)

In the WiFi UI configuration is an option to enable wifi passphrase encryptiong.

The option will enable/disable wifi passphrase encryption with a key unique to the FUJINET device and offers protection from observation of passphrases in INI files.

The encryption routine is taken from https://github.com/torvalds/uemacs 'micro emacs' and is isomorphic (encrypt/decrypt are mirror functions), as well as giving characters in the ascii domain to ensure no binary gets in the INI file.

# Recovering default state (if shit happens)

If you somehow change the MAC address of your FUJINET and want to reset back to normal, you can either reset your config (Power ON + B), or change the global `encrypt_passphrase` value back to 0, and using clear text for the passphrase in the INI file, as shown below:

```ini
[General]
encrypt_passphrase=0

[WiFi]
enabled=1
SSID=fckinloveatari
passphrase=your_plaintext_here
```

## Console Monitor Output
Output in serial monitor will show the decryption in action, for example:
```
11:15:12.979 > WiFiManager::start() complete
11:15:13.065 > WIFI_EVENT_STA_START
11:15:13.065 > Decrypting passphrase
11:15:13.065 > WiFi connect attempt to SSID "fishenburg"
11:15:13.069 > esp_wifi_connect returned 0
11:15:13.124 > WIFI_EVENT_STA_CONNECTED
11:15:14.957 > IP_EVENT_STA_GOT_IP
11:15:14.957 > Obtained IP address: 192.168.1.130
```

If passphrase encryption is not enabled, the line `Decrypting passphrase` will not show.
