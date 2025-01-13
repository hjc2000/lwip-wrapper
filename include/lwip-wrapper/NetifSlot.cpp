#include "NetifSlot.h"
#include <base/string/define.h>
#include <bsp-interface/di/interrupt.h>
#include <bsp-interface/TaskSingletonGetter.h>

lwip::NetifSlot &lwip::NetifSlot::Instance()
{
	class Getter :
		public bsp::TaskSingletonGetter<NetifSlot>
	{
	public:
		std::unique_ptr<NetifSlot> Create() override
		{
			return std::unique_ptr<NetifSlot>{new NetifSlot{}};
		}
	};

	Getter g;
	return g.Instance();
}

void lwip::NetifSlot::PlugIn(std::shared_ptr<lwip::NetifWrapper> const &o)
{
	if (o == nullptr)
	{
		throw std::invalid_argument{std::string{CODE_POS_STR} + "禁止传入空指针。"};
	}

	try
	{
		_netif_dic.Add(o->Name(), o);
	}
	catch (std::exception const &e)
	{
		throw std::runtime_error{std::string{CODE_POS_STR} + e.what()};
	}
}

std::shared_ptr<lwip::NetifWrapper> lwip::NetifSlot::Find(std::string const &key)
{
	std::shared_ptr<lwip::NetifWrapper> *pp = _netif_dic.Find(key);
	if (pp == nullptr)
	{
		return nullptr;
	}

	return *pp;
}

std::shared_ptr<lwip::NetifWrapper> lwip::NetifSlot::FindDefaultNetif() const
{
	for (std::pair<std::string const, std::shared_ptr<lwip::NetifWrapper>> const &pair : _netif_dic)
	{
		if (pair.second->IsDefaultNetInterface())
		{
			return pair.second;
		}
	}

	return nullptr;
}
