CC ?= gcc
AR ?= ar
RANLIB ?= ranlib
PKG_CONFIG ?= pkg-config

CFLAGS ?= -O2 -g
WARNFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Werror
PICFLAGS := -fPIC
THREAD_FLAGS := -pthread

ifeq ($(MAKECMDGOALS),clean)
USB_CFLAGS :=
USB_LIBS :=
OPENSSL_CFLAGS :=
OPENSSL_LIBS :=
else
ifeq ($(GX_FAKE_LIBUSB),1)
USB_CFLAGS := -Itests/fake_libusb
USB_LIBS := -Wl,-l:libusb-1.0.so.0
else
USB_CFLAGS := $(shell $(PKG_CONFIG) --cflags libusb-1.0 2>/dev/null)
USB_LIBS := $(shell $(PKG_CONFIG) --libs libusb-1.0 2>/dev/null)
ifeq ($(strip $(USB_LIBS)),)
$(error libusb-1.0 development files were not found)
endif
endif
OPENSSL_CFLAGS := $(shell $(PKG_CONFIG) --cflags openssl 2>/dev/null)
OPENSSL_LIBS := $(shell $(PKG_CONFIG) --libs openssl 2>/dev/null)
ifeq ($(strip $(OPENSSL_LIBS)),)
$(error OpenSSL development files were not found)
endif
endif

CPPFLAGS ?= -Iinclude
CPPFLAGS += $(USB_CFLAGS) $(OPENSSL_CFLAGS) -D_POSIX_C_SOURCE=200809L
LDLIBS_COMMON := -lm $(THREAD_FLAGS)
LDLIBS_DEVICE := $(USB_LIBS) $(OPENSSL_LIBS) $(LDLIBS_COMMON)

BUILD := build

EXTRACTOR_SOURCES := \
	src/gx_extractor.c \
	src/gx_feature_aux_metadata.c \
	src/gx_feature_candidate.c \
	src/gx_feature_compact.c \
	src/gx_feature_descriptor_assembly.c \
	src/gx_feature_descriptor_lifecycle.c \
	src/gx_feature_descriptor_sample.c \
	src/gx_feature_detector.c \
	src/gx_feature_filter.c \
	src/gx_feature_global_postprocess.c \
	src/gx_feature_object.c \
	src/gx_feature_orientation.c \
	src/gx_feature_postprocess.c \
	src/gx_feature_primitives.c \
	src/gx_feature_pyramid.c \
	src/gx_feature_quality_map.c \
	src/gx_feature_quality_post.c \
	src/gx_feature_record.c \
	src/gx_feature_refine.c \
	src/gx_feature_response.c \
	src/gx_feature_root.c \
	src/gx_preprocess.c

DEVICE_SOURCES := \
	src/gx_device.c \
	src/gx_usb.c \
	src/gx_transport.c \
	src/gx_protocol.c \
	src/gx_session.c \
	src/gx_secret.c \
	src/gx_tls.c \
	src/gx_sensor.c \
	src/gx_sensor_data.c \
	src/gx_capture.c

PIPELINE_SOURCES := src/gx_pipeline.c
ENROLLMENT_SOURCES := src/gx_enrollment.c
MATCHER_SOURCES := src/gx_matcher.c

EXTRACTOR_OBJECTS := $(EXTRACTOR_SOURCES:src/%.c=$(BUILD)/extractor/%.o)
DEVICE_OBJECTS := $(DEVICE_SOURCES:src/%.c=$(BUILD)/device/%.o)
PIPELINE_OBJECTS := $(PIPELINE_SOURCES:src/%.c=$(BUILD)/pipeline/%.o)
ENROLLMENT_OBJECTS := $(ENROLLMENT_SOURCES:src/%.c=$(BUILD)/enrollment/%.o)
MATCHER_OBJECTS := $(MATCHER_SOURCES:src/%.c=$(BUILD)/matcher/%.o)

EXTRACTOR_STATIC := $(BUILD)/libgx5125extractor.a
DEVICE_STATIC := $(BUILD)/libgx5125device.a
PIPELINE_STATIC := $(BUILD)/libgx5125pipeline.a
ENROLLMENT_STATIC := $(BUILD)/libgx5125enrollment.a
MATCHER_STATIC := $(BUILD)/libgx5125matcher.a

EXTRACTOR_SELFTEST := $(BUILD)/gx5125-extractor-selftest
DEVICE_SELFTEST := $(BUILD)/gx5125-device-selftest
PIPELINE_SELFTEST := $(BUILD)/gx5125-pipeline-selftest
ENROLLMENT_SELFTEST := $(BUILD)/gx5125-enrollment-selftest
MATCHER_SELFTEST := $(BUILD)/gx5125-matcher-selftest
CHICAGOHS_PROBE := $(BUILD)/gx5125-chicagohs-probe

.PHONY: all clean test

all: $(EXTRACTOR_STATIC) $(DEVICE_STATIC) $(PIPELINE_STATIC) \
     $(ENROLLMENT_STATIC) $(MATCHER_STATIC) \
     $(EXTRACTOR_SELFTEST) $(DEVICE_SELFTEST) $(PIPELINE_SELFTEST) \
     $(ENROLLMENT_SELFTEST) $(MATCHER_SELFTEST) $(CHICAGOHS_PROBE)

$(BUILD)/extractor $(BUILD)/device $(BUILD)/pipeline $(BUILD)/enrollment $(BUILD)/matcher:
	mkdir -p $@

$(BUILD)/extractor/%.o: src/%.c | $(BUILD)/extractor
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(PICFLAGS) -c $< -o $@

$(BUILD)/device/%.o: src/%.c | $(BUILD)/device
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(PICFLAGS) $(THREAD_FLAGS) -c $< -o $@

$(BUILD)/pipeline/%.o: src/%.c | $(BUILD)/pipeline
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(PICFLAGS) $(THREAD_FLAGS) -c $< -o $@

$(BUILD)/enrollment/%.o: src/%.c | $(BUILD)/enrollment
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(PICFLAGS) -c $< -o $@

$(BUILD)/matcher/%.o: src/%.c | $(BUILD)/matcher
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(PICFLAGS) -c $< -o $@

$(EXTRACTOR_STATIC): $(EXTRACTOR_OBJECTS)
	mkdir -p $(BUILD)
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(DEVICE_STATIC): $(DEVICE_OBJECTS)
	mkdir -p $(BUILD)
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(PIPELINE_STATIC): $(PIPELINE_OBJECTS) $(EXTRACTOR_OBJECTS) $(DEVICE_OBJECTS)
	mkdir -p $(BUILD)
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(ENROLLMENT_STATIC): $(ENROLLMENT_OBJECTS)
	mkdir -p $(BUILD)
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(MATCHER_STATIC): $(MATCHER_OBJECTS)
	mkdir -p $(BUILD)
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(EXTRACTOR_SELFTEST): tests/test_extractor.c $(EXTRACTOR_STATIC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -o $@ $< $(EXTRACTOR_STATIC) $(LDLIBS_COMMON)

$(DEVICE_SELFTEST): tests/test_device.c $(DEVICE_STATIC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(THREAD_FLAGS) -o $@ $< $(DEVICE_STATIC) $(LDLIBS_DEVICE)

$(PIPELINE_SELFTEST): tests/test_pipeline.c $(PIPELINE_STATIC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(THREAD_FLAGS) -o $@ $< $(PIPELINE_STATIC) $(LDLIBS_DEVICE)

$(ENROLLMENT_SELFTEST): tests/test_enrollment.c $(ENROLLMENT_STATIC) $(PIPELINE_STATIC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(THREAD_FLAGS) -o $@ $< $(ENROLLMENT_STATIC) $(PIPELINE_STATIC) $(LDLIBS_DEVICE)

$(MATCHER_SELFTEST): tests/test_matcher.c $(MATCHER_STATIC) $(ENROLLMENT_STATIC) $(EXTRACTOR_STATIC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -o $@ $< $(MATCHER_STATIC) $(ENROLLMENT_STATIC) $(EXTRACTOR_STATIC) $(LDLIBS_COMMON)

$(CHICAGOHS_PROBE): tools/gx5125-chicagohs-probe.c $(DEVICE_STATIC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(THREAD_FLAGS) -o $@ $< $(DEVICE_STATIC) $(LDLIBS_DEVICE)

test: all
	$(EXTRACTOR_SELFTEST)
	$(DEVICE_SELFTEST)
	$(PIPELINE_SELFTEST)
	$(ENROLLMENT_SELFTEST)
	$(MATCHER_SELFTEST)
	$(CHICAGOHS_PROBE) --selftest

clean:
	rm -rf $(BUILD)
