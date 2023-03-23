#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <vector>

#include "catpc_utils.hpp"
#include "catpc_monitor.hpp"
#include "catpc_allocator.hpp"

#define SERVER_PORT 10000

FILE* log_file = NULL;
std::unordered_map<std::string, catpc_application*> applications;

/*
* =======================================
* Main
* =======================================
*/
int main(int argc, char** argv)
{
	pid_t pid = fork();
	char* master_name = NULL;

	if (argc < 2) {
		printf("Usage : %s [master-name]\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	master_name = argv[1];

	if (pid < 0) {
		fprintf(stderr, "fork failed!\n");
		exit(EXIT_FAILURE);
	}

	if (pid > 0) {
		fprintf(stderr, "daemon created : %d \n", pid);
		exit(EXIT_SUCCESS);
	}

	/*
	* =======================================
	* Daemon Code
	* =======================================
	*/
	umask(0);

	pid_t sid = 0;
	sid = setsid();	// set new session
	if(sid < 0) {
		exit(EXIT_FAILURE);
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	struct hostent* host_info = NULL;
	struct sockaddr_in server_addr;
	int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	int bytes_read = 0, bytes_sent = 0;
	enum catpc_message msg;
	std::string cmdline;
	unsigned int CLOS_id;
	uint64_t required_llc;
	size_t sz;
	int ret = 0;
	char buf[512];

	log_file = fopen("/var/log/catpc.slave.log", "w");
	if (log_file == NULL) {
		exit(EXIT_FAILURE);
	}

	// Start Monitoring
	ret = init_monitoring();
	if (ret < 0) {
		log_fprint(log_file, "ERROR: unable to init monitoring\n");
		exit(EXIT_FAILURE);
	}

	// get allocation configuration
	std::vector<llc_ca> llcs = get_allocation_config();
	if (llcs.empty()) {
		log_fprint(log_file, "ERROR: get_allocation_config failed\n");
		exit(EXIT_FAILURE);
	}

	// get master info by name
	host_info = gethostbyname(master_name);
	if (host_info == NULL) {
		log_fprint(log_file, "ERROR: unknown host: %s\n", master_name);
		exit(EXIT_FAILURE);
	}

	server_addr.sin_family = AF_INET;
	memcpy((char*)&server_addr.sin_addr, host_info->h_addr_list[0], host_info->h_length);
	server_addr.sin_port = htons(SERVER_PORT);
	
	if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		log_fprint(log_file, "ERROR: connection to %s failed: %s (%d)\n", inet_ntoa(server_addr.sin_addr), strerror(errno), errno);
		exit(EXIT_FAILURE);
	}

	// read message from master
	while ((bytes_read = recv(sock, &msg, sizeof(msg), 0)) > 0) {
		switch (msg) {
		case CATPC_GET_MONITORING_VALUES:
			log_fprint(log_file, "INFO: message received: CATPC_GET_MONITORING_VALUES\n");
			
			// poll monitoring values
			ret = poll_monitoring_data(applications);
			if (ret < 0) {
				log_fprint(log_file, "ERROR: polling monitoring data (%d)\n", ret);
				goto exit;
			}

			// send values tab
			sz = applications.size();
			bytes_sent = send(sock, &sz, sizeof(size_t), 0);													// send the number of applications
			for (std::pair<std::string, catpc_application*> element : applications) {
				catpc_application* app_ptr = element.second;
				sz = app_ptr->cmdline.size();																				
				bytes_sent = send(sock, &sz, sizeof(size_t), 0);												// send the length of the cmdline string
				bytes_sent = send(sock, app_ptr->cmdline.c_str(), sz * sizeof(char), 0);					// send the cmdline string
				bytes_sent = send(sock, &app_ptr->values, sizeof(catpc_monitoring_values), 0);		// send monitoring values
				bytes_sent = send(sock, &app_ptr->CLOS_id, sizeof(unsigned int), 0);					// send CLOS id
			}

			break;
		case CATPC_ADD_APP_TO_MONITOR:
			log_fprint(log_file, "INFO: message received: CATPC_ADD_APP_TO_MONITOR\n");
			
			// receive cmd line string
			bytes_read = recv(sock, &sz, sizeof(size_t), 0);
			bytes_read = recv(sock, buf, sz * sizeof(char), 0);
			cmdline.assign(buf, sz);


			// add application to the map
			applications.try_emplace(cmdline, new catpc_application(cmdline, catpc_monitoring_values(), 0));

			// start monitoring on app launched by the command line
			set_logfile(log_file);
			ret = start_monitoring(cmdline);
			if (ret < 0) {
				log_fprint(log_file, "ERROR: unable to start monitoring on app \"%s(%d)\"\n", cmdline.c_str(), ret);
				exit(EXIT_FAILURE);
			}
			
			log_fprint(log_file, "INFO: app added : %s\n", cmdline.c_str());
			break;

		case CATPC_REMOVE_APP_TO_MONITOR:
			log_fprint(log_file, "INFO: message received: CATPC_REMOVE_APP_TO_MONITOR\n");

			// receive cmd line string
			bytes_read = recv(sock, &sz, sizeof(size_t), 0);
			bytes_read = recv(sock, buf, sz * sizeof(char), 0);
			cmdline.assign(buf, sz);

			// add application to the map
			remove_application(applications, llcs, cmdline);
			
			log_fprint(log_file, "INFO: app removed : %s\n", cmdline.c_str());
			break;

		case CATPC_GET_CAPABILITIES:
			log_fprint(log_file, "INFO: message received: CATPC_GET_CAPABILITIES\n");
			sz = llcs.size();
			bytes_sent = send(sock, &sz, sizeof(size_t), 0);
			for (llc_ca llc : llcs) {
				bytes_sent = send(sock, &llc.id, sizeof(int), 0);
				bytes_sent = send(sock, &llc.num_ways, sizeof(unsigned), 0);
				bytes_sent = send(sock, &llc.way_size, sizeof(unsigned), 0);
				bytes_sent = send(sock, &llc.clos_count, sizeof(unsigned), 0);
				for (CLOS clos : llc.clos_list) {
					bytes_sent = send(sock, &clos, sizeof(CLOS), 0);
				}
			}
			break;
		case CATPC_PERFORM_ALLOCATION:
			log_fprint(log_file, "INFO: message received: CATPC_PERFORM_ALLOCATION\n");
			// read as many times as there are applications
			for (unsigned i = 0; i < applications.size(); ++i) {
				bytes_read = recv(sock, &sz, sizeof(size_t), 0);
				bytes_read = recv(sock, buf, sz * sizeof(char), 0);
				bytes_read = recv(sock, &CLOS_id, sizeof(unsigned int), 0);
				bytes_read = recv(sock, &required_llc, sizeof(uint64_t), 0);
				log_fprint(log_file, "INFO: %s: COS%u -> COS%u\n", cmdline.c_str(), applications[cmdline]->CLOS_id, CLOS_id);
				
				cmdline.assign(buf, sz);
				applications[cmdline]->CLOS_id = CLOS_id;
				applications[cmdline]->required_llc = required_llc;
			}

			// perform allocation
			for (std::pair<std::string, catpc_application*> element : applications) {
				catpc_application* app_ptr = element.second;
				if (app_ptr->required_llc > 0) {
					if (!app_ptr->smart_alloc_done) {
						ret = perform_smart_allocation(app_ptr, llcs);
						if (ret < 0) {
							log_fprint(log_file, "ERROR: perform_smart_allocation failed (%d)\n", ret);
						}
						app_ptr->smart_alloc_done = true;
						log_fprint(log_file, "DEBUG: smart allocation done for %s : COS%d\n", app_ptr->cmdline.c_str(), app_ptr->CLOS_id);
					}
				}
				else {
					ret = perform_allocation(app_ptr);
					if (ret < 0) {
						log_fprint(log_file, "ERROR: perform_allocation failed (%d)\n", ret);
					}
				}
			}
			break;
		default:
			log_fprint(log_file, "ERROR: unknow message value: %d\n", msg);
		}

		// error checking
		if (bytes_sent < 0) {
			log_fprint(log_file, "ERROR: send: %s (%d)\n", strerror(errno), errno);
		}
		if (bytes_read < 0) {
			log_fprint(log_file, "ERROR: recv: %s (%d)\n", strerror(errno), errno);
		}
	}

	if (bytes_read == 0) {
		log_fprint(log_file, "INFO: recv: server closed.\n");
	}
	else {	// bytes_read < 0
		log_fprint(log_file, "ERROR: recv: %s (%d)\n", strerror(errno), errno);
	}

exit:
	// stop monitoring before exit
	stop_monitoring(applications);
	
	log_fprint(log_file, "INFO: Done.\n");
	
	// cleaning up everything
	close(sock);
	fclose(log_file);
	return EXIT_SUCCESS;
}