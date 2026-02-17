#include "TcpIpInitialize.h"
#include "base/embedded/interrupt/interrupt.h"
#include "lwip/tcpip.h"

bool volatile _tcp_ip_has_been_initialized = false;

void lwip::TcpIpInitialize()
{
	if (_tcp_ip_has_been_initialized)
	{
		return;
	}

	base::interrupt::GlobalInterruptionGuard g;
	_tcp_ip_has_been_initialized = true;
	tcpip_init(nullptr, nullptr);
}
