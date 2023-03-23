#ifndef __CATPC_ALLOCATOR_HPP__
#define __CATPC_ALLOCATOR_HPP__

#include <vector>
#include <unordered_map>

#include "catpc_monitor.hpp"

struct CLOS {
	unsigned int id;
	uint64_t mask;
};

struct llc_ca {
	int id;
	unsigned way_size;
	unsigned num_ways;
	unsigned clos_count;
	std::vector<CLOS> clos_list;
};

/**
 * @brief Get the cache allocation config of the system
 * 
 * @return std::vector<llc> vector of llcs
 */
std::vector<llc_ca> get_allocation_config();

/**
 * @brief Perform the cache allocation on the applications
 * 
 * @param [in] application_ptr application pointer 
 * 
 * @retval 0 OK
 * @retval -1 error
 */
int perform_allocation(catpc_application* application_ptr);

/**
 * @brief Process and return the required amount of llc
 * 
 * @param [in] mrc miss rate curve of the application
 * @param [in] llcs vector of llc info
 * @return the required llc 
 */
uint64_t get_required_llc(const std::map<uint64_t, double>& mrc, const std::vector<llc_ca>& llcs);

/**
 * @brief 
 * 
 * @param [in] application application to which smart cache allocation is to be performed.
 * @param [in] llcs reference to the list of last level caches.
 * 
 * @retval 0 OK
 * @retval -1 error
 */
int perform_smart_allocation(catpc_application* application_ptr, std::vector<llc_ca>& llcs);

/**
 * @brief Remove an application from the list of monitored applications.
 * 
 * @param [in,out] applications application map list
 * @param [in] llcs constant reference to the list of last level caches
 * @param [in] cmdline command line of the application to remove
 * 
 * @retval 0 OK
 * @retval -1 error
 */
int remove_application(std::unordered_map<std::string, catpc_application*>& applications, const std::vector<llc_ca>& llcs, const std::string& cmdline);
#endif