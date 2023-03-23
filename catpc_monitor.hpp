#ifndef __CATPC_MONITOR_HPP__
#define __CATPC_MONITOR_HPP__

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <map>

#include "pqos.h"

/**
 * The structure to store monitoring data for all of the events
 */
struct catpc_monitoring_values {
	uint64_t llc;                /**< cache occupancy */
	double ipc;                  /**< instructions per cycle */
	uint64_t llc_misses;         /**< LLC misses - delta */
	uint64_t llc_references;     /**< LLC references */
};

/**
 * @brief Miss Rate Curve entry : occupancy (in kB) and miss_rate (in [0, 1])
 * 
 */
using MRC = std::map<unsigned, double>;

/**
 * @brief HPC Application data structure characterized by its command line 
 * that will be used to find all PIDs belonging to the app 
 */

struct catpc_application {
	std::string cmdline;
	catpc_monitoring_values values;
	unsigned int CLOS_id;
	bool eval_done;
	bool smart_alloc_done;
	uint64_t required_llc;

	catpc_application(std::string cl = "") : cmdline{cl}, values{}, CLOS_id{0}, eval_done{false}, smart_alloc_done{false}, required_llc{0} {}

	catpc_application(const std::string& cl, const catpc_monitoring_values& v, const unsigned int& cid)
		: cmdline{cl}, values{v}, CLOS_id{cid}, eval_done{false}, smart_alloc_done{false}, required_llc{0} {}
};

/**
 * @brief System information about PQOS 
 * 
 */
struct catpc_system_info {
	struct pqos_config config;
	const struct pqos_cpuinfo *p_cpu = NULL;
	const struct pqos_cap *p_cap = NULL;
	const struct pqos_capability *cap_mon = NULL;
	const struct pqos_capability *l3_cap = NULL;
};

/**
 * @brief Init monitoring environment
 * 
 * @retval 0 OK
 * @retval -1 error
 */
int init_monitoring();

/**
 * @brief Starts monitoring on app launched by a command line
 *
 * @param [in] cmdline application commandline
 * 
 * @return Operation status
 * @retval 0 OK
 * @retval -1 error
 */

int start_monitoring(const std::string&);

/**
 * @brief Reads monitoring data
 * 
 * @param [in] applications application map list
 * 
 * @retval 0 OK
 * @retval -1 error
 */
int poll_monitoring_data(std::unordered_map<std::string, catpc_application*>&);

/**
 * @brief Stop monitoring on all PIDs
 * 
 * @param [in] applications application map list
 * 
 */
void stop_monitoring(std::unordered_map<std::string, catpc_application*>&);

/**
 * @brief Get the system info about PQOS
 * 
 * @return system_info 
 */
catpc_system_info get_system_info();

void set_logfile(FILE* file);
#endif