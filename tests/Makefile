# Run from tests/ (or make -C tests ...). See README.md.
# make image                                  build olrt/olr:local-<sha> from this checkout
# make image-ref REF=bersler/OpenLogReplicator@2.0.0   build olrt/olr:2.0.0 from GitHub
# make test IMAGE=olrt/olr:local-<sha>
# make compare IMAGES="olrt/olr:2.0.0 olrt/olr:local-<sha>"
# make test IMAGE=... SCENARIOS="wide/ types/number-scale"   (ids, groups or tags)
# make unit-asan                              ctest under AddressSanitizer + UndefinedBehaviorSanitizer
PY ?= .venv/bin/python
IMAGE ?= olrt/olr:$(LOCAL_TAG)
IMAGES ?= $(IMAGE)
PROFILES ?= debezium,json
SCENARIOS ?=
RUN ?=

.PHONY: venv db-up db-down db-reset test compare record replicate check list image image-ref unit-asan-image unit-asan

venv: .venv/.installed
.venv/.installed: requirements.txt
	python3 -m venv .venv
	.venv/bin/pip install -q -r requirements.txt
	touch .venv/.installed

db-up: venv
	$(PY) -m olrt db-up

db-down:
	$(PY) -m olrt db-down

db-reset:
	$(PY) -m olrt db-down --volume

test: compare

compare: venv
	$(PY) -m olrt run $(if $(RUN),--run $(RUN)) $(foreach i,$(IMAGES),--image $(i)) --profile $(PROFILES) $(SCENARIOS)

record: venv
	$(PY) -m olrt record $(if $(RUN),--run $(RUN)) $(SCENARIOS)

replicate: venv
	$(PY) -m olrt replicate $(if $(RUN),--run $(RUN)) $(foreach i,$(IMAGES),--image $(i)) --profile $(PROFILES) $(SCENARIOS)

check: venv
	$(PY) -m olrt check $(if $(RUN),--run $(RUN)) $(foreach i,$(IMAGES),--image $(i)) --profile $(PROFILES) $(SCENARIOS)

list: venv
	$(PY) -m olrt list

# OLR image from this checkout (build context = repository root)
LOCAL_TAG ?= local-$(shell git rev-parse --short HEAD)
image:
	docker build -f docker/olr/Dockerfile -t olrt/olr:$(LOCAL_TAG) ..

# OLR image from GitHub: REF=<owner>/<repo>@<version>, a tag without the leading v, a branch or a commit
REF ?= bersler/OpenLogReplicator@2.0.0
image-ref:
	docker build -f docker/olr/Dockerfile -t olrt/olr:$(lastword $(subst @, ,$(REF))) \
	  --build-arg OPENLOGREPLICATOR_REPO=$(firstword $(subst @, ,$(REF))) \
	  --build-arg OPENLOGREPLICATOR_VERSION=$(lastword $(subst @, ,$(REF))) ..

# Unit tests (ctest) from this checkout, built with -DWITH_TESTS=ON under ASan + UBSan; any sanitizer report fails the run
# evaluated once, so the image built and the image run are the same even if HEAD moves during the build
ifndef UNIT_TAG
UNIT_TAG := unit-asan-$(shell git rev-parse --short HEAD)
endif
UNIT_BUILD_TYPE ?= Debug
SANITIZE ?= address,undefined
unit-asan-image:
	docker build -f docker/olr/Dockerfile -t olrt/olr:$(UNIT_TAG) \
	  --build-arg BUILD_TYPE=$(UNIT_BUILD_TYPE) --build-arg WITHTESTS=1 \
	  --build-arg EXTRA_CXXFLAGS="-fsanitize=$(SANITIZE) -fno-omit-frame-pointer" \
	  --build-arg EXTRA_LDFLAGS="-fsanitize=$(SANITIZE)" ..

unit-asan: unit-asan-image
	docker run --rm --user 0 --entrypoint ctest \
	  -e ASAN_OPTIONS=halt_on_error=1 -e UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	  olrt/olr:$(UNIT_TAG) --test-dir /opt/OpenLogReplicator-src/cmake-build-$(UNIT_BUILD_TYPE)-x86_64 --output-on-failure
