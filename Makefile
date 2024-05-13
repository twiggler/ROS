.PHONY: all loader kernel libr services
all: loader

loader kernel libr services:
	${MAKE} --directory=$@ $(TARGET)

loader: kernel libr services
kernel: libr
