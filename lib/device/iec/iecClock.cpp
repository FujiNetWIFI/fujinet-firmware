#ifdef BUILD_IEC

#include "iecClock.h"
#include "string_utils.h"
#include "debug.h"

// FujiNet commands come on channel 15, as on the bus's other virtualDevices
static bool is_raw_channel(uint8_t secondary)
{
    return (secondary & 0x0F) != 15;
}

iecClock::iecClock(uint8_t devnr) : virtualDevice(devnr)
{
    ts = 0;
    payload.clear();
    response.clear();
    responsePtr = 0;
}

iecClock::~iecClock()
{
}

void iecClock::set_timestamp(std::string s)
{
    Debug_printf("set_timestamp(%s)\r\n",s.c_str());
    ts = atoi(payload.c_str());
}

void iecClock::set_timestamp_format(std::string s)
{
    Debug_printf("set_timestamp_format(%s)\r\n",s.c_str());
    s = mstr::toUTF8(s);
    tf = s;
}

// fujinet-lib sends the time zone's length as a 16-bit param, then the name
std::optional<std::string> iecClock::fujidev_read_tz()
{
    std::optional<std::string> tz = read_tz_by_length(_packet->param16(0));

    if (tz)
        tz = SYSTEM_BUS.nativeTextToUnicode(*tz);
    return tz;
}

void iecClock::talk(uint8_t secondary)
{
  rawChannel = is_raw_channel(secondary);
  if (!rawChannel)
    {
      virtualDevice::talk(secondary);
      return;
    }

  // get current time in task() function, can't do it here 
  // since we are in timing-sensitive code
  responsePtr = 0xFFFFFFFF;
}


void iecClock::untalk()
{
  if (!rawChannel)
    {
      virtualDevice::untalk();
      return;
    }

  responsePtr = 0;
  response.clear();
}


void iecClock::listen(uint8_t secondary)
{
  rawChannel = is_raw_channel(secondary);
  if (!rawChannel)
    {
      virtualDevice::listen(secondary);
      return;
    }

  payload.clear();
}


void iecClock::unlisten()
{
  if (!rawChannel)
    {
      virtualDevice::unlisten();
      return;
    }

  if( !payload.empty() )
    {
      if (mstr::isNumeric(payload))
        set_timestamp(payload);
      else
        set_timestamp_format(payload);

      payload.clear();
    }
}


int8_t iecClock::canWrite()
{
  if (!rawChannel)
    return virtualDevice::canWrite();

  return 1;
}


int8_t iecClock::canRead()
{
  if (!rawChannel)
    return virtualDevice::canRead();

  if( responsePtr==0xFFFFFFFF )
    return -1; // response not yet set
  else
    return std::min((size_t) 2, response.size()-responsePtr);
}


void iecClock::write(uint8_t data, bool eoi)
{
  if (!rawChannel)
    {
      virtualDevice::write(data, eoi);
      return;
    }

  payload += char(data);
}


uint8_t iecClock::read()
{
  if (!rawChannel)
    return virtualDevice::read();

  // we should never get here if responsePtr>=response.size() because
  // then canRead would have returned 0, but better safe than sorry
  return responsePtr < response.size() ? response[responsePtr++] : 0;
}


void iecClock::task()
{
  // runs a FujiNet command once its LISTEN has ended
  virtualDevice::task();

  if( responsePtr==0xFFFFFFFF )
    {
      struct tm *info;
      char output[128];
      std::string s;

      if (!ts) // ts == 0, get current time
        time(&ts);
      else
        Debug_printf("using set time: %llu", ts);
    
      info = localtime(&ts);

      if (tf.empty())
        {
          Debug_printf("sending default time string.\r\n");
          s = std::string(asctime(info));
          mstr::replaceAll(s,":",".");
        }
      else
        {
          Debug_printf("Sending strftime of format %s\r\n",tf.c_str());
          strftime(output,sizeof(output),tf.c_str(),info);
          s = std::string(output);
        }

      mstr::toUpper(s);
      response = s;
      responsePtr = 0;
      ts = 0;
    }
}


void iecClock::reset()
{
  ts = 0;
  tf.clear();
  payload.clear();
  response.clear();
  responsePtr = 0;
}


#endif /* BUILD_IEC */
