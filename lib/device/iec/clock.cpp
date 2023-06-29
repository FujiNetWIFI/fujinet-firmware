#ifdef BUILD_IEC

#include "clock.h"
#include "string_utils.h"

iecClock::iecClock()
{
    ts = 0;
}

iecClock::~iecClock()
{
}

void iecClock::set_timestamp(std::string s)
{
    Debug_printf("set_timestamp(%s)\n",s.c_str());
    ts = atoi(payload.c_str());
}

void iecClock::set_timestamp_format(std::string s)
{
    Debug_printf("set_timestamp_format(%s)\n",s.c_str());
    mstr::toASCII(s);
    tf = s;
}

bool is_number(const std::string& s)
{
    return !s.empty() && std::find_if(s.begin(), 
        s.end(), [](unsigned char c) { return !std::isdigit(c); }) == s.end();
}

device_state_t iecClock::process()
{
    virtualDevice::process();

    switch (commanddata.secondary)
    {
    case IEC_OPEN:
        iec_open();
        break;
    case IEC_CLOSE:
        iec_close();
        break;
    case IEC_REOPEN:
        iec_reopen();
        break;
    default:
        break;
    }

    return device_state;
}

void iecClock::iec_open()
{
    if (is_number(payload))
        set_timestamp(payload);
    else
        set_timestamp_format(payload);
}

void iecClock::iec_close()
{

}

void iecClock::iec_reopen()
{
    switch (commanddata.primary)
    {
    case IEC_TALK:
        iec_reopen_talk();
        break;
    case IEC_LISTEN:
        iec_reopen_listen();
        break;
    }
}

void iecClock::iec_reopen_listen()
{
    std::string s;

    Debug_printf("IEC REOPEN LISTEN\n");

    while (!(IEC.flags & EOI_RECVD))
    {
        int16_t b = IEC.receiveByte();

        Debug_printf("%02X %c\n",b,b);

        if (b<0)
        {
            Debug_printf("Error on receive.\n");
            return;
        }
        else
            s.push_back(b);
    }

    mstr::toASCII(s);

    Debug_printf("Sending over %s\n",s.c_str());

    if (is_number(s))
        set_timestamp(s);
    else
        set_timestamp_format(s);
}

void iecClock::iec_reopen_talk()
{
    struct tm *info;
    char output[128];
    std::string s;

    if (!ts) // ts == 0, get current time
        time(&ts);
    
    info = localtime(&ts);

    if (tf.empty())
    {
        s = std::string(asctime(info));
        mstr::replaceAll(s,":",".");
    }
    else
    {
        strftime(output,sizeof(output),tf.c_str(),info);
    }
    
    mstr::toUpper(s);
    
    IEC.sendBytes(s,true);
}

#endif /* BUILD_IEC */