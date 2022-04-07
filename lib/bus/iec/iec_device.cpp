#ifdef BUILD_CBM

#include "iec_device.h"
#include "iec.h"
#include "wrappers/iec_buffer.h"

using namespace CBM;
using namespace Protocol;

namespace
{
	// Buffer for incoming and outgoing serial bytes and other stuff.
	char serCmdIOBuf[MAX_BYTES_PER_REQUEST];

} // unnamed namespace

iecDevice::iecDevice(IEC &iec)
	: m_iec(iec),
	m_iec_data(*reinterpret_cast<IEC::Data *>(&serCmdIOBuf[sizeof(serCmdIOBuf) / 2])),
	m_device(0)
{
	m_iec_data.content += '\0';
	reset();
} // ctor


void iecDevice::reset(void)
{
	//m_device.reset();
} // reset


uint8_t iecDevice::service(void)
{
	iecDevice::DeviceState r = DEVICE_IDLE;

	//#ifdef HAS_RESET_LINE
	//	if(m_iec.checkRESET()) {
	//		// IEC reset line is in reset device state
	//		reset();
	//
	//
	//		return IEC::BUS_RESET;
	//	}
	//#endif
	// Wait for it to get out of reset.
	//while (m_iec.checkRESET())
	//{
	//	Debug_println("BUS_RESET");
	//}

	//	noInterrupts();
	IEC::BusState bus_state = m_iec.service(m_iec_data);
	//	interrupts();


	if (bus_state == IEC::BUS_ERROR)
	{
		reset();
		bus_state = IEC::BUS_IDLE;
	}

	// Did anything happen from the controller side?
	else if (bus_state not_eq IEC::BUS_IDLE)
	{
		Debug_printf("DEVICE: [%d] ", m_iec_data.device);

		if (m_iec_data.command == IEC::IEC_OPEN)
		{
			Debug_printf("OPEN CHANNEL %d\r\n", m_iec_data.channel);
			if (m_iec_data.channel == 0)
				Debug_printf("LOAD \"%s\",%d\r\n", m_iec_data.content.c_str(), m_iec_data.device);
			else if (m_iec_data.channel == 1)
				Debug_printf("SAVE \"%s\",%d\r\n", m_iec_data.content.c_str(), m_iec_data.device);
			else {
				Debug_printf("OPEN #,%d,%d,\"%s\"\r\n", m_iec_data.device, m_iec_data.channel, m_iec_data.content.c_str());
			}

			// Open Named Channel
			handleOpen(m_iec_data);

			// Open either file or prg for reading, writing or single line command on the command channel.
			if (bus_state == IEC::BUS_COMMAND)
			{
				// Process a command
				Debug_printf("[Process a command]");
				handleListenCommand(m_iec_data);
			}
			else if (bus_state == IEC::BUS_LISTEN)
			{
				// Receive data
				Debug_printf("[Receive data]");
				handleListenData();	
			}
		}
		else if (m_iec_data.command == IEC::IEC_SECOND) // data channel opened
		{
			Debug_printf("DATA CHANNEL %d\r\n", m_iec_data.channel);
			if (bus_state == IEC::BUS_COMMAND)
			{
				// Process a command
				Debug_printf("[Process a command]");
				handleListenCommand(m_iec_data);
			}
			else if (bus_state == IEC::BUS_LISTEN)
			{
				// Receive data
				Debug_printf("[Receive data]");
				handleListenData();	
			}
			else if (bus_state == IEC::BUS_TALK)
			{
				// Send data
				Debug_printf("[Send data]");
				if (m_iec_data.channel == CMD_CHANNEL)
				{
					handleListenCommand(m_iec_data);		 // This is typically an empty command,
				}

				handleTalk(m_iec_data.channel);
			}
		}
		else if (m_iec_data.command == IEC::IEC_CLOSE)
		{
			Debug_printf("CLOSE CHANNEL %d\r\n", m_iec_data.channel);
			if(m_iec_data.channel > 0)
			{
				handleClose(m_iec_data);
			}			
		}
	}
	//Debug_printf("mode[%d] command[%.2X] channel[%.2X] state[%d]", mode, m_iec_data.command, m_iec_data.channel, m_openState);

	return bus_state;
} // service


Channel iecDevice::channelSelect(IEC::Data &iec_data) 
{
	size_t key = (iec_data.device * 100) + iec_data.channel;
	if(channels.find(key)!=channels.end()) {
		return channels.at(key);
	}

	// create and add channel if not found
	auto newChannel = Channel();
	newChannel.url = iec_data.content;
	Debug_printf("CHANNEL device[%d] channel[%d] url[%s]", iec_data.device, iec_data.channel, iec_data.content.c_str());

	channels.insert(std::make_pair(key, newChannel));
	return newChannel;
}

bool iecDevice::channelClose(IEC::Data &iec_data, bool close_all) 
{
	size_t key = (iec_data.device * 100) + iec_data.channel;
	if(channels.find(key)!=channels.end()) {
		return channels.erase(key);
	}

	return false;
}

#endif /* BUILD_CBM */