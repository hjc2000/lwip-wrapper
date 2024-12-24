#pragma once
#include <base/container/Dictionary.h>
#include <base/define.h>
#include <lwip-wrapper/NetifWrapper.h>
#include <string>

namespace lwip
{
	/// @brief 网卡插槽。
	/// @note NetifWrapper 打开后就会向 lwip 注册，网卡就可以使用了。但是必须始终保持 NetifWrapper 对象
	/// 不析构，否则网卡会销毁。如果没法自己保存 NetifWrapper 对象，就把它放到本插槽中。
	class NetifSlot
	{
	private:
		NetifSlot() = default;
		NetifSlot(NetifSlot const &o) = delete;
		NetifSlot &operator=(NetifSlot const &o) = delete;

		base::Dictionary<std::string, std::shared_ptr<lwip::NetifWrapper>> _netif_dic;

	public:
		static_function NetifSlot &Instance();

		/// @brief 插入一张网卡。
		/// @param o
		void PlugIn(std::shared_ptr<lwip::NetifWrapper> const &o);

		/// @brief 移除一张网卡。
		/// @param name 网卡名称。
		/// @return 移除成功返回 true，元素不存在返回 false。
		bool Remove(std::string const &name)
		{
			return _netif_dic.Remove(name);
		}

		/// @brief 查找一张网卡。找不到会返回空指针。
		/// @param name 网卡名称。
		/// @return
		std::shared_ptr<lwip::NetifWrapper> Find(std::string const &name);

		/// @brief 获取网卡个数。
		/// @return
		int Count() const
		{
			return _netif_dic.Count();
		}

		/// @brief 获取迭代器
		/// @return
		std::shared_ptr<base::IEnumerator<std::pair<std::string const, std::shared_ptr<lwip::NetifWrapper>>>> GetEnumerator()
		{
			return _netif_dic.GetEnumerator();
		}
	};
} // namespace lwip
