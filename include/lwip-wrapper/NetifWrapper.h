#pragma once
#include "base/IDisposable.h"
#include "base/net/IPAddress.h"
#include "base/net/Mac.h"
#include "base/peripheral/ethernet/IEthernetPort.h"
#include "base/task/IBinarySemaphore.h"
#include "base/Wrapper.h"
#include "lwip/netif.h"
#include <atomic>
#include <memory>

namespace lwip
{
	/// @brief lwip 的 netif 的包装器。
	/// @warning 本类依赖 netif 的 state 字段。使用本类后，禁止修改此字段。
	class NetifWrapper :
		public base::Wrapper<netif>,
		public base::IDisposable
	{
	private:
		std::atomic_bool _disposed = false;
		std::atomic_bool _dhcp_enabled = false;
		std::unique_ptr<netif> _wrapped_obj{new netif{}};
		base::ethernet::IEthernetPort *_ethernet_port = nullptr;
		std::string _name;
		std::shared_ptr<base::IBinarySemaphore> _link_state_detecting_thread_func_exited = base::CreateIBinarySemaphore(false);
		std::shared_ptr<base::IIdToken> _receiving_event_unsubscribe_token;
		std::shared_ptr<base::IIdToken> _connection_event_unsubscribe_token;
		std::shared_ptr<base::IIdToken> _disconnection_event_unsubscribe_token;
		std::vector<base::ReadOnlySpan> _sending_spans{};

		class LinkController;
		std::shared_ptr<LinkController> _link_controller = nullptr;

		/// @brief 缓存数据。
		class Cache;
		std::shared_ptr<Cache> _cache = nullptr;

		void InitializationCallbackFunc();

		/// @brief 使用本网卡发送 pbuf 链表。
		/// @param p
		void SendPbuf(pbuf *p);

		/// @brief 检测链接状态的线程函数。
		void LinkStateDetectingThreadFunc();

		void OnInput(base::ReadOnlySpan span);
		void TryDHCP();
		void SubscribeEvents();

	public:
#pragma region 生命周期
		/// @brief 构造函数。
		NetifWrapper(std::string const &name);
		~NetifWrapper();

		void Dispose() override;
#pragma endregion

		/// @brief 获取被包装对象的指针。
		/// @return
		netif *WrappedObj() const override;

		std::string Name() const;

		/// @brief 打开网卡，打开后会向 lwip 注册本网卡，然后就可以被 lwip 使用了。
		/// @param ethernet_port
		/// @param mac
		/// @param ip_address
		/// @param netmask
		/// @param gateway
		/// @param mtu
		void Open(base::ethernet::IEthernetPort *ethernet_port,
				  base::Mac const &mac,
				  base::IPAddress const &ip_address,
				  base::IPAddress const &netmask,
				  base::IPAddress const &gateway,
				  int32_t mtu);

#pragma region 地址
		base::Mac Mac() const;
		void SetMac(base::Mac const &o);

		base::IPAddress IPAddress() const;
		void SetIPAddress(base::IPAddress const &value);

		base::IPAddress Netmask() const;
		void SetNetmask(base::IPAddress const &value);

		base::IPAddress Gateway() const;
		void SetGateway(base::IPAddress const &value);

		/// @brief 直接将全部地址改了。
		/// @note 如果实际需求是修改多个地址，最好使用本方法，因为修改地址后要做一次配置，
		/// 让 lwip 使用新的地址。修改一个地址是配置一次，修改多个地址也是配置一次。所以
		/// 如果是修改多个地址，使用本方法会比对每个地址单独调用 Set 方法的性能好。
		/// @param ip_address
		/// @param netmask
		/// @param gateway
		void SetAllAddress(base::IPAddress const &ip_address,
						   base::IPAddress const &netmask,
						   base::IPAddress const &gateway);

		/// @brief 将所有地址清 0.
		/// @note 包括：IP 地址、子网掩码、网关。
		void ClearAllAddress();
#pragma endregion

#pragma region 公共 DHCP
		/// @brief 检查本次启动 DHCP 后 IP 地址是否被 DHCP 提供了。
		/// @return 如果 DHCP 提供了 IP 地址，则返回 true, 否则返回 false.
		bool HasGotAddressesByDHCP();

		void EnableDHCP();
		void DisableDHCP();
#pragma endregion

		/// @brief 设置为默认网卡。
		void SetAsDefaultNetInterface();

		/// @brief 检查本网卡是否是默认网卡。
		/// @return
		bool IsDefaultNetInterface() const;
	};
} // namespace lwip
