############################################################################
# syscall/Makefile
#
# SPDX-License-Identifier: Apache-2.0
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
###########################################################################

include $(TOPDIR)/Make.defs

include proxies/Make.defs
include stubs/Make.defs
include wraps/Make.defs
include ppcs/Make.defs

MKSYSCALL = "$(TOPDIR)$(DELIM)tools$(DELIM)mksyscall$(HOSTEXEEXT)"
CSVFILE = $(TOPDIR)$(DELIM)syscall$(DELIM)syscall.csv
ifneq ($(CONFIG_LIB_SYSCALL_FILES),"")
  CSVFILE += $(addprefix $(TOPDIR)$(DELIM),$(subst ",,$(subst ;, ,$(CONFIG_LIB_SYSCALL_FILES))))
endif
LOOKUPFILE = "$(TOPDIR)$(DELIM)include$(DELIM)syscall_lookup.h"

ifeq ($(CONFIG_LIB_PPCCALL),y)
  PPCCSVFILE = $(addprefix $(TOPDIR)$(DELIM),$(subst ",,$(subst ;, ,$(CONFIG_LIB_PPCCALL_FILES))))
  PPCLOOKUPFILE = "$(TOPDIR)$(DELIM)include$(DELIM)ppccall_lookup.h"
endif

STUB_SRCS += syscall_names.c

ifeq ($(CONFIG_LIB_SYSCALL_WRAPPER),y)
  WRAP_SRCS += syscall_names.c
endif

STUB_SRCS += syscall_stublookup.c

AOBJS = $(ASRCS:.S=$(OBJEXT))

PROXY_OBJS = $(PROXY_SRCS:.c=$(OBJEXT))
STUB_OBJS = $(STUB_SRCS:.c=$(OBJEXT))
WRAP_OBJS = $(WRAP_SRCS:.c=$(OBJEXT))
PPC_OBJS = $(PPC_SRCS:.c=$(OBJEXT))

CSRCS = $(sort $(PROXY_SRCS) $(STUB_SRCS) $(WRAP_SRCS) $(PPC_SRCS))
COBJS = $(CSRCS:.c=$(OBJEXT))

SRCS = $(ASRCS) $(CSRCS)
OBJS = $(AOBJS) $(COBJS)

PROXYDEPPATH = --dep-path proxies
STUBDEPPATH = --dep-path stubs
WRAPDEPPATH = --dep-path wraps
PPCDEPPATH = --dep-path ppcs
VPATH = proxies:stubs:wraps:ppcs

BIN1 = libproxies$(LIBEXT)
BIN2 = libstubs$(LIBEXT)
BIN3 = libwraps$(LIBEXT)
BIN4 = libppcs$(LIBEXT)
SYSCALLWRAPS = syscall_wraps.ldcmd

all: $(BIN1) $(BIN2) $(BIN3) $(BIN4) $(SYSCALLWRAPS)
.PHONY: context depend clean distclean

$(AOBJS): %$(OBJEXT): %.S
	$(call ASSEMBLE, $<, $@)

$(COBJS): %$(OBJEXT): %.c
	$(call COMPILE, $<, $@)

$(BIN1): $(PROXY_OBJS)
	$(call ARCHIVE, $@, $(PROXY_OBJS))

$(BIN2): $(STUB_OBJS)
	$(call ARCHIVE, $@, $(STUB_OBJS))

$(BIN3): $(WRAP_OBJS)
	$(call ARCHIVE, $@, $(WRAP_OBJS))

$(BIN4): $(PPC_OBJS)
	$(call ARCHIVE, $@, $(PPC_OBJS))

$(SYSCALLWRAPS): .context

makedepfile: $(CSRCS:.c=.ddc) $(ASRCS:.S=.dds)
	$(call CATFILE, Make.dep, $^)
	$(call DELFILE, $^)

.depend: Makefile $(SRCS) $(TOPDIR)$(DELIM).config
	$(Q) $(MAKE) makedepfile DEPPATH="$(PROXYDEPPATH) $(STUBDEPPATH) $(WRAPDEPPATH) $(PPCDEPPATH)"
	$(Q) touch $@

depend: .depend

.context: $(CSVFILE) $(PPCCSVFILE)
ifneq ($(filter y,$(CONFIG_LIB_SYSCALL)$(CONFIG_LIB_SYSCALL_WRAPPER)),)
	$(Q) $(MAKE) -C $(TOPDIR)$(DELIM)tools -f Makefile.host mksyscall
	$(Q) $(MKSYSCALL) -l $(CSVFILE)
	$(Q) cp syscall_lookup.h $(LOOKUPFILE)
endif
ifeq ($(CONFIG_LIB_SYSCALL),y)
	$(Q) (cd proxies; $(MKSYSCALL) -p $(CSVFILE);)
	$(Q) (cd stubs; $(MKSYSCALL) -s $(CSVFILE);)
endif
ifeq ($(CONFIG_LIB_PPCCALL),y)
	$(Q) (cd ppcs; $(MKSYSCALL) -c $(PPCCSVFILE) $(OPTIONS);)
	$(Q) cp ppcs$(DELIM)ppccall_lookup.h $(PPCLOOKUPFILE)
	$(Q) cp ppccall.h ppcs$(DELIM)ppccall.h
endif
	$(Q) touch $@

context: .context

clean:
	$(call DELFILE, $(BIN1))
	$(call DELFILE, $(BIN2))
	$(call DELFILE, $(BIN3))
	$(call DELFILE, $(BIN4))
ifneq ($(OBJEXT),)
	$(call DELFILE, proxies$(DELIM)*$(OBJEXT))
	$(call DELFILE, stubs$(DELIM)*$(OBJEXT))
	$(call DELFILE, wraps$(DELIM)*$(OBJEXT))
	$(call DELFILE, ppcs$(DELIM)*$(OBJEXT))
endif
	$(call CLEAN)

distclean: clean
	$(call DELFILE, .context)
	$(call DELFILE, Make.dep)
	$(call DELFILE, .depend)
	$(call DELFILE, proxies$(DELIM)*.c)
	$(call DELFILE, stubs$(DELIM)*.c)
	$(call DELFILE, wraps$(DELIM)*.c)
	$(call DELFILE, $(LOOKUPFILE))
	$(call DELFILE, ppcs$(DELIM)*.c)
	$(call DELFILE, ppcs$(DELIM)*.h)
	$(call DELFILE, $(PPCLOOKUPFILE))
	$(call DELFILE, $(SYSCALLWRAPS))

-include Make.dep
