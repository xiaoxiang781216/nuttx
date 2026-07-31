############################################################################
# arch/zsp/src/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

include $(TOPDIR)/tools/Make.defs
include chip/Make.defs

ARCH_SRCDIR = $(TOPDIR)$(DELIM)arch$(DELIM)$(CONFIG_ARCH)$(DELIM)src

INCLUDES += ${INCDIR_PREFIX}$(ARCH_SRCDIR)$(DELIM)chip
INCLUDES += ${INCDIR_PREFIX}$(ARCH_SRCDIR)$(DELIM)common
INCLUDES += ${INCDIR_PREFIX}$(TOPDIR)$(DELIM)sched

CPPFLAGS += $(INCLUDES)
CFLAGS += $(INCLUDES)
CXXFLAGS += $(INCLUDES)
AFLAGS += $(INCLUDES)

NUTTX = $(call CONVERT_PATH,$(TOPDIR)$(DELIM)nuttx$(EXEEXT))

HEAD_OBJ = $(HEAD_ASRC:.S=$(OBJEXT))
STARTUP_OBJS ?= $(HEAD_OBJ)

ASRCS = $(CHIP_ASRCS) $(CMN_ASRCS)
AOBJS = $(ASRCS:.S=$(OBJEXT))

CSRCS = $(CHIP_CSRCS) $(CMN_CSRCS)
COBJS = $(CSRCS:.c=$(OBJEXT))

SRCS = $(ASRCS) $(CSRCS)
OBJS = $(AOBJS) $(COBJS)

VPATH = common:chip:nanoplus


all: $(HEAD_OBJ) libarch$(LIBEXT)

.PHONY: board/libboard$(LIBEXT) context

$(AOBJS) $(HEAD_OBJ): %$(OBJEXT): %.S
	$(call ASSEMBLE, $<, $@)

$(COBJS): %$(OBJEXT): %.c
	$(call COMPILE, $<, $@)

libarch$(LIBEXT): $(OBJS)
	$(call ARCHIVE, $@, $(OBJS))

board/libboard$(LIBEXT):
	$(Q) $(MAKE) -C board libboard$(LIBEXT) EXTRAFLAGS="$(EXTRAFLAGS)"

nuttx$(EXEEXT): $(HEAD_OBJ) board/libboard$(LIBEXT)
	$(Q) echo "LD: nuttx"
	$(Q) $(call PREPROCESS, $(LDSCRIPT), $(notdir $(LDSCRIPT:.in=)))
	$(Q) $(LD) --entry=_main $(LDFLAGS) \
		-T $(notdir $(LDSCRIPT:.in=)) \
		-o $(NUTTX) $(HEAD_OBJ) zsp_uvt.o \
		--start-group $(addprefix $(TOPDIR)/staging/,$(LINKLIBS)) \
		board/libboard.a \
		$(EXTRA_LIBS) \
		$(TOPDIR)/staging/libarch.a \
		--end-group

context::

.depend: Makefile $(SRCS) $(TOPDIR)$(DELIM).config
	$(Q) touch $@

depend: .depend

clean:
	$(call DELFILE, libarch$(LIBEXT))
	$(call DELFILE, .depend)
	$(call DELFILE, Make.dep)
	$(call CLEAN)

distclean: clean
