#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

EXE_SUFFIX := .exe

CP_PIN := $(PINKIT)/source/tools/Utils/obj-$(TARGET)/cp-pin.exe
PROGRAMS := memory_leak_detection_$(TARGET)$(EXE_SUFFIX)  mt_counter_$(TARGET)$(EXE_SUFFIX)

TARGET_FLAGS =
ifeq ($(TARGET),ia32)
	TARGET_FLAGS += -m32
else
	TARGET_FLAGS += -m64
endif

TESTS := basic.test \
		 inscount_img.test \
		 inscount_skl.test \
		 call_trace_mt_replace.test \
		 call_trace_mt.test \
		 call_trace_probe.test \
		 call_trace.test \
		 malloctrace_noblock.test


.PHONY: test clean $(PROGRAMS) $(TESTS)

export GLUE_PIN_KIT := $(PINKIT)

clean:
	rm -f *.out

sanity: $(TESTS)

test: $(TESTS) malloctrace_probe_timer_cb.test

basic.test: %.test: %.py
	$(PYTHON3) $< --glueargs="-services-path TestServices" -- $(CP_PIN) makefile makefile.$@
	rm makefile.$@

inscount_img.test: %.test: %.py
	$(PYTHON3) $< --glueargs="-services-path TestServices" -- $(CP_PIN) makefile makefile.$@
	rm makefile.$@

inscount_skl.test: %.test: %.py
	$(PYTHON3) $< --glueargs="-services-path TestServices" -- $(CP_PIN) makefile makefile.$@
	rm makefile.$@

call_trace_mt.test: mt_counter_$(TARGET)$(EXE_SUFFIX)
	$(PYTHON3) call_trace_mt.py  -- ./$<

call_trace_mt_replace.test: mt_counter_$(TARGET)$(EXE_SUFFIX)
	$(PYTHON3) call_trace_mt_replace.py --glueargs="-services-path TestServices" -- ./$< "true"

call_trace.test: memory_leak_detection_$(TARGET)$(EXE_SUFFIX)
	$(PYTHON3) call_trace.py -- ./$<

call_trace_probe.test: memory_leak_detection_$(TARGET)$(EXE_SUFFIX)
	$(PYTHON3) call_trace_probe.py --pinargs="-probe" -- ./$<

malloctrace_noblock.test: %.test: %.py
	$(PYTHON3) $< --glueargs="-services-path TestServices" -- $(CP_PIN) makefile makefile.$@
	rm makefile.$@

malloctrace_probe_timer_cb.test: memory_leak_detection_$(TARGET)$(EXE_SUFFIX)
	$(PYTHON3) malloctrace_probe_timer_cb.py --glueargs="-services-path TestServices" --pinargs="-probe" -- ./$<

$(PROGRAMS): %_$(TARGET)$(EXE_SUFFIX): %.c
	$(CC) -g -O0 $(TARGET_FLAGS) -pthread -o $@ $<
