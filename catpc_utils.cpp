#include "catpc_utils.hpp"

#include <dirent.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>

int get_pids_by_cmdline(pid_t* pids, const char* cmdline)
{
	char filepath[32];
	char buf[4096];
	char c = '\0';
	int i = 0, sz = 0;
	pid_t pid;
	FILE* fp = popen("/bin/ps -A -o pid=", "r");	// list of all pids
	FILE* cmdfile = NULL;
	
	if (fp == NULL) {
		return -1;
	}

	while (fscanf(fp, "%d", &pid) != EOF) {
		sprintf(filepath, "/proc/%d/cmdline", pid);
		cmdfile = fopen(filepath, "r");
		
		if (cmdfile == NULL)
			continue;

		// read command of the process
		i = 0;
		while ( fscanf(cmdfile, "%c", &c) != EOF) {
			if (c == '\0') {
				continue;
				//c = ' ';	
			}
			buf[i++] = c;
		}
		buf[i] = '\0';

		if (strcmp(buf, cmdline) == 0) {
			pids[sz++] = pid;
		}
		fclose(cmdfile);
	}

	pclose(fp);

	return sz;
}

void log_fprint(FILE* fp, const char* fmt, ...)
{
	va_list args;
	time_t timestamp;
	struct tm* now;
	char buff[70];
	
	time(&timestamp);
	now  = localtime(&timestamp);
	strftime(buff, sizeof buff, "%F %H:%M:%S", now);
	
	fprintf(fp, "[%s] ", buff);
	va_start(args, fmt);
	vfprintf(fp, fmt, args);
	va_end(args);
	fflush(fp);
}

std::ostream& operator<<(std::ostream& os, const std::map<uint64_t, double>& m)
{
	for (auto& entry : m) {
		os << entry.first << ", " << entry.second << "\n";
	}
	return os;
}