CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -D_GNU_SOURCE
LDFLAGS = -lsqlite3

TARGET  = rasd
SRCDIR  = src
SRCS    = $(wildcard $(SRCDIR)/*.c)
OBJS    = $(SRCS:.c=.o)

PREFIX     = /usr/local
BINDIR     = $(PREFIX)/bin
SERVICEDIR = /etc/systemd/system

.PHONY: all clean install uninstall install-service

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(SRCDIR)/*.o $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	rm -f $(BINDIR)/$(TARGET)

install-service: install
	install -Dm644 rasd.service $(SERVICEDIR)/rasd.service
	systemctl daemon-reload
	@echo "Run: systemctl enable --now rasd"
