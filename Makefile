PROJ = firmware
CC = clang
CFLAGS = -O3 -ffast-math -Wall -Wextra -Ispidriver/c/common
PIN_DEF = pindef.pcf
DEVICE = up5k
PACKAGE = sg48

all: $(PROJ).bin

%.json: %.v
	yosys -p 'synth_ice40 -top top -json $@' $<

%.asc: $(PIN_DEF) %.json
	nextpnr-ice40 --$(DEVICE) --package $(PACKAGE) --asc $@ --pcf $< --json $*.json

%.bin: %.asc
	icepack $< $@

prog.out: prog.c spidriver/c/common/spidriver.c
	$(CC) $(CFLAGS) -o $@ $^ 

prog: prog.out
	@./prog.out /dev/ttyUSB0 $(PROJ).bin

clean:
	rm -f $(PROJ).json $(PROJ).asc $(PROJ).rpt $(PROJ).bin prog.out

.PHONY: all clean prog