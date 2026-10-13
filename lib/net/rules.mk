local_dir := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

OBJS += \
	$(local_dir)/arp.o \
	$(local_dir)/dhcp.o \
	$(local_dir)/ethernet.o \
	$(local_dir)/icmp.o \
	$(local_dir)/ipv4.o \
	$(local_dir)/net.o \
	$(local_dir)/udp.o
