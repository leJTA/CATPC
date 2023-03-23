#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <errno.h>
#include <thread>
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <unordered_map>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <atomic>
#include <fstream>
#include <iostream>

#include "catpc_utils.hpp"
#include "catpc_monitor.hpp"
#include "catpc_allocator.hpp"

#define MAX_CLIENTS 16			// Maximum number of slaves managed by CATPC master
#define SERVER_PORT 10000

void connection_handler(connection_t*);
void processing_loop();
void termination_handler(int signum);
void watch_started_app();
void watch_terminated_app();

const std::chrono::milliseconds period{500};

int sock;
FILE* log_file = NULL;
std::atomic<unsigned int> client_count = 0;
std::vector<connection_t*> connections;
sig_atomic_t terminate = 0;
std::mutex global_mtx;
std::condition_variable global_cv;
std::chrono::time_point<std::chrono::steady_clock> start_time;

std::unordered_map<int, std::unordered_map<std::string, catpc_application*>> sock_to_application;
std::unordered_map<int, notification_t> sock_to_notification;
std::unordered_map<int, std::vector<llc_ca>> sock_to_llcs;
std::unordered_map<int, std::unordered_map<std::string, std::map<uint64_t, double>>> sock_to_mrc;

/*
* =======================================
* Main
* =======================================
*/
int main(int argc, char** argv)
{
	pid_t pid = fork();

	if (pid < 0) {
		fprintf(stderr, "fork failed!\n");
		exit(EXIT_FAILURE);
	}

	if (pid > 0) {
		printf("daemon created : %d \n", pid);
		exit(EXIT_SUCCESS);
	}

	/*
	* =======================================
	* Daemon Code
	* =======================================
	*/
	umask(0);
	
	pid_t sid = 0;
	sid = setsid();			// set new session
	if(sid < 0) {
		exit(EXIT_FAILURE);
	}
	
	// close all standard file descriptors
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	
	// handle termination signals
	signal(SIGINT, termination_handler);
	signal(SIGTERM, termination_handler);

	std::vector<std::thread> threads;
	sockaddr_in address;
	const int reuse = 1;		// reuse socket

	log_file = fopen("/var/log/catpc.master.log", "w");
	if (log_file == NULL) {
		exit(EXIT_FAILURE);
	}
	
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(SERVER_PORT);

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0)
		log_fprint(log_file, "ERROR: setsockopt (SO_REUSEADDR) failed: %s (%d)\n", strerror(errno), errno);
	
	// Bind
	if (bind(sock, (struct sockaddr*)&address, sizeof(address)) < 0) {
		log_fprint(log_file, "ERROR: bind failed: %s (%d)\n", strerror(errno), errno);
		exit(EXIT_FAILURE);
	}

	// Listen
	if (listen(sock, MAX_CLIENTS)) {
		log_fprint(log_file, "ERROR: listen failed: %s (%d)\n", strerror(errno), errno);
	}
	
	// start fifo monitoring thread
	std::thread started_app_watcher(watch_started_app);
	std::thread terminated_app_watcher(watch_terminated_app);

	// start procressing loop thread
	std::thread processing_thread(processing_loop);

	// Loop to accept incomming connections
	start_time = std::chrono::steady_clock::now();
	while (!terminate) {
		connection_t conn;
		conn.sock = accept(sock, (struct sockaddr*)&conn.address, &conn.addr_len);
		if (conn.sock < 0) {
			log_fprint(log_file, "ERROR: accept failed: %s (%d)\n", strerror(errno), errno);
			continue;
		}
		connections.push_back(new connection_t(conn));
		client_count++;

		// insert corresponding map elements
		sock_to_notification.emplace(connections.back()->sock, notification_t());
		
		log_fprint(log_file, "INFO: slave connected: %s\n", inet_ntoa(connections.back()->address.sin_addr));

		// Launch connection handler on a thread
		threads.push_back(std::thread(connection_handler, connections.back()));
		if (!threads.back().joinable()) {
			log_fprint(log_file, "ERROR: could not create thread for slave %s\n", inet_ntoa(connections.back()->address.sin_addr));
		}
	}
	
	// Close everything

	for (connection_t* conn : connections) {
		close(conn->sock);
		delete conn;
	}

	if (started_app_watcher.joinable()) {
		started_app_watcher.join();
	}
	if (terminated_app_watcher.joinable()) {
		terminated_app_watcher.join();
	}
	if (processing_thread.joinable()) {
		processing_thread.join();
	}

	for (std::thread& thd : threads) {
		if (thd.joinable()) {
			thd.join();
		}
	}

	log_fprint(log_file, "INFO: Done.\n");
	fclose(log_file);
	return EXIT_SUCCESS;
}

/*
* =======================================
* Handlers and Watchers
* =======================================
*/

void connection_handler(connection_t* conn)
{
	notification_t& notif = sock_to_notification[conn->sock];
	int bytes_read = 0, bytes_sent = 0;
	enum catpc_message msg = CATPC_GET_CAPABILITIES;
	std::string cmdline;
	catpc_application app;
	size_t sz, len;
	char buf[512];

	while (true) {
		{
			// Read the notification object to know if there is a new app or if an app has been removed/terminated
			std::scoped_lock<std::mutex> lk(notif.mtx);
			if (!notif.event_queue.empty()) {
				std::pair<notification_t::event, std::string>& p = notif.event_queue.front();
				cmdline = p.second;
				switch (p.first) {
				case notification_t::event::APP_ADDED:
					msg = CATPC_ADD_APP_TO_MONITOR;
					break;
				case notification_t::event::APP_REMOVED:
					msg = CATPC_REMOVE_APP_TO_MONITOR;
					break;
				case notification_t::event::PERFORM_ALLOCATION:
					msg = CATPC_PERFORM_ALLOCATION;
					break;
				}
				notif.event_queue.pop();
			}
		}

		// Message management
		switch (msg) {
		case CATPC_REMOVE_APP_TO_MONITOR:
			delete sock_to_application[conn->sock][cmdline];
			sock_to_application[conn->sock].erase(cmdline);
			sock_to_mrc[conn->sock].erase(cmdline);
			[[gnu::fallthrough]];

		case CATPC_ADD_APP_TO_MONITOR:
			if ((bytes_sent = send(conn->sock, &msg, sizeof(msg), 0)) > 0) {
				len = cmdline.size();
				send(conn->sock, &len, sizeof(size_t), 0);
				send(conn->sock, cmdline.c_str(), len * sizeof(char), 0);
			}
			break;
		
		case CATPC_GET_MONITORING_VALUES:
			if ((bytes_sent = send(conn->sock, &msg, sizeof(msg), 0)) > 0) {
				// Read the number of applications
				bytes_read = recv(conn->sock, &sz, sizeof(size_t), 0);

				for (size_t i = 0; i < sz && bytes_read > 0; ++i) {

					// Read the cmdline (size then chars)
					bytes_read = recv(conn->sock, &len, sizeof(size_t), 0);
					bytes_read = recv(conn->sock, buf, len * sizeof(char), 0);
					cmdline.assign(buf, len);
					
					// insert if not exist aka 'get or create'
					sock_to_application[conn->sock].try_emplace(cmdline, new catpc_application(cmdline));
					catpc_application* ptr = sock_to_application[conn->sock][cmdline];

					// Read monitoring data
					bytes_read = recv(conn->sock, &ptr->values, sizeof(catpc_monitoring_values), 0);

					// Read CLOS id
					bytes_read = recv(conn->sock, &ptr->CLOS_id, sizeof(unsigned int), 0);

					//log_fprint(log_file, "[DEBUG] : MRC[%s][%fKB] = %f\n", 
					//	ptr->cmdline.c_str(), ptr->values.llc / 1024.0, 
					//	(double)ptr->values.llc_misses / ptr->values.llc_references);
				}
				// Notify the loop processing thread
				{
					std::scoped_lock<std::mutex> lk(global_mtx);
					global_cv.notify_one();
				}
			}	
		break;

		case CATPC_GET_CAPABILITIES:
		if ((bytes_sent = send(conn->sock, &msg, sizeof(msg), 0)) > 0) {
			sock_to_llcs[conn->sock].clear();								// Clear the llc_ca list
			bytes_read = recv(conn->sock, &sz, sizeof(size_t), 0);	// read the number of llc/sockets
			for (size_t i = 0; i < sz; ++i) {
				sock_to_llcs[conn->sock].push_back(llc_ca());
				llc_ca& llc = sock_to_llcs[conn->sock].back();			// reference to the new element for better readability
				
				bytes_read = recv(conn->sock, &llc.id, sizeof(int), 0);
				bytes_read = recv(conn->sock, &llc.num_ways, sizeof(unsigned), 0);
				bytes_read = recv(conn->sock, &llc.way_size, sizeof(unsigned), 0);
				bytes_read = recv(conn->sock, &llc.clos_count, sizeof(unsigned), 0);

				for (unsigned j = 0; j < llc.clos_count; ++j) {
					llc.clos_list.push_back(CLOS());
					bytes_read = recv(conn->sock, &llc.clos_list.back(), sizeof(CLOS), 0);
				}
			}
		}
		break;

		case CATPC_PERFORM_ALLOCATION:
			if ((bytes_sent = send(conn->sock, &msg, sizeof(msg), 0)) > 0) {
				for (const std::pair<std::string, catpc_application*>& element : sock_to_application[conn->sock]) {
					catpc_application* app_ptr = element.second;
					len = app_ptr->cmdline.size();
					send(conn->sock, &len, sizeof(size_t), 0);
					send(conn->sock, app_ptr->cmdline.c_str(), len * sizeof(char), 0);
					send(conn->sock, &app_ptr->CLOS_id, sizeof(unsigned int), 0);
					send(conn->sock, &app_ptr->required_llc, sizeof(uint64_t), 0);
				}
			}
		break;

		default:
			log_fprint(log_file, "unknow message value: %d\n", msg);
		}
		
		// Error handling
		if (bytes_read < 0) {
			log_fprint(log_file, "ERROR: recv from %s: %s (%d)\n", inet_ntoa(conn->address.sin_addr), strerror(errno), errno);
			break;
		}

		if (bytes_sent == 0) {
			log_fprint(log_file, "INFO: recv: slave disconnected %s\n", inet_ntoa(conn->address.sin_addr));
			break;
		}
		else if (bytes_sent < 0) {	// bytes_sent < 0
			log_fprint(log_file, "ERROR: send to %s: %s (%d)\n", inet_ntoa(conn->address.sin_addr), strerror(errno), errno);
			break;
		}

		msg = CATPC_GET_MONITORING_VALUES;	// set to the default message value
		std::this_thread::sleep_for(period - (std::chrono::steady_clock::now() - start_time) % period);
	}

	client_count--;
	close(conn->sock);
}

void processing_loop()
{
	std::unordered_map<std::string, std::map<uint64_t, double>> mrc;
	
	while (!terminate) {
		// Wait for the signal of each worker thread
		unsigned i = 0;
		do {
			std::unique_lock<std::mutex> lk{global_mtx};
			global_cv.wait(lk);
		} while (++i < client_count);

		bool allocation_changed = false;
		bool mrc_completed = false;
		for (connection_t* conn : connections) {
			for (const auto& entry : sock_to_application[conn->sock]) {
				catpc_application* app_ptr = entry.second;
				// If the CLOS_id done is less than the last CLOS_id, continue MRC evaluation to the next CLOS
				if (!app_ptr->eval_done) {
					mrc[app_ptr->cmdline][app_ptr->values.llc] = (double)app_ptr->values.llc_misses / app_ptr->values.llc_references;
					log_fprint(log_file, "DEBUG: MRC[%.1fKB] = %.3f\n", app_ptr->values.llc / 1024.0, (double)app_ptr->values.llc_misses / app_ptr->values.llc_references);
					if (app_ptr->CLOS_id < sock_to_llcs[conn->sock][0].clos_count - 1) {
						// Go to the next CLOS
						app_ptr->CLOS_id++;
						
						// Avoid out of bound CLOS_id
						assert(app_ptr->CLOS_id < sock_to_llcs[conn->sock][0].clos_count);

						allocation_changed = true;
					}
					else { // (app_ptr->CLOS_id == sock_to_llcs[conn->sock][0].clos_count - 1)
						app_ptr->eval_done = true;
					}
				}
				else if (!app_ptr->smart_alloc_done) {	// eval done => MRC is completed
					mrc_completed = true;
					allocation_changed = true;
				}
			}
		}

		// Send notification and get required llc if MRC is completed
		for (connection_t* conn : connections) {
			if (mrc_completed) {
				for (const auto& entry : sock_to_application[conn->sock]) {
					catpc_application* app_ptr = entry.second;
					app_ptr->required_llc = get_required_llc(mrc[app_ptr->cmdline], sock_to_llcs[conn->sock]);
					app_ptr->smart_alloc_done = true;
					log_fprint(log_file, "INFO: required llc of %s is %.1fKB\n", entry.first.c_str(), app_ptr->required_llc / 1024.0);
				}
			}

			if (allocation_changed) {
				// Push perform allocation notification message
				notification_t& notif = sock_to_notification[conn->sock];
				std::lock_guard<std::mutex> lk{notif.mtx};
				notif.event_queue.push(std::make_pair(notification_t::event::PERFORM_ALLOCATION, ""));
			}
		}

		// Print on file
		for (const std::pair<std::string, std::map<uint64_t, double>>& entry : mrc) {
			std::ofstream ofs{"/tmp/" + entry.first.substr(entry.first.rfind('/') + 1) + ".csv", std::ios::trunc};
			for (const auto& e : entry.second) {
				ofs << (e.first / 1024.0) << ", " << e.second << "\n";
			}
			ofs.close();
		}
	}
}

void termination_handler(int signum) 
{
	if (signum == SIGTERM) {
		terminate = 1;
		close(sock);
	}
}

void watch_started_app()
{
	log_fprint(log_file, "INFO: starting \"started_app_watcher\"\n");
	const char* catpc_fifo = "/tmp/catpc.started.fifo";
	int bytes_read = 0;
	char buf[512];
	int fd;

	mkfifo(catpc_fifo, 0666);

	while (!terminate) {
		fd = open(catpc_fifo, O_RDONLY);
		bytes_read = read(fd, buf, sizeof(buf));
		if ( bytes_read > 0) {
			log_fprint(log_file, "INFO: app launched: \"%s\"\n", buf);
			for (std::pair<const int, notification_t>& it : sock_to_notification) {
				{
					std::scoped_lock<std::mutex> lk(it.second.mtx);
					it.second.event_queue.push(std::make_pair(notification_t::event::APP_ADDED, std::string(buf)));
				}
			}
		}
		else if (bytes_read < 0) {
			log_fprint(log_file, "ERROR: read failed: %s(%d)\n", strerror(errno), errno);
			return;
		}
		memset(buf, 0, sizeof(buf));
		close(fd);
	}
}

void watch_terminated_app()
{
	log_fprint(log_file, "INFO: starting \"terminated_app_watcher\"\n");
	const char* catpc_fifo = "/tmp/catpc.terminated.fifo";
	int bytes_read = 0;
	char buf[512];
	int fd;

	mkfifo(catpc_fifo, 0666);

	while (!terminate) {
		fd = open(catpc_fifo, O_RDONLY);
		bytes_read = read(fd, buf, sizeof(buf));
		if ( bytes_read > 0) {
			log_fprint(log_file, "INFO: app terminated: \"%s\"\n", buf);
			for (std::pair<const int, notification_t>& it : sock_to_notification) {
				{
					std::scoped_lock<std::mutex> lk(it.second.mtx);
					it.second.event_queue.push(std::make_pair(notification_t::event::APP_REMOVED, std::string(buf)));
				}
			}
		}
		else if (bytes_read < 0) {
			log_fprint(log_file, "ERROR: read failed: %s(%d)\n", strerror(errno), errno);
			return;
		}
		memset(buf, 0, sizeof(buf));
		close(fd);
	}
}