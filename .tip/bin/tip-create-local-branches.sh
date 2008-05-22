#!/bin/sh

. $(dirname "$0")/tip-lib

check_master

BS=`(git branch -a | grep origin | grep -v master | grep -v HEAD | grep -v " linus" | sed s@origin/@@; \
    git branch | grep -v master | grep -v HEAD | grep -v " linus") | sort | uniq -u`

for B in $BS
do
    git branch $B origin/$B
done
