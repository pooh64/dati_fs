src += $(wildcard *.c)
obj += $(src:.c=.o)
dep += $(obj:.o=.d)

CC = gcc
CFLAGS += -g --std=gnu11 -MMD -Wall -Wpointer-arith
LDFLAGS +=

all: a.out

a.out: $(obj)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

-include $(dep)

.PHONY: clean
clean:
	rm -f a.out $(obj) $(dep)
