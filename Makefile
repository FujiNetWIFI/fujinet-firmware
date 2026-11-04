SHELL = /bin/bash -o pipefail

# make <target> YES=1 passes -y: install missing prerequisites without asking.
define builder
  ./build.sh $(if $(YES),-y) $1
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

# make tests/<name> builds that ctest in build/, if it has a target, and runs it.
tests/%: FORCE
	cmake --build build --target cmake_check_build_system
	@if cmake --build build --target help | grep -w '$*' > /dev/null; then \
	  cmake --build build --target $*; \
	fi
	cd build && ctest -R '^$*$$' --output-on-failure --no-tests=error

FORCE:
