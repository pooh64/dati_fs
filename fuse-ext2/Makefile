src += $(wildcard ../e2img/*.c)
CFLAGS += -I../e2img
CFLAGS += `pkg-config fuse3 --cflags`
LDFLAGS += `pkg-config fuse3 --libs` -lext2fs
include ../simple.mk
