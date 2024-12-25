#include "TcpIpInitialize.h"
#include <bsp-interface/di/interrupt.h>
#include <lwip/tcpip.h>

bool volatile _tcp_ip_has_been_initialized = false;

void lwip::TcpIpInitialize()
{
	if (_tcp_ip_has_been_initialized)
	{
		return;
	}

	DI_DoGlobalCriticalWork(
		[]
		{
			_tcp_ip_has_been_initialized = true;
			tcpip_init(nullptr, nullptr);
		});
}
