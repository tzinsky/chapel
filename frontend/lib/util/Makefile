# Copyright 2020-2024 Hewlett Packard Enterprise Development LP
# Copyright 2004-2019 Cray Inc.
# Other additional copyright holders may be indicated within.
#
# The entirety of this work is licensed under the Apache License,
# Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.
#
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ifndef CHPL_MAKE_HOME
export CHPL_MAKE_HOME=$(shell pwd)/../../..
endif

COMPILER_ROOT = ../../../compiler
COMPILER_SUBDIR = frontend/lib/util

#
# standard header
#
include $(COMPILER_ROOT)/make/Makefile.compiler.head

include Makefile.include

TARGETS = $(FRONTEND_UTIL_OBJS)

include $(COMPILER_ROOT)/make/Makefile.compiler.subdirrules

$(FRONTEND_UTIL_OBJDIR)/my_aligned_alloc.o: my_aligned_alloc.c $(OBJ_SUBDIR_MADE)
	$(CC) -c $(COMP_CFLAGS) -DCOMPILER_SUBDIR=$(COMPILER_SUBDIR) -o $@ $<

$(FRONTEND_UTIL_OBJDIR)/my_strerror_r.o: my_strerror_r.c $(OBJ_SUBDIR_MADE)
	$(CC) -c $(COMP_CFLAGS) -DCOMPILER_SUBDIR=$(COMPILER_SUBDIR) -o $@ $<

FORCE:

#
# standard footer
#
include $(COMPILER_ROOT)/make/Makefile.compiler.foot
