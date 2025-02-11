#pragma once
#include <base/net/IPAddress.h>
#include <lwip/netif.h>

namespace base
{
	ip_addr_t &operator<<(ip_addr_t &out, base::IPAddress const &in);

	base::IPAddress &operator<<(base::IPAddress &out, ip_addr_t const &in);
} // namespace base
