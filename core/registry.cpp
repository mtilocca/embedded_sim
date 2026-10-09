#include "core/registry.h"

namespace sim {

#ifdef CONFIG_SIM_SENSORS
Simulation &sensors();
#endif
#ifdef CONFIG_SIM_SCADA
Simulation &scada();
#endif
#ifdef CONFIG_SIM_ROBOTICS
Simulation &robotics();
#endif
#ifdef CONFIG_SIM_PUMPS
Simulation &pumps();
#endif
#ifdef CONFIG_SIM_WEBSERVER
Simulation &webserver();
#endif

Simulation *const *enabled(size_t &count)
{
	static Simulation *const list[] = {
#ifdef CONFIG_SIM_SENSORS
		&sensors(),
#endif
#ifdef CONFIG_SIM_SCADA
		&scada(),
#endif
#ifdef CONFIG_SIM_ROBOTICS
		&robotics(),
#endif
#ifdef CONFIG_SIM_PUMPS
		&pumps(),
#endif
#ifdef CONFIG_SIM_WEBSERVER
		&webserver(),
#endif
		nullptr, // keeps the array non-empty
	};
	count = sizeof(list) / sizeof(list[0]) - 1;
	return list;
}

} // namespace sim
