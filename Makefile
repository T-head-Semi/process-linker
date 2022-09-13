ifneq ($(wildcard ../.param),)
	include ../.param
endif

INC_PATH ?= /usr/include
LIB_PATH ?= /usr/lib

OUTPUTDIR = ./output
LIBNAME = $(OUTPUTDIR)/libplink.so
server_NAME = $(OUTPUTDIR)/plinkserver
client_NAME = $(OUTPUTDIR)/plinkclient
stitcher_NAME = $(OUTPUTDIR)/plinkstitcher

INCS = ./inc
LIBSRCS = ./src/process_linker.c
LIBOBJS = $(LIBSRCS:.c=.o)
server_SRCS = ./test/plink_server.c
server_OBJS = $(server_SRCS:.c=.o)
client_SRCS = ./test/plink_client.c
client_OBJS = $(client_SRCS:.c=.o)
stitcher_SRCS = ./test/plink_stitcher.c
stitcher_OBJS = $(stitcher_SRCS:.c=.o)

CFLAGS = -I$(INCS) -I$(INC_PATH)/vidmem
CFLAGS += -pthread -fPIC -O

$(shell if [ ! -e $(OUTPUTDIR) ];then mkdir -p $(OUTPUTDIR); fi)

all: lib server client stitcher

lib: 
	$(CC) $(LIBSRCS) $(CFLAGS) -shared -o $(LIBNAME)

server: lib
	$(CC) $(server_SRCS) $(CFLAGS) -L$(OUTPUTDIR) -L$(LIB_PATH)/vidmem -lplink -lvmem -ldl -pthread -o $(server_NAME)

client: lib
	$(CC) $(client_SRCS) $(CFLAGS) -L$(OUTPUTDIR) -L$(LIB_PATH)/vidmem -lplink -lvmem -ldl -pthread -o $(client_NAME)

stitcher: lib
	$(CC) $(stitcher_SRCS) $(CFLAGS) -L$(OUTPUTDIR) -L$(LIB_PATH)/vidmem -lplink -lvmem -ldl -pthread -o $(stitcher_NAME)

clean:
	rm -rf $(OUTPUTDIR)

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@
