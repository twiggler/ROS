.PHONY: all loader kernel libr services
all: loader

loader kernel libr:
	${MAKE} --directory=$@ $(TARGET)

services:
	${MAKE} --directory=services TARGET=$(TARGET)

loader: kernel libr services
kernel: libr
