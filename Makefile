SHELL = /bin/bash -o pipefail

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

%-lwm:
	$(call builder, -p $(shell echo $* | tr '[:lower:]' '[:upper:]') -g)

pico-de-coco:
	make -C pico/coco/build

clean:
	$(call builder, -c)
