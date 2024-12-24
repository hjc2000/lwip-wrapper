#include "lwip_convert.h"
#include <base/string/define.h>

template <>
ip_addr_t base::Convert(base::IPAddress const &o)
{
	try
	{
		ip_addr_t ip{};

		base::Span span{
			reinterpret_cast<uint8_t *>(&ip.addr),
			sizeof(ip.addr),
		};

		span.CopyFrom(o.AsReadOnlySpan());

		/* base::IPAddress 类用小端序储存 IP 地址，而 lwip 的 ip_addr.addr
		 * 是用大端序，所以要翻转。
		 */
		span.Reverse();
		return ip;
	}
	catch (std::exception const &e)
	{
		std::string message = std::string{CODE_POS_STR};
		throw std::runtime_error{message + e.what()};
	}
}

template <>
base::IPAddress base::Convert(ip_addr_t const &o)
{
	try
	{
		base::ReadOnlySpan span{
			reinterpret_cast<uint8_t const *>(&o.addr),
			sizeof(o.addr),
		};

		return base::IPAddress{std::endian::big, span};
	}
	catch (std::exception const &e)
	{
		std::string message = std::string{CODE_POS_STR};
		throw std::runtime_error{message + e.what()};
	}
}
