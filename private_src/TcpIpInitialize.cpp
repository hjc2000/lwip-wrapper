#include "TcpIpInitialize.h"
#include "lwip/tcpip.h"

bool volatile _tcp_ip_has_been_initialized = false;

void lwip::TcpIpInitialize()
{
	if (_tcp_ip_has_been_initialized)
	{
		return;
	}

	_tcp_ip_has_been_initialized = true;
	tcpip_init(nullptr, nullptr);
}
