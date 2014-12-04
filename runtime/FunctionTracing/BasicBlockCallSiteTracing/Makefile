##===- runtime/libprofile/Makefile -------------------------*- Makefile -*-===##
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
##===----------------------------------------------------------------------===##

LEVEL = ../../..

LIBRARYNAME = bb_call_site_tracing_rt
LINK_LIBS_IN_SHARED = 1
SHARED_LIBRARY = 1

CPPFLAGS=-std=c++11 -stdlib=libstdc++ -I $(HOME)/usr/include -pg -g -Wall
# Build and install this archive.                                                                                                                  
BUILD_ARCHIVE = 1
override NO_INSTALL_ARCHIVES =

include $(LEVEL)/Makefile.common

