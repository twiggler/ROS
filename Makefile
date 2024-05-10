.PHONY: all loader kernel libr
all: loader

loader kernel libr:
	${MAKE} --directory=$@ $(TARGET)

loader: kernel libr
kernel: libr
