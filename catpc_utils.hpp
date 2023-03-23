#ifndef __CATPC_UTILS_HPP__
#define __CATPC_UTILS_HPP__

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h> 
#include <signal.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <queue>
#include <mutex>
#include <cassert>
#include <ostream>

#include "catpc_monitor.hpp"

/**
 * @brief CATPC message values
 * 
 */
enum catpc_message {
	CATPC_GET_MONITORING_VALUES = 0, /**< retrieve monitoring values */
	CATPC_GET_CAPABILITIES = 1,   /**< retrieve system capabilities */
	CATPC_PERFORM_ALLOCATION = 2,    /**< perform allocation */
	CATPC_ADD_APP_TO_MONITOR = 3,		/**< add application to monitor */
	CATPC_REMOVE_APP_TO_MONITOR = 4		/**< remove application to monitor */
};

struct connection_t {
	int sock;
	struct sockaddr_in address;
	socklen_t addr_len;
};

struct notification_t {
	std::mutex mtx;
	enum event {
		APP_ADDED = 0,
		APP_REMOVED = 1,
		PERFORM_ALLOCATION = 2
	};
	std::queue<std::pair<event, std::string>> event_queue;

	notification_t(): event_queue() {
		assert(event_queue.empty());
	}

	notification_t(const notification_t&& other)
	{
		this->event_queue = other.event_queue;
	}
};

/**
 * @brief Retrieve process ids created from a command line
 * 
 * @param [in] cmdline full command line with spaces removed
 * @param [out] pids array of pids
 * @return number of pids
 */
int get_pids_by_cmdline(pid_t* pids, const char* cmdline);

/**
 * @brief logging function
 * 
 * @param [in] fp file pointer to the log file
 * @param [in] fmt format string compatible with fprintf().
 * @param [in] args variadic arguments to follow depending on \a fmt. 
 */
void log_fprint(FILE* fp, const char* fmt, ...);

/**
 * @brief operator definition to print map on output stream
 * 
 * @param [in] os output stream
 * @param [in] m the map
 * @return std::ofstream& reference to the output stream 
 */
std::ostream& operator<<(std::ostream& os, const std::map<uint64_t, double>& m);

#endif