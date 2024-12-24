#include "NetifSlot.h"
#include <base/di/SingletonGetter.h>
#include <bsp-interface/di/interrupt.h>

lwip::NetifSlot &lwip::NetifSlot::Instance()
{
	class Getter :
		public base::SingletonGetter<NetifSlot>
	{
	public:
		std::unique_ptr<NetifSlot> Create() override
		{
			return std::unique_ptr<NetifSlot>{new NetifSlot{}};
		}

		void Lock() override
		{
			DI_DisableGlobalInterrupt();
		}

		void Unlock() override
		{
			DI_EnableGlobalInterrupt();
		}
	};

	Getter g;
	return g.Instance();
}
