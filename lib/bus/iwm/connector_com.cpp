#ifdef BUILD_APPLE
#ifdef DEV_RELAY_SLIP

#include "connector_com.h"
#include <libserialport.h>
#include <iostream>
#include "debug.h"

void cleanup_port(struct sp_port *port)
{
	if (port != nullptr)
	{
		sp_close(port);
		sp_free_port(port);
	}
}

std::shared_ptr<Connection> connector_com::create_connection()
{
	struct sp_port **port_list;
	enum sp_return result = sp_list_ports(&port_list);
	if (result != SP_OK)
	{
		std::ostringstream msg;
		msg << "Unable to list serial ports";
		throw std::runtime_error(msg.str());
	}

	struct sp_port *port = nullptr;
	for (int i = 0; port_list[i] != NULL; i++)
	{
		if (sp_get_port_transport(port_list[i]) == SP_TRANSPORT_USB)
		{
			result = sp_copy_port(port_list[i], &port);
			if (result != SP_OK)
			{
				std::ostringstream msg;
				msg << "Unable to copy serial port";
				throw std::runtime_error(msg.str());
			}
			break;
		}
	}
	sp_free_port_list(port_list);
	if (port == nullptr)
	{
		return nullptr;
	}

	std::string port_name = sp_get_port_name(port);
	Debug_printf("COM: port_name = %s\n", port_name.c_str());

	for (int i = 0; i < 10; i++)
	{
	 	usleep(10 * 1000);
		Debug_printf(".");
		result = sp_open(port, SP_MODE_READ_WRITE);
		if (result == SP_OK)
		{
			break;
		}
	}
	if (result != SP_OK)
	{
		cleanup_port(port);
		std::ostringstream msg;
		msg << "Unable to open serial port: " << port_name.c_str();
		throw std::runtime_error(msg.str());
	}

	result = sp_set_dtr(port, SP_DTR_ON);
	if (result != SP_OK)
	{
		cleanup_port(port);
		std::ostringstream msg;
		msg << "Unable to set DTR on serial port: " << port_name.c_str();
		throw std::runtime_error(msg.str());
	}

	auto conn = std::make_shared<COMConnection>(port_name, port, true);
	conn->create_read_channel();
	return conn;
}

#endif
#endif