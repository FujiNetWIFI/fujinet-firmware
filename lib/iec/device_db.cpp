#include "device_db.h"

DeviceDB::DeviceDB(FS* fileSystem)
{
	m_fileSystem = fileSystem;
    m_dirty = false;

} // constructor

DeviceDB::~DeviceDB()
{

} // destructor

bool DeviceDB::init(String db_file)
{
    database = db_file;

    // initialize DB file
    Serial.println("Initialize device database");
    if( !m_fileSystem->exists(database) )
    {
        // Does folder exist? If not, create it
        int index = -1;
        int index2;
        String path;

#if defined(USE_LITTLEFS)
        do
        {
            index = database.indexOf('/', index + 1);
            index2 = database.indexOf('/', index + 1);
            path = database.substring(0, index2);
            if( !m_fileSystem->exists(path) && index2 > 0)
            {
                //Serial.printf("%d %d: %s\r\n", index, index2, path.c_str());
                m_fileSystem->mkdir(path);
            }
        } while (index2 > -1);
#endif

        Serial.printf("Creating Device Database [%s]\r\n", database.c_str());
        File f_database = m_fileSystem->open(database, "w+");
        if(!f_database)
        {
            Serial.printf("Error creating database.\r\n");
            return false;
        }
        else
        {
#if defined(ESP32)
            uint8_t buffer[RECORD_SIZE] = { 0 };
#elif defined(ESP8266)
            const char buffer[RECORD_SIZE] = { 0 };
#endif
            for(byte i = 0; i < 31; i++) // 22 devices x 2 drives = 44 records x 256 bytes = 11264 total bytes
            {
                sprintf( (char *)buffer, "{\"device\":%d,\"drive\":0,\"partition\":0,\"url\":\"\",\"path\":\"/\",\"image\":\"\"}", i );
                f_database.write(buffer, RECORD_SIZE);
                Serial.printf("Writing Record %d: %s\r\n", i, buffer);                    
            }
            Serial.printf("Database created!\r\n");
        }
        f_database.close();
    }
    return true;
}

bool DeviceDB::check()
{
    Serial.printf("Checking Device Database [%s]", database.c_str());
    File f_database = m_fileSystem->open(database, "r+");
    if(!f_database)
    {
        debugPrintf("\r\nDeviceDB::init unsable to open DB: %s", database.c_str());
        return false;
    }
    else
    {
        uint32_t offset;
        for(byte i=0; i < 31; i++)
        {
            // Select new record
            offset = i * RECORD_SIZE;
            if (f_database.seek( offset, SeekSet ))
            {
                debugPrintf("\r\nDeviceDB::init seek: %d, %.4X\r\n", i, offset);

                // Parse JSON object
                DeserializationError error = deserializeJson(m_device, f_database);
                if (error) {
                    Serial.print(F("\r\ndeserializeJson() failed: "));
                    Serial.println(error.c_str());
                }
                else
                {
                    debugPrintln(m_device.as<String>().c_str());
                }
            }
        }
        f_database.close();
    }
    return true;
}

bool DeviceDB::select(byte new_device)
{
    uint32_t offset;
    byte device = m_device["device"];

    if (new_device == device)
    {
        return true;
    }

    // Flush record to database
    save();

    File f_database = m_fileSystem->open(database, "r+");

    // Select new record
    offset = new_device * RECORD_SIZE;
    f_database.seek( offset, SeekSet );
    debugPrintf("\r\nDeviceDB::select seek: %d, %.4X", new_device, offset);

    // Parse JSON object
    DeserializationError error = deserializeJson(m_device, f_database);
    if (error) {
        Serial.print(F("\r\ndeserializeJson() failed: "));
        Serial.println(error.c_str());
        return false;
    }
    //m_device["device"] = new_device;

    f_database.close();
    return true;
}

bool DeviceDB::save()
{
    // Only save if dirty
    if ( m_dirty )
    {
        uint32_t offset;
        byte device = m_device["device"];

        File f_database = m_fileSystem->open(database, "r+");

        offset = device * RECORD_SIZE;
        debugPrintf("\r\nDeviceDB::select m_dirty: %d, %.4X", device, offset);
        f_database.seek( offset, SeekSet );
    #if defined(ESP32)
        f_database.write((const uint8_t *)m_device.as<String>().c_str(),strlen(m_device.as<String>().c_str()));
    #elif defined(ESP8266)
        f_database.write(m_device.as<String>().c_str());
    #endif
        m_dirty = false;
        f_database.close();
    }

    return true;
}

byte DeviceDB::device()
{
    return m_device["device"];
}
void DeviceDB::device(byte device)
{
    if(device != m_device["device"])
    {
        select(device);
        m_device["device"] = device;
    }
}

byte DeviceDB::drive()
{
    return m_device["drive"];
}
void DeviceDB::drive(byte drive)
{
    m_device["drive"] = drive;
}
byte DeviceDB::partition()
{
    return m_device["partition"];
}
void DeviceDB::partition(byte partition)
{
    m_device["partition"] = partition;
}
String DeviceDB::url()
{
    return m_device["url"];
}
void DeviceDB::url(String url)
{
    m_device["url"] = url;
    m_dirty = true;
}
String DeviceDB::path()
{
    return m_device["path"];
}
void DeviceDB::path(String path)
{
    path.replace(F("//"), F("/"));
    if ( path == NULL)
        path = "/";
    m_device["path"] = path;
    m_dirty = true;
}
String DeviceDB::image()
{
    return m_device["image"];
}
void DeviceDB::image(String image)
{
    if ( image == NULL)
        image = "";
    m_device["image"] = image;
    m_dirty = true;
}

