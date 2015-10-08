##===- runtime/libprofile/Makefile -------------------------*- Makefile -*-===##
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
##===----------------------------------------------------------------------===##

LEVEL = ../..

LIBRARYNAME = dump_trace_rt
LINK_LIBS_IN_SHARED = 1
SHARED_LIBRARY = 1

# Build and install this archive.                                                                                                                  
BUILD_ARCHIVE = 1
override NO_INSTALL_ARCHIVES =
CPPFLAGS=-std=c++0x -I $(HOME)/usr/include -g
#CPPFLAGS=-std=c++11 -stdlib=libstdc++ -I $(HOME)/usr/include
#CPPFLAGS=-I $(HOME)/usr/include

include $(LEVEL)/Makefile.common

