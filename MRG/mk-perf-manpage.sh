#!/bin/bash
#
# Author: John Kacur <jkacur@redhat.com>
#
# This script is needed to build the perf manpages as an extra manual step.
# Currently RHEL5 doesn't have the necessary tools to do this.

# If GIT_REPO has a value, then use it, else get it from the command-line
GIT_REPO=${GIT_REPO:-$1}

if [ -z "${GIT_REPO}" ]; then
	echo "Usage: $(basename $0) git_repo"
	exit -1
fi

rm -f perf-manpage.tar.bz2
rm -rf usr

CWD=$(pwd)

pushd $GIT_REPO || exit -1
	export prefix=""
	export DESTDIR="${CWD}/usr/"

	make -C tools/perf prefix="${prefix}" DESTDIR="${DESTDIR}" install-man
popd

tar cjfv perf-manpage.tar.bz2 usr || exit -1

# Clean-up so we don't leave anything unaccountable for CVS
echo "rm -rf usr"
rm -rf usr

echo "Don't forget to do:"
echo "cvs ci perf-manpage.tar.bz2"

