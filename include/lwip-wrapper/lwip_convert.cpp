#include "lwip_convert.h"
#include <base/string/define.h>

ip_addr_t &base::operator<<(ip_addr_t &out, base::IPAddress const &in)
{
	try
	{
		base::Span span{
			reinterpret_cast<uint8_t *>(&out.addr),
			sizeof(out.addr),
		};

		span.CopyFrom(in.AsReadOnlySpan());

		/* base::IPAddress 类用小端序储存 IP 地址，而 lwip 的 ip_addr.addr
		 * 是用大端序，所以要翻转。
		 */
		span.Reverse();
		return out;
	}
	catch (std::exception const &e)
	{
		throw std::runtime_error{CODE_POS_STR + e.what()};
	}
}

base::IPAddress &base::operator<<(base::IPAddress &out, ip_addr_t const &in)
{
	try
	{
		base::ReadOnlySpan span{
			reinterpret_cast<uint8_t const *>(&in.addr),
			sizeof(in.addr),
		};

		out = base::IPAddress{std::endian::big, span};
		return out;
	}
	catch (std::exception const &e)
	{
		throw std::runtime_error{CODE_POS_STR + e.what()};
	}
}
