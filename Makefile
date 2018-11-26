.PHONY: all power_monitor clean

all: power_monitor

install:
	cp power_monitor /usr/local/bin/power_monitor

power_monitor:
#gcc -g power_config.c power_meter.c power_monitor.c power_wave.c -o power_monitor `pkg-config --cflags --libs gtk+-3.0 libgpib`
	gcc -g -Wall  power_meter.c power_monitor.c power_wave.c power_tuning.c -o power_monitor `pkg-config --cflags --libs gtk+-3.0 libgpib`

clean:
	rm power_monitor
