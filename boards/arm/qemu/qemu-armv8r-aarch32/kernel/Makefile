############################################################################
# boards/arm/qemu/qemu-armv8r-aarch32/kernel/Makefile
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
############################################################################

include $(TOPDIR)/Make.defs

# The entry point name (if none is provided in the .config file)

CONFIG_INIT_ENTRYPOINT ?= user_start
ENTRYPT = $(patsubst "%",%,$(CONFIG_INIT_ENTRYPOINT))

# Get the paths to the libraries and the links script path in format that
# is appropriate for the host OS

USER_LIBPATHS = $(addprefix -L,$(call CONVERT_PATH,$(addprefix $(TOPDIR)$(DELIM),$(dir $(USERLIBS)))))
USER_LDSCRIPT = $(call CONVERT_PATH,$(BOARD_DIR)$(DELIM)scripts$(DELIM)nuttx_user.ld)
USER_HEXFILE += $(call CONVERT_PATH,$(TOPDIR)$(DELIM)nuttx_user.hex)
USER_SRECFILE += $(call CONVERT_PATH,$(TOPDIR)$(DELIM)nuttx_user.srec)
USER_BINFILE += $(call CONVERT_PATH,$(TOPDIR)$(DELIM)nuttx_user.bin)

USER_LDFLAGS = --undefined=userspace --undefined=$(ENTRYPT) --entry=$(ENTRYPT) -T $(addsuffix .tmp,$(USER_LDSCRIPT))
USER_LDLIBS = $(patsubst lib%,-l%,$(basename $(notdir $(USERLIBS))))
USER_LIBGCC = "${shell "$(CC)" $(ARCHCPUFLAGS) -print-libgcc-file-name}"

# Objects needed for multiple linking

ifeq ($(CONFIG_MM_KASAN_GLOBAL),y)
EXTRA_UOBJS += kasan_globals$(OBJEXT)
endif
ifeq ($(CONFIG_ALLSYMS),y)
EXTRA_UOBJS += allsyms$(OBJEXT)
endif

define LINK_ALLSYMS_KASAN
	$(if $(CONFIG_ALLSYMS),
	$(Q) $(TOPDIR)/tools/mkallsyms.py nuttx_user allsyms.tmp --orderbyname $(CONFIG_SYMTAB_ORDEREDBYNAME) --section ${CONFIG_ALLSYMS_SECTION}
	$(Q) $(call COMPILE, allsyms.tmp, allsyms$(OBJEXT), -x c -fno-sanitize=kernel-address)
	$(Q) $(call DELFILE, allsyms.tmp))
	$(if $(CONFIG_MM_KASAN_GLOBAL),
	$(Q) $(TOPDIR)/tools/kasan_global.py -e nuttx_user -o kasan_globals.tmp -a $(CONFIG_MM_KASAN_GLOBAL_ALIGN)
	$(Q) $(call COMPILE, kasan_globals.tmp, kasan_globals$(OBJEXT), -x c -fno-sanitize=kernel-address)
	$(Q) $(call DELFILE, kasan_globals.tmp))
	$(Q) $(LD) $(USER_LDFLAGS) $(USER_LIBPATHS) -o nuttx_user $(OBJS) $(EXTRA_UOBJS) \
		--start-group $(USER_LDLIBS) $(USER_LIBGCC) --end-group
endef

# Source files

COBJS = $(CSRCS:.c=$(OBJEXT))
OBJS  = $(COBJS)

# Targets:

all: $(TOPDIR)$(DELIM)nuttx_user $(TOPDIR)$(DELIM)User.map
.PHONY: nuttx_user depend clean distclean

$(COBJS): %$(OBJEXT): %.c
	$(call COMPILE, $<, $@)

$(addsuffix .tmp,$(USER_LDSCRIPT)): $(USER_LDSCRIPT)
	$(call PREPROCESS,$(patsubst %.tmp,%,$@),$@)

# Create the nuttx_user file containing all of the user-mode code

nuttx_user: $(OBJS) $(addsuffix .tmp,$(USER_LDSCRIPT))
ifeq ($(CONFIG_ALLSYMS)$(CONFIG_MM_KASAN_GLOBAL),)
	$(Q) $(LD) -o $@ $(USER_LDFLAGS) $(USER_LIBPATHS) $(OBJS) --start-group $(USER_LDLIBS) $(USER_LIBGCC) --end-group
else
	$(Q) $(call LINK_ALLSYMS_KASAN)
	$(Q) $(call LINK_ALLSYMS_KASAN)
	$(Q) $(call LINK_ALLSYMS_KASAN)
	$(Q) $(call LINK_ALLSYMS_KASAN)
endif

$(TOPDIR)$(DELIM)nuttx_user: nuttx_user
	@echo "LD: nuttx_user"
	$(Q) cp -a nuttx_user $(TOPDIR)$(DELIM)nuttx_user
ifeq ($(CONFIG_INTELHEX_BINARY),y)
	@echo "CP: nuttx_user.hex"
	$(Q) $(OBJCOPY) $(OBJCOPYARGS) -O ihex nuttx_user $(USER_HEXFILE)
endif
ifeq ($(CONFIG_MOTOROLA_SREC),y)
	@echo "CP: nuttx_user.srec"
	$(Q) $(OBJCOPY) $(OBJCOPYARGS) -O srec nuttx_user $(USER_SRECFILE)
endif
ifeq ($(CONFIG_RAW_BINARY),y)
	@echo "CP: nuttx_user.bin"
	$(Q) $(OBJCOPY) $(OBJCOPYARGS) -O binary nuttx_user $(USER_BINFILE)
endif

$(TOPDIR)$(DELIM)User.map: nuttx_user
	@echo "MK: User.map"
	$(Q) $(NM) nuttx_user >$(TOPDIR)$(DELIM)User.map
	$(Q) $(CROSSDEV)size nuttx_user

.depend:

depend: .depend

clean:
	$(call DELFILE, nuttx_user)
	$(call DELFILE, "$(TOPDIR)$(DELIM)nuttx_user.*")
	$(call DELFILE, "$(TOPDIR)$(DELIM)User.map")
	$(call CLEAN)

distclean: clean
