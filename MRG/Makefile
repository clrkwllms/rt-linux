# Makefile for source rpm: kernel-rt
# $Id: Makefile,v 1.2 2007/04/26 21:34:06 streeter Exp $
NAME := kernel-rt
SPECFILE = $(firstword $(wildcard *.spec))

# use noarch for make prep instead of the current CPU
# noarch creates and checks all config files not just the current one,
# in addition "i386" isn't a valid kernel target
PREPARCH  = noarch

include ../common/Makefile.common
