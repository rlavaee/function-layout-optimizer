##===- runtime/CacheCountingRuntime/Makefile -------------------------*- Makefile -*-===##
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
##===----------------------------------------------------------------------===##

LEVEL = ../..

LIBRARYNAME = cachecounting_rt
LINK_LIBS_IN_SHARED = 1
SHARED_LIBRARY = 1

# Build and install this archive.                                                                                                                  
BUILD_ARCHIVE = 1
override NO_INSTALL_ARCHIVES =

LFLAGS= $(LEVEL)/tools/papi-5.3.0/src/libpapi.a

include $(LEVEL)/Makefile.common

