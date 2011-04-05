EXTRA_OPTS := -ggdb
PKGS := libnautilus-extension gnome-vfs-module-2.0 gnome-system-tools gnome-vfs-2.0 libnautilus-extension libglade-2.0 libgnomeui-2.0 glib-2.0
CFLAGS := $(EXTRA_OPTS) $(shell pkg-config --cflags $(PKGS))
LDFLAGS := $(shell pkg-config --libs $(PKGS))
all: main.c
	gcc  $(CFLAGS) $< -o $@ $(LDFLAGS)

