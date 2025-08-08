SHELL=/bin/bash -o pipefail

define builder
  ./build.sh $1 2>&1 | sed -e 's/'$$'\033''[[][0-9][0-9]*m//g'
endef

.PHONY: upload uploadfs build all

upload:
	$(call builder, -u)

uploadfs:
	$(call builder, -f)

build:
	$(call builder, -b)

all:
	$(call builder, -a)

zip:
	$(call builder, -z)

coco-lwm:
	$(call builder, -p COCO -g)

rs232-lwm:
	$(call builder, -p RS232 -g)

apple-lwm:
	$(call builder, -p APPLE -g)

pico-de-coco:
	make -C pico/coco/build

clean:
	$(call builder, -c)
