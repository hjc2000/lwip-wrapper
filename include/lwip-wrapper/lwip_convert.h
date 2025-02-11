#pragma once
#include <base/net/IPAddress.h>
#include <lwip/netif.h>

namespace base
{
	ip_addr_t To_ip_addr_t(base::IPAddress const &o);

	base::IPAddress ToIPAddress(ip_addr_t const &o);
} // namespace base
