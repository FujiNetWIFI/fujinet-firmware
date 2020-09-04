#include "cassette.h"
#include "../../include/debug.h"

#define CASSETTE_FILE "test.cas"

void sioCassette::open_cassette_file(FileSystem *filesystem)
{
    _FS = filesystem;
    if (_file != nullptr)
        fclose(_file);
    _file = _FS->file_open(CASSETTE_FILE, "r"); // This should create/truncate the file

#ifdef DEBUG
    if (_file != nullptr)
        Debug_println("CAS file opened succesfully!");
    else
        Debug_println("Could not open CAS file :(");
#endif
}

void sioCassette::sio_enable_cassette()
{
    // open a file
    // TBD

    // Change baud rate
    fnUartSIO.set_baudrate(CASSETTE_BAUD);
    cassetteActive = true;

#ifdef DEBUG
    Debug_println("Cassette Mode enabled");
#endif
}

void sioCassette::sio_disable_cassette()
{
    // close the file
    // TBD

    fnUartSIO.set_baudrate(SIO_STANDARD_BAUDRATE);
    cassetteActive = false;
}

void sioCassette::sio_handle_cassette()
{
    if (fnSystem.digital_read(PIN_MTR) == DIGI_LOW)
        return;

    // if there’s data available, read bytes from file

    // now send to UART:
    //fnUartSIO.write(buf1, packetSize);
#ifdef DEBUG
    Debug_print("CASSETTE-OUT: ");
    //Debug_println((char *)buf1);
#endif

    if (fnUartSIO.available())
    {
        // read the data until pause:
        fnUartSIO.read(); // Toss the data if motor or command is asserted
    }
    else
    {
        while (1)
        {
            if (fnUartSIO.available())
            {
      //          buf2[i2] = (char)fnUartSIO.read(); // read char from UART
       //         if (i2 < Cassette_BUFFER_SIZE - 1)
                 //   i2++;
            }
            else
            {
        //        fnSystem.delay_microseconds(Cassette_PACKET_TIMEOUT);
                if (!fnUartSIO.available())
                    break;
            }
        }

        // write to file

#ifdef DEBUG
        Debug_print("CAS-OUT: ");
    //    Debug_println((char *)buf2);
#endif

    //    i2 = 0;
    }
}

void sioCassette::sio_status()
{
    // Nothing to do here
    return;
}

void sioCassette::sio_process(uint32_t commanddata, uint8_t checksum)
{
    // Nothing to do here
    return;
}
