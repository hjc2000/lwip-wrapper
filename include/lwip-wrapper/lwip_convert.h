#pragma once
#include <base/Convert.h>
#include <base/net/IPAddress.h>
#include <lwip/netif.h>

namespace base
{
	template <>
	ip_addr_t Convert<ip_addr_t, base::IPAddress>(base::IPAddress const &o);

	template <>
	base::IPAddress Convert<base::IPAddress, ip_addr_t>(ip_addr_t const &o);
} // namespace base
