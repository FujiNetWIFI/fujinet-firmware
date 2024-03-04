#ifdef BUILD_APPLE
#ifdef DEV_RELAY_SLIP

#include "connector_com.h"
#include <libserialport.h>
#include <iostream>
#include "fnConfig.h"

sp_parity parity_from_int(int parity_int)
{
	sp_parity parity;
	switch (parity_int)
	{
	case 0:
		parity = SP_PARITY_NONE;
		break;
	case 1:
		parity = SP_PARITY_ODD;
		break;
	case 2:
		parity = SP_PARITY_EVEN;
		break;
	case 3:
		parity = SP_PARITY_MARK;
		break;
	case 4:
		parity = SP_PARITY_SPACE;
		break;
	default:
		parity = SP_PARITY_NONE; // Default / in case of invalid input
	}
	return parity;
}

sp_flowcontrol flowcontrol_from_int(int flowcontrol_int)
{
	sp_flowcontrol flowcontrol;
	switch (flowcontrol_int)
	{
	case 0:
		flowcontrol = SP_FLOWCONTROL_NONE;
		break;
	case 1:
		flowcontrol = SP_FLOWCONTROL_XONXOFF;
		break;
	case 2:
		flowcontrol = SP_FLOWCONTROL_RTSCTS;
		break;
	case 3:
		flowcontrol = SP_FLOWCONTROL_DTRDSR;
		break;
	default:
		flowcontrol = SP_FLOWCONTROL_NONE; // Default / in case of invalid input
	}
	return flowcontrol;
}

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
	Debug_printf("SP OVER SLIP, COM - Creating connection to target to service devices\n");

	std::string port_name = Config.get_bos_port_name();
	Debug_printf("COM: port_name = %s\n", port_name.c_str());
	struct sp_port *port = nullptr;
	enum sp_return result = sp_get_port_by_name(port_name.c_str(), &port);
	if (result != SP_OK)
	{
		std::ostringstream msg;
		msg << "Unable to get serial port from the given name: " << port_name.c_str();
		throw std::runtime_error(msg.str());
	}

	sp_set_baudrate(port, Config.get_bos_baud());
	sp_set_bits(port, Config.get_bos_bits());
	sp_set_parity(port, parity_from_int(Config.get_bos_parity()));
	sp_set_stopbits(port, Config.get_bos_stop_bits());
	sp_set_flowcontrol(port, flowcontrol_from_int(Config.get_bos_flowcontrol()));
	
	Debug_printf("COM: baud = %d\n", Config.get_bos_baud());
	Debug_printf("COM: bits = %d\n", Config.get_bos_bits());
	Debug_printf("COM: parity = %d\n", Config.get_bos_parity());
	Debug_printf("COM: stopbits = %d\n", Config.get_bos_stop_bits());
	Debug_printf("COM: flow_control = %d\n", Config.get_bos_flowcontrol());

	result = sp_open(port, SP_MODE_READ_WRITE);
	if (result != SP_OK)
	{
		cleanup_port(port);
		std::ostringstream msg;
		msg << "Unable to get open serial port: " << port_name.c_str();
		throw std::runtime_error(msg.str());
	}

	// we now need to do some handshake to check the other end is what we expect it to be, as so far, all we've done is open a port.
	

	auto conn = std::make_shared<COMConnection>(port_name, port, true);
	return conn;
}

#endif
#endif