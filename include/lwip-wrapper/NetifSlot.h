#pragma once
#include <base/container/Dictionary.h>
#include <base/define.h>
#include <lwip-wrapper/NetifWrapper.h>
#include <string>

namespace lwip
{
	/// @brief 网卡插槽。
	/// @note 其实就是个字典，用来储存 lwip::NetifWrapper 对象。
	class NetifSlot :
		public base::IDictionary<std::string, std::shared_ptr<lwip::NetifWrapper>>
	{
	private:
		NetifSlot() = default;
		NetifSlot(NetifSlot const &o) = delete;
		NetifSlot &operator=(NetifSlot const &o) = delete;

		base::Dictionary<std::string, std::shared_ptr<lwip::NetifWrapper>> _netif_dic;

	public:
		static_function NetifSlot &Instance();

		/// @brief 获取元素个数。
		/// @return
		int Count() const override
		{
			return _netif_dic.Count();
		}

		/// @brief 查找元素。
		/// @param key 键
		/// @return 指针。找到了返回元素的指针，找不到返回空指针。
		std::shared_ptr<lwip::NetifWrapper> *Find(std::string const &key) override
		{
			return _netif_dic.Find(key);
		}

		/// @brief 移除一个元素。
		/// @param key 键
		/// @return 移除成功返回 true，元素不存在返回 false。
		bool Remove(std::string const &key) override
		{
			return _netif_dic.Remove(key);
		}

		/// @brief 设置一个元素。本来不存在，会添加；本来就存在了，会覆盖。
		/// @param key
		/// @param item
		void Set(std::string const &key, std::shared_ptr<lwip::NetifWrapper> const &item) override
		{
			_netif_dic.Set(key, item);
		}

		/// @brief 获取迭代器
		/// @return
		std::shared_ptr<base::IEnumerator<std::pair<std::string const, std::shared_ptr<lwip::NetifWrapper>>>> GetEnumerator() override
		{
			return _netif_dic.GetEnumerator();
		}
	};
} // namespace lwip
