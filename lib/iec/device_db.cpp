#include "device_db.h"
#include "../utils/utils.h"

DeviceDB::DeviceDB()//(FileSystem *fs)
{
    //m_fileSystem = fs;
    m_dirty = false;

} // constructor

DeviceDB::~DeviceDB()
{

} // destructor

bool DeviceDB::init(std::string db_file, FileSystem *fs)
{
    // database = db_file;
    // m_fileSystem = fs;
    
    // // initialize DB file
    // Debug_println("Initialize device database");
    // if (!m_fileSystem->exists(database.c_str()))
    // {
    //     // Does folder exist? If not, create it
    //     int index = -1;
    //     int index2;
    //     std::string path;

    //     do
    //     {
    //         index = database.find('/', index + 1);
    //         index2 = database.find('/', index + 1);
    //         path = database.substr(0, index2);
    //         if( !m_fileSystem->exists(path.c_str()) && index2 > 0)
    //         {
    //             //Debug_printf("%d %d: %s\r\n", index, index2, path.c_str());
    //             // TODO m_fileSystem->mkdir(path);
    //         }
    //     } while (index2 > -1);

    //     Debug_printf("Creating Device Database [%s]\r\n", database.c_str());
    //     FILE *f_database = m_fileSystem->file_open(database.c_str(), FILE_WRITE);
    //     if(!f_database)
    //     {
    //         Debug_printf("Error creating database.\r\n");
    //         return false;
    //     }
    //     else
    //     {
    //         const char buffer[RECORD_SIZE] = {0};
    //         for (uint8_t i = 0; i < 31; i++) // 22 devices x 2 drives = 44 records x 256 bytes = 11264 total bytes
    //         {
    //             sprintf( (char *)buffer, "{\"device\":%d,\"drive\":0,\"partition\":0,\"url\":\"\",\"path\":\"/\",\"image\":\"\"}", i );
    //             fwrite(buffer, RECORD_SIZE, 1, f_database);
    //             //f_database->write(, RECORD_SIZE);
    //             Debug_printf("Writing Record %d: %s\r\n", i, buffer);
    //         }
    //         Debug_printf("Database created!\r\n");
    //     }
    //     fclose(f_database);
    // }
    return true;
}

bool DeviceDB::check()
{
//    Debug_printf("Checking Device Database [%s]", database.c_str());
//     FILE *f_database = m_fileSystem->file_open(database.c_str(), "r+");
//     if(!f_database)
//     {
//         Debug_printf("\r\nDeviceDB::init unsable to open DB: %s", database.c_str());
//         return false;
//     }
//     else
//     {
//         uint32_t offset;
//         for (uint8_t i = 0; i < 31; i++)
//         {
//             // Select new record
//             offset = i * RECORD_SIZE;
//             if (fseek(f_database, offset, SEEK_SET))
//             {
//                 Debug_printf("\r\nDeviceDB::init seek: %d, %.4X\r\n", i, offset);

//                 // Parse JSON object
// /*                 DeserializationError error = deserializeJson(m_device, f_database);
//                 if (error) {
//                     Debug_print("\r\ndeserializeJson() failed: ");
//                     Debug_println(error.c_str());
//                 }
//                 else
//                 {
//                     Debug_println(m_device.as);
//                 }
//  */            }
//         }
//         fclose(f_database);
//     }
    return true;
}

bool DeviceDB::select(uint8_t new_device)
{
//     uint32_t offset;
//     uint8_t device = 8; //m_device["device"];

//     if (new_device == device)
//     {
//         return true;
//     }

//     // Flush record to database
//     save();

//     FILE *f_database = m_fileSystem->file_open(database.c_str(), "r+");

//     // Select new record
//     offset = new_device * RECORD_SIZE;
//     fseek(f_database, offset, SEEK_SET);
//     Debug_printf("\r\nDeviceDB::select seek: %d, %.4X", new_device, offset);

// /* 
//     // Parse JSON object
//     DeserializationError error = deserializeJson(m_device, f_database);
//     if (error) {
//         Debug_print("\r\ndeserializeJson() failed: ");
//         Debug_println(error.c_str());
//         return false;
//     }
//     //m_device["device"] = new_device;
//  */

//     fclose(f_database);
    return true;
}

bool DeviceDB::save()
{
    // // Only save if dirty
    // if ( m_dirty )
    // {
    //     uint32_t offset;
    //     uint8_t device = 8;
    //    // m_device["device"];

    //     FILE *f_database = m_fileSystem->file_open(database.c_str(), "r+");

    //     offset = device * RECORD_SIZE;
    //     Debug_printf("\r\nDeviceDB::select m_dirty: %d, %.4X", device, offset);
    //     fseek(f_database, offset, SEEK_SET);
    //     // fprintf(f_database, "%10ud", m_device);
    //     m_dirty = false;
    //     fclose(f_database);
    // }

    return true;
}

uint8_t DeviceDB::device()
{
    return 8;
    //m_device["device"];
}
void DeviceDB::device(uint8_t device)
{
    /* if(device != m_device["device"])
    {
        select(device);
        m_device["device"] = device;
    }
 */
}

uint8_t DeviceDB::drive()
{
    return 0;
   // m_device["drive"];
}
void DeviceDB::drive(uint8_t drive)
{
    // m_device["drive"] = drive;
}
uint8_t DeviceDB::partition()
{
    return 0;
    // m_device["partition"];
}
void DeviceDB::partition(uint8_t partition)
{
    // m_device["partition"] = partition;
}
std::string DeviceDB::url()
{
    return "";
    // m_device["url"];
}
void DeviceDB::url(std::string url)
{
    // m_device["url"] = url;
    // m_dirty = true;
}
std::string DeviceDB::path()
{
    return "/";
    // m_device["path"];
}
void DeviceDB::path(std::string path)
{
    //path.replace("//", "/");
    util_replaceAll(path, "//", "/");
    if (path.empty())
        path = "/";
    // m_device["path"] = path;
    // m_dirty = true;
}
std::string DeviceDB::image()
{
    return "";
    // m_device["image"];
}
void DeviceDB::image(std::string image)
{
    if (image.empty())
        image = "";
    //m_device["image"] = image;
    //m_dirty = true;
}

