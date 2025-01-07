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
#include <TcpIpInitialize.h>

class lwip::NetifWrapper::LinkController
{
private:
	netif *_netif{};
	std::atomic_bool _dhcp_started = false;

public:
	LinkController(netif *net_interface)
		: _netif(net_interface)
	{
	}

	/// @brief 通知 lwip 当前链路已接通。
	void SetUpLink()
	{
		_dhcp_started = false;
		netif_set_up(_netif);
		netif_set_link_up(_netif);
	}

	/// @brief 通知 lwip 当前链路已断开。
	void SetDownLink()
	{
		/**
		 * netif_set_down 和 netif_set_link_down 会导致 DHCP 停止。所以，为了让
		 * _dhcp_started 与 lwip 的实际状态同步，禁止私自使用这两个函数，只能使用
		 * 本类进行操作。
		 */
		_dhcp_started = false;
		netif_set_down(_netif);
		netif_set_link_down(_netif);
	}

	/// @brief 检查当前 lwip 被告知的链路状态。即是断开还是接通了。
	/// @note 这个值并不指示真实的链路状态，而是 lwip 上次被 SetUpLink 方法
	/// 和 SetDownLink 方法所告知的状态。
	/// @return
	bool LinkIsUp()
	{
		return netif_is_up(_netif);
	}

	/// @brief 启动 DHCP.
	/// @note 本函数幂等。
	void StartDHCP()
	{
		if (_dhcp_started)
		{
			return;
		}

		_dhcp_started = true;
		dhcp_start(_netif);
	}

	/// @brief 停止 DHCP.
	/// @note 本函数幂等。
	void StopDHCP()
	{
		if (!_dhcp_started)
		{
			return;
		}

		_dhcp_started = false;
		dhcp_stop(_netif);
	}

	/// @brief 检查 DHCP 是否已经启动。
	bool DhcpHasStarted() const
	{
		return _dhcp_started;
	}
};

class lwip::NetifWrapper::Cache
{
public:
	/// @brief 本机IP地址
	base::IPAddress _ip_address{};

	base::IPAddress _netmask{"255.255.255.0"};

	base::IPAddress _gateway{};

	/// @brief 本网卡所使用的 MAC 地址。
	base::Mac _mac;

	int32_t _mtu = 1500;
};

void lwip::NetifWrapper::InitializationCallbackFunc()
{
	_wrapped_obj->hostname = _name.c_str();
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

void lwip::NetifWrapper::LinkStateDetectingThreadFunc()
{
	bool dhcp_supplied_address_in_last_loop = false;
	while (true)
	{
		if (_disposed)
		{
			DI_Console().WriteLine("LinkStateDetectingThreadFunc 退出");
			_link_state_detecting_thread_func_exited->Release();
			return;
		}

		if (_dhcp_enabled && _link_controller->LinkIsUp())
		{
			TryDHCP();

			// 检测 DHCP 是否成功，成功了需要打印出通过 DHCP 获取到的地址。
			bool dhcp_supplied_address = HasGotAddressesByDHCP();
			if (!dhcp_supplied_address_in_last_loop && dhcp_supplied_address)
			{
				_cache->_ip_address = IPAddress();
				_cache->_netmask = Netmask();
				_cache->_gateway = Gateway();
				DI_Console().WriteLine("DHCP 成功：");
				DI_Console().WriteLine("通过 DHCP 获取到 IP 地址：" + _cache->_ip_address.ToString());
				DI_Console().WriteLine("通过 DHCP 获取到子网掩码：" + _cache->_netmask.ToString());
				DI_Console().WriteLine("通过 DHCP 获取到的默认网关：" + _cache->_gateway.ToString());
			}

			dhcp_supplied_address_in_last_loop = dhcp_supplied_address;
		}

		// 延时。检测链路状态不需要那么快。
		DI_Delayer().Delay(std::chrono::milliseconds{1000});
	}
}

void lwip::NetifWrapper::OnInput(base::ReadOnlySpan span)
{
	pbuf_custom *custom_pbuf = new pbuf_custom{};
	custom_pbuf->custom_free_function = [](pbuf *p)
	{
		delete reinterpret_cast<pbuf_custom *>(p);
	};

	pbuf *buf = pbuf_alloced_custom(PBUF_RAW,
									span.Size(),
									PBUF_REF,
									custom_pbuf,
									const_cast<uint8_t *>(span.Buffer()),
									span.Size());

	buf->next = nullptr;

	err_t input_result = _wrapped_obj->input(buf, _wrapped_obj.get());
	if (input_result != err_enum_t::ERR_OK)
	{
		// 输入发生错误，释放 pbuf 链表。
		pbuf_free(buf);
	}
}

void lwip::NetifWrapper::TryDHCP()
{
	if (_link_controller->DhcpHasStarted())
	{
		return;
	}

	DI_Console().WriteLine("开始进行 DHCP.");
	_link_controller->StartDHCP();

	bool dhcp_result = false;
	for (int i = 0; i < 50; i++)
	{
		// 如果失败，最多重试 50 次。
		dhcp_result = HasGotAddressesByDHCP();
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
		return;
	}

	// DHCP 成功
	_cache->_ip_address = IPAddress();
	_cache->_netmask = Netmask();
	_cache->_gateway = Gateway();
	DI_Console().WriteLine("DHCP 成功：");
	DI_Console().WriteLine("通过 DHCP 获取到 IP 地址：" + _cache->_ip_address.ToString());
	DI_Console().WriteLine("通过 DHCP 获取到子网掩码：" + _cache->_netmask.ToString());
	DI_Console().WriteLine("通过 DHCP 获取到的默认网关：" + _cache->_gateway.ToString());
}

netif *lwip::NetifWrapper::WrappedObj() const
{
	return _wrapped_obj.get();
}

#pragma region 生命周期

lwip::NetifWrapper::NetifWrapper(std::string const &name)
{
	_wrapped_obj->state = this;
	_name = name;
	_cache = std::shared_ptr<lwip::NetifWrapper::Cache>{new Cache{}};
	_link_controller = std::shared_ptr<LinkController>{new LinkController{_wrapped_obj.get()}};
}

lwip::NetifWrapper::~NetifWrapper()
{
	Dispose();
}

void lwip::NetifWrapper::Dispose()
{
	if (_disposed)
	{
		return;
	}

	_disposed = true;

	if (_unsubscribe_token != nullptr)
	{
		_unsubscribe_token->Unsubscribe();
	}

	if (_connection_event_unsubscribe_token != nullptr)
	{
		_connection_event_unsubscribe_token->Unsubscribe();
	}

	if (_disconnection_event_unsubscribe_token != nullptr)
	{
		_disconnection_event_unsubscribe_token->Unsubscribe();
	}

	_link_state_detecting_thread_func_exited->Acquire();
	netif_remove(_wrapped_obj.get());
}

#pragma endregion

std::string lwip::NetifWrapper::Name() const
{
	return _name;
}

void lwip::NetifWrapper::Open(bsp::IEthernetPort *ethernet_port,
							  base::Mac const &mac,
							  base::IPAddress const &ip_address,
							  base::IPAddress const &netmask,
							  base::IPAddress const &gateway,
							  int32_t mtu)
{
	_ethernet_port = ethernet_port;
	_cache->_mac = mac;
	_cache->_ip_address = ip_address;
	_cache->_netmask = netmask;
	_cache->_gateway = gateway;
	_cache->_mtu = mtu;

	if (_ethernet_port == nullptr)
	{
		throw std::invalid_argument{CODE_POS_STR + "ethernet_port 不能是空指针。"};
	}

	_ethernet_port->Open(_cache->_mac);
	TcpIpInitialize();

	ip_addr_t ip_addr_t_ip_address = base::Convert<ip_addr_t, base::IPAddress>(_cache->_ip_address);
	ip_addr_t ip_addr_t_netmask = base::Convert<ip_addr_t, base::IPAddress>(_cache->_netmask);
	ip_addr_t ip_addr_t_gataway = base::Convert<ip_addr_t, base::IPAddress>(_cache->_gateway);

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
										&ip_addr_t_ip_address,
										&ip_addr_t_netmask,
										&ip_addr_t_gataway,
										this,
										initialization_callback,
										tcpip_input);

	if (netif_add_result == nullptr)
	{
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
		256);

	{
		if (_unsubscribe_token != nullptr)
		{
			_unsubscribe_token->Unsubscribe();
		}

		_unsubscribe_token = _ethernet_port->ReceivintEhternetFrameEvent().Subscribe(
			[this](base::ReadOnlySpan span)
			{
				OnInput(span);
			});
	}

	{
		if (_connection_event_unsubscribe_token != nullptr)
		{
			_connection_event_unsubscribe_token->Unsubscribe();
		}

		_connection_event_unsubscribe_token = _ethernet_port->ConnectionEvent().Subscribe(
			[this]()
			{
				// 开启以太网及虚拟网卡
				DI_Console().WriteLine("检测到网线插入");
				_link_controller->SetUpLink();
			});
	}

	{
		if (_disconnection_event_unsubscribe_token != nullptr)
		{
			_disconnection_event_unsubscribe_token->Unsubscribe();
		}

		_disconnection_event_unsubscribe_token = _ethernet_port->DisconnectionEvent().Subscribe(
			[this]()
			{
				DI_Console().WriteLine("检测到网线断开。");
				_link_controller->SetDownLink();
			});
	}
}

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

#pragma region 公共 DHCP

bool lwip::NetifWrapper::HasGotAddressesByDHCP()
{
	if (!_dhcp_enabled)
	{
		throw std::runtime_error{std::string{CODE_POS_STR} + "要先使能 DHCP 才能调用本方法"};
	}

	return dhcp_supplied_address(_wrapped_obj.get());
}

void lwip::NetifWrapper::EnableDHCP()
{
	_dhcp_enabled = true;
	_link_controller->StartDHCP();
}

void lwip::NetifWrapper::DisableDHCP()
{
	_dhcp_enabled = false;
	_link_controller->StopDHCP();
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
