#include "NetifWrapper.h"
#include <base/container/List.h>
#include <base/string/define.h>
#include <bsp-interface/di/console.h>
#include <bsp-interface/di/delayer.h>
#include <bsp-interface/di/task.h>
#include <lwip-wrapper/lwip_convert.h>
#include <lwip-wrapper/NetifSlot.h>
#include <lwip/dhcp.h>
#include <lwip/etharp.h>
#include <lwip/tcpip.h>

struct lwip::NetifWrapper::Cache
{
	/// @brief 本机IP地址
	base::IPAddress _ip_address{"192.168.1.30"};

	base::IPAddress _netmask{"255.255.255.0"};

	base::IPAddress _gateway{"192.168.1.1"};

	/// @brief 本网卡所使用的 MAC 地址。
	base::Mac _mac{
		std::endian::big,
		base::Array<uint8_t, 6>{
			0xB8,
			0xAE,
			0x1D,
			0x00,
			0x04,
			0x00,
		},
	};

	int32_t _mtu = 1500;
};

void lwip::NetifWrapper::InitializationCallbackFunc()
{
	_wrapped_obj->hostname = "lwip-wrapper";
	_wrapped_obj->name[0] = 'i';
	_wrapped_obj->name[1] = 'p';

	// 设置 MAC 地址长度，为 6 个字节
	_wrapped_obj->hwaddr_len = ETHARP_HWADDR_LEN;

	SetMac(_cache->_mac);
	_wrapped_obj->mtu = _cache->_mtu;

	/* 网卡状态信息标志位，是很重要的控制字段，它包括网卡功能使能、广播
	 * 使能、 ARP 使能等等重要控制位
	 */
	_wrapped_obj->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

	/* We directly use etharp_output() here to save a function call.
	 * You can instead declare your own function an call etharp_output()
	 * from it if you have to do some checks before sending (e.g. if link
	 * is available...)
	 */
	_wrapped_obj->output = etharp_output;

	_wrapped_obj->linkoutput = [](netif *net_interface, pbuf *p) -> err_t
	{
		try
		{
			reinterpret_cast<NetifWrapper *>(net_interface->state)->SendPbuf(p);
			return err_enum_t::ERR_OK;
		}
		catch (std::exception const &e)
		{
			return err_enum_t::ERR_IF;
		}
	};
}

void lwip::NetifWrapper::SendPbuf(pbuf *p)
{
	if (_ethernet_port == nullptr)
	{
		throw std::runtime_error{"必须先调用 Open 方法传入一个 bsp::IEthernetPort 对象"};
	}

	pbuf *current_pbuf;
	base::List<base::ReadOnlySpan> spans{};

	for (current_pbuf = p; current_pbuf != nullptr; current_pbuf = current_pbuf->next)
	{
		base::ReadOnlySpan span{
			reinterpret_cast<uint8_t *>(current_pbuf->payload),
			current_pbuf->len,
		};

		spans.Add(span);
	}

	_ethernet_port->Send(spans);
}

#pragma region DHCP

bool lwip::NetifWrapper::TryDHCP()
{
	DI_Console().WriteLine("开始进行 DHCP.");
	ClearAllAddress();
	StartDHCP();

	bool dhcp_result = false;
	for (int i = 0; i < 100; i++)
	{
		// 如果失败，最多重试 100 次。
		dhcp_result = DhcpSuppliedAddress();
		if (dhcp_result)
		{
			break;
		}

		DI_Delayer().Delay(std::chrono::milliseconds{100});
	}

	if (!dhcp_result)
	{
		// 使用静态IP地址
		SetIPAddress(_cache->_ip_address);
		SetNetmask(_cache->_netmask);
		SetGateway(_cache->_gateway);
		DI_Console().WriteLine("DHCP 超时：");
		DI_Console().WriteLine("使用静态 IP 地址：" + IPAddress().ToString());
		DI_Console().WriteLine("使用静态子网掩码：" + Netmask().ToString());
		DI_Console().WriteLine("使用静态网关：" + Gateway().ToString());
		return false;
	}

	// DHCP 成功
	_cache->_ip_address = IPAddress();
	_cache->_netmask = Netmask();
	_cache->_gateway = Gateway();
	DI_Console().WriteLine("DHCP 成功：");
	DI_Console().WriteLine("通过 DHCP 获取到 IP 地址：" + _cache->_ip_address.ToString());
	DI_Console().WriteLine("通过 DHCP 获取到子网掩码：" + _cache->_netmask.ToString());
	DI_Console().WriteLine("通过 DHCP 获取到的默认网关：" + _cache->_gateway.ToString());
	return true;
}

void lwip::NetifWrapper::StartDHCP()
{
	dhcp_start(_wrapped_obj.get());
}

void lwip::NetifWrapper::StopDHCP()
{
	dhcp_stop(_wrapped_obj.get());
}

bool lwip::NetifWrapper::DhcpSuppliedAddress()
{
	return dhcp_supplied_address(_wrapped_obj.get());
}

#pragma endregion

#pragma region 线程函数

void lwip::NetifWrapper::LinkStateDetectingThreadFunc()
{
	while (true)
	{
		bool is_linked = _ethernet_port->IsLinked();
		if (is_linked == netif_is_up(_wrapped_obj.get()))
		{
			DI_Delayer().Delay(std::chrono::milliseconds{100});
			continue;
		}

		if (is_linked)
		{
			// 开启以太网及虚拟网卡
			DI_Console().WriteLine("检测到网线插入");
			_ethernet_port->Restart();
			netif_set_up(_wrapped_obj.get());
			netif_set_link_up(_wrapped_obj.get());
			TryDHCP();
		}
		else
		{
			StopDHCP();

			/* LWIP_DHCP */
			DI_Console().WriteLine("检测到网线断开。");
			netif_set_down(_wrapped_obj.get());
			netif_set_link_down(_wrapped_obj.get());
		}
	}
}

void lwip::NetifWrapper::InputThreadFunc()
{
	while (true)
	{
		base::IEnumerable<base::ReadOnlySpan> const &spans = _ethernet_port->Receive();
		pbuf *head_pbuf = nullptr;
		pbuf *current_pbuf = nullptr;

		for (base::ReadOnlySpan const &span : spans)
		{
			pbuf_custom *custom_pbuf = new pbuf_custom{};
			custom_pbuf->custom_free_function = [](pbuf *p)
			{
				delete reinterpret_cast<pbuf_custom *>(p);
			};

			pbuf *p = pbuf_alloced_custom(PBUF_RAW,
										  span.Size(),
										  PBUF_REF,
										  custom_pbuf,
										  const_cast<uint8_t *>(span.Buffer()),
										  span.Size());

			p->next = nullptr;
			if (head_pbuf == nullptr)
			{
				head_pbuf = p;
				current_pbuf = p;
			}
			else
			{
				current_pbuf->next = p;
				current_pbuf = p;
			}
		}

		if (head_pbuf == nullptr)
		{
			continue;
		}

		// 串成链表后一次性输入。
		err_t input_result = _wrapped_obj->input(head_pbuf, _wrapped_obj.get());
		if (input_result != err_enum_t::ERR_OK)
		{
			// 输入发生错误，释放 pbuf 链表。
			pbuf_free(head_pbuf);
		}
	}
}

#pragma endregion

netif *lwip::NetifWrapper::WrappedObj() const
{
	return _wrapped_obj.get();
}

lwip::NetifWrapper::NetifWrapper()
{
	_wrapped_obj->state = this;
	_cache = std::shared_ptr<lwip::NetifWrapper::Cache>{new Cache{}};
}

lwip::NetifWrapper::~NetifWrapper()
{
	// 析构后移除网卡。
	netif_remove(_wrapped_obj.get());
}

std::string lwip::NetifWrapper::Name() const
{
	return _wrapped_obj->hostname;
}

#pragma region Open

void lwip::NetifWrapper::Open(bsp::IEthernetPort *ethernet_port,
							  base::Mac const &mac,
							  base::IPAddress const &ip_address,
							  base::IPAddress const &netmask,
							  base::IPAddress const &gateway,
							  int32_t mtu)
{
	_cache->_mac = mac;
	_cache->_ip_address = ip_address;
	_cache->_netmask = netmask;
	_cache->_gateway = gateway;

	// 将参数赋值给字段赋值后，调用参数较少的 Open 重载版本。
	Open(ethernet_port, mtu);
}

void lwip::NetifWrapper::Open(bsp::IEthernetPort *ethernet_port, int32_t mtu)
{
	_cache->_mtu = mtu;

	// 将参数赋值给字段赋值后，调用参数较少的 Open 重载版本。
	Open(ethernet_port);
}

void lwip::NetifWrapper::Open(bsp::IEthernetPort *ethernet_port)
{
	_ethernet_port = ethernet_port;
	if (_ethernet_port == nullptr)
	{
		throw std::runtime_error{"必须先调用 Open 方法传入一个 bsp::IEthernetPort 对象"};
	}

	_ethernet_port->Open(_cache->_mac);
	tcpip_init(nullptr, nullptr);

	ip_addr_t ip_address = base::Convert<ip_addr_t, base::IPAddress>(_cache->_ip_address);
	ip_addr_t netmask = base::Convert<ip_addr_t, base::IPAddress>(_cache->_netmask);
	ip_addr_t gataway = base::Convert<ip_addr_t, base::IPAddress>(_cache->_gateway);

	auto initialization_callback = [](netif *p) -> err_t
	{
		try
		{
			reinterpret_cast<NetifWrapper *>(p->state)->InitializationCallbackFunc();
			return err_enum_t::ERR_OK;
		}
		catch (std::exception const &e)
		{
			DI_Console().WriteLine(e.what());
			return err_enum_t::ERR_IF;
		}
	};

	/* netif_add 函数的 state 参数是 lwip 用来让用户传递私有数据的，会被放到 netif 的 state 字段中，
	 * 这里传递了 this，这样就将 netif 和本类对象绑定了，只要拿到了 netif 指针，就能拿到本类对象的指针。
	 */
	netif *netif_add_result = netif_add(_wrapped_obj.get(),
										&ip_address,
										&netmask,
										&gataway,
										this,
										initialization_callback,
										tcpip_input);

	if (netif_add_result == nullptr)
	{
		DI_Console().WriteLine("添加网卡失败。");
		throw std::runtime_error{"添加网卡失败。"};
	}

	if (netif_default == nullptr)
	{
		// 如果当前没有默认的网卡，则将本网卡设为默认。
		SetAsDefaultNetInterface();
	}

	// 链接状态更新
	DI_TaskManager().Create(
		[this]()
		{
			LinkStateDetectingThreadFunc();
		},
		512);

	DI_TaskManager().Create(
		[this]()
		{
			InputThreadFunc();
		},
		512);
}

#pragma endregion

#pragma region 地址

base::Mac lwip::NetifWrapper::Mac() const
{
	return base::Mac{
		std::endian::big,
		base::ReadOnlySpan{
			_wrapped_obj->hwaddr,
			sizeof(_wrapped_obj->hwaddr),
		},
	};
}

void lwip::NetifWrapper::SetMac(base::Mac const &o)
{
	base::Span netif_mac_buff_span{_wrapped_obj->hwaddr, 6};
	netif_mac_buff_span.CopyFrom(o.AsReadOnlySpan());
	netif_mac_buff_span.Reverse();
}

base::IPAddress lwip::NetifWrapper::IPAddress() const
{
	return base::Convert<base::IPAddress, ip_addr_t>(_wrapped_obj->ip_addr);
}

void lwip::NetifWrapper::SetIPAddress(base::IPAddress const &value)
{
	_wrapped_obj->ip_addr = base::Convert<ip_addr_t, base::IPAddress>(value);

	netif_set_addr(_wrapped_obj.get(),
				   &_wrapped_obj->ip_addr,
				   &_wrapped_obj->netmask,
				   &_wrapped_obj->gw);
}

base::IPAddress lwip::NetifWrapper::Netmask() const
{
	return base::Convert<base::IPAddress, ip_addr_t>(_wrapped_obj->netmask);
}

void lwip::NetifWrapper::SetNetmask(base::IPAddress const &value)
{
	_wrapped_obj->netmask = base::Convert<ip_addr_t, base::IPAddress>(value);

	netif_set_addr(_wrapped_obj.get(),
				   &_wrapped_obj->ip_addr,
				   &_wrapped_obj->netmask,
				   &_wrapped_obj->gw);
}

base::IPAddress lwip::NetifWrapper::Gateway() const
{
	return base::Convert<base::IPAddress, ip_addr_t>(_wrapped_obj->gw);
}

void lwip::NetifWrapper::SetGateway(base::IPAddress const &value)
{
	_wrapped_obj->gw = base::Convert<ip_addr_t, base::IPAddress>(value);

	netif_set_addr(_wrapped_obj.get(),
				   &_wrapped_obj->ip_addr,
				   &_wrapped_obj->netmask,
				   &_wrapped_obj->gw);
}

void lwip::NetifWrapper::SetAllAddress(base::IPAddress const &ip_address,
									   base::IPAddress const &netmask,
									   base::IPAddress const &gateway)
{
	_wrapped_obj->ip_addr = base::Convert<ip_addr_t, base::IPAddress>(ip_address);
	_wrapped_obj->netmask = base::Convert<ip_addr_t, base::IPAddress>(netmask);
	_wrapped_obj->gw = base::Convert<ip_addr_t, base::IPAddress>(gateway);

	netif_set_addr(_wrapped_obj.get(),
				   &_wrapped_obj->ip_addr,
				   &_wrapped_obj->netmask,
				   &_wrapped_obj->gw);
}

void lwip::NetifWrapper::ClearAllAddress()
{
	_wrapped_obj->ip_addr = ip_addr_t{};
	_wrapped_obj->netmask = ip_addr_t{};
	_wrapped_obj->gw = ip_addr_t{};

	netif_set_addr(_wrapped_obj.get(),
				   &_wrapped_obj->ip_addr,
				   &_wrapped_obj->netmask,
				   &_wrapped_obj->gw);
}

#pragma endregion

void lwip::NetifWrapper::SetAsDefaultNetInterface()
{
	netif_set_default(_wrapped_obj.get());
}

bool lwip::NetifWrapper::IsDefaultNetInterface() const
{
	return netif_default == _wrapped_obj.get();
}
