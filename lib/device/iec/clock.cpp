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
    s = mstr::toUTF8(s);
    tf = s;
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
    if (mstr::isNumeric(payload))
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
    Debug_printf("IEC REOPEN LISTEN\n");

    //mstr::toASCII(payload);

    Debug_printf("Sending over %s\n",payload.c_str());

    if (mstr::isNumeric(payload))
        set_timestamp(payload);
    else
        set_timestamp_format(payload);
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
        Debug_printf("sending default time string.\n");
        s = std::string(asctime(info));
        mstr::replaceAll(s,":",".");
    }
    else
    {
        Debug_printf("Sending strftime of format %s\n",tf.c_str());
        strftime(output,sizeof(output),tf.c_str(),info);
        s = std::string(output);
    }
    
    mstr::toUpper(s);
    
    IEC.sendBytes(s, true);
}

#endif /* BUILD_IEC */