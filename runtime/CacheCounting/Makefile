##===- runtime/CacheCountingRuntime/Makefile -------------------------*- Makefile -*-===##
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
##===----------------------------------------------------------------------===##

LEVEL = ../..

LIBRARYNAME = cache_counting_rt
LINK_LIBS_IN_SHARED = 1
SHARED_LIBRARY = 1
LIBPAPI=$(HOME)/usr/lib/libpapi.a

# Build and install this archive.                                                                                                                  
BUILD_ARCHIVE = 1
override NO_INSTALL_ARCHIVES =
CFLAGS=-I $(HOME)/usr/include
LFLAGS= $(LIBPAPI)

include $(LEVEL)/Makefile.common

