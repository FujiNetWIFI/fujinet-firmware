# a52plan, the cart's plan for an image, built for the host tools (abprep.py, soak.py, mkmapdb.py).
# It lands in ../build/tools, outside the source tree.

CC ?= cc
CFLAGS ?= -O1
FW := ../firmware
COMMON := ../../common
OUT := ../build/tools

SRCS := a52plan.c $(FW)/src/a52map.c $(FW)/src/a52map_db.c $(COMMON)/src/fuji_crc.c
HDRS := $(wildcard $(FW)/include/*.h $(COMMON)/include/*.h)

all: $(OUT)/a52plan

$(OUT)/a52plan: $(SRCS) $(HDRS)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I$(FW)/include -I$(COMMON)/include -o $@ $(SRCS)

.PHONY: all
