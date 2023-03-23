CC = g++
LIBDIR = ../../../../lib
CFLAGS = -I $(LIBDIR) -I . -lpthread -lboost_math_tr1 -g3 -O2 -std=gnu++17 -Wall -Wextra \
	-Wmissing-declarations -Wpointer-arith -Wcast-qual -Wundef -Wwrite-strings \
	-Wformat -Wformat-security -fstack-protector -fPIE -D_FORTIFY_SOURCE=2 \
	-Wunreachable-code -Wsign-compare -Wno-endif-labels

LDFLAGS = -L$(LIBDIR) -pie -z noexecstack -z relro -z now
LDLIBS = -lpthread -lpqos -lcgroup

SRCS = catpc_utils.cpp catpc_allocator.cpp catpc_monitor.cpp
OBJS = $(SRCS:.cpp=.o)

MASTER = master_daemon
SLAVE = slave_daemon

all : $(MASTER) $(SLAVE)

%.o: %.cpp
	$(CC) -o $@ -c $< $(CFLAGS)

dump:
	echo $(SRCS)
	echo $(OBJS)

$(MASTER): $(MASTER).o $(OBJS)
$(SLAVE): $(SLAVE).o $(OBJS)

kill:
	sudo ./stop_daemon.sh

clean:
	-rm -f $(MASTER) $(SLAVE) ./*.o

	