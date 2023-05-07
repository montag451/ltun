LUA_VERSION=5.3
LUA_CFLAGS=$(shell pkg-config --cflags lua$(LUA_VERSION))
CPPFLAGS=$(MY_CPPFLAGS)
CFLAGS=-fpic -Wall -Wextra -Werror $(LUA_CFLAGS) $(MY_CFLAGS)
ifeq ($(DEBUG),1)
    CFLAGS+=-g
else
    CFLAGS+=-O2
endif
LDFLAGS=-shared $(MY_LDFLAGS)
TARGET=ltun.so
OBJS=ltun.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

.PHONY: clean

clean:
	rm -f $(OBJS)
	rm -f $(TARGET)

