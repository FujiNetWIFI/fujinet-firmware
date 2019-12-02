// based on SPIFFS.cpp

#include "tnfs.h" // chnanged to TNFS


TNFSFS::TNFSFS() : FS(FSImplPtr(new TNFSImpl()))
{
}

bool TNFSFS::begin(const char *server, int port, const char *basePath, uint8_t maxOpenFiles)
{
    /* if(esp_spiffs_mounted(NULL)){
        log_w("TNFS Already Mounted!");
        return true;
    }

    esp_vfs_spiffs_conf_t conf = {
      .base_path = basePath,
      .partition_label = NULL,
      .max_files = maxOpenFiles,
      .format_if_mount_failed = false
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if(err == ESP_FAIL && formatOnFail){
        if(format()){
            err = esp_vfs_spiffs_register(&conf);
        }
    }
    if(err != ESP_OK){
        log_e("Mounting TNFS failed! Error: %d", err);
        return false;
    } */
    tnfs_mount(server, port);
    _impl->mountpoint(basePath);
    return true;
}

    bool TNFSFS::format() { return false; }
    size_t TNFSFS::totalBytes() { return 0;}
    size_t TNFSFS::usedBytes() { return 0; }
    void TNFSFS::end() { }


/* from SPIFFS.cpp with "SPIFFS" replaced by "TNFS"
void TNFSFS::end()
{
    if(esp_spiffs_mounted(NULL)){
        esp_err_t err = esp_vfs_spiffs_unregister(NULL);
        if(err){
            log_e("Unmounting TNFS failed! Error: %d", err);
            return;
        }
        _impl->mountpoint(NULL);
    }
}

bool TNFSFS::format()
{
    disableCore0WDT();
    esp_err_t err = esp_spiffs_format(NULL);
    enableCore0WDT();
    if(err){
        log_e("Formatting TNFS failed! Error: %d", err);
        return false;
    }
    return true;
}

size_t TNFSFS::totalBytes()
{
    size_t total,used;
    if(esp_spiffs_info(NULL, &total, &used)){
        return 0;
    }
    return total;
}

size_t TNFSFS::usedBytes()
{
    size_t total,used;
    if(esp_spiffs_info(NULL, &total, &used)){
        return 0;
    }
    return used;
} */

TNFSFS TNFS;
