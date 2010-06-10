Summary: The Linux RT kernel

# build parallelism:
%{!?_smp_mflags:%define _smp_mflags --jobs=16}

# realtime kernels are named "kernel-rt"
%define kernel kernel
%define realtime rt

# mrgN
%define iteration 22

# rtN
%define rttag rt23

# What parts do we want to build?  We must build at least one kernel.
# These are the kernels that are built IF the architecture allows it.

%define buildrt 0
%define builddoc 0
%define builddebug 1
%define buildheaders 0
%define buildvanilla 0
%define buildtrace 0
%define buildkabi 0
%define buildperf 1

%define _enable_debug_packages 1

# Versions of various parts

%define base_sublevel 33

# all of this is to handle the differences between building
# from a released kernel tarball and a release-candidate (rc)
# tarball
%define released_kernel 1

## If this is a released kernel ##
%if 0%{?released_kernel}
%define upstream_sublevel %{base_sublevel}
# Do we have a 2.6.31.y update to apply?
%define stable_update 5
# Set rpm version accordingly
%if 0%{?stable_update}
%define stablerev .%{stable_update}
%endif
%define rpmversion 2.6.%{base_sublevel}%{?stablerev}

## The not-released-kernel case ##
%else
# The next upstream release sublevel (base_sublevel+1)
%define upstream_sublevel %(expr %{base_sublevel} + 1)
# The rc snapshot level
%define rcrev 8
# Set rpm version accordingly
%define rpmversion 2.6.%{upstream_sublevel}
%endif

# pkg_release is what we'll fill in for the rpm Release field
%if 0%{?released_kernel}
### Old naming convention w/o rtX
### %define pkg_release %{iteration}%{?buildid}%{?dist}
%define pkg_release %{rttag}.%{iteration}%{?buildid}%{?dist}
%else
%if 0%{?rcrev}
%define rctag rc%rcrev
%endif
%if 0%{?gitrev}
%define gittag .git%gitrev
%if !0%{?rcrev}
%define rctag .rc0
%endif
%endif
### old naming convention
### %define pkg_release 0.%{iteration}%{?rctag}%{?gittag}%{?buildid}%{?dist}
### Old naming convention w/o rtX
### %define pkg_release %{?rctag}%{?gittag}.%{iteration}%{?buildid}%{?dist}
%define pkg_release %{?rctag}%{?gittag}.%{rttag}.%{iteration}%{?buildid}%{?dist}
%endif

# The kernel tarball/base version
%define kversion 2.6.%{base_sublevel}

%define signmodules 0
%define make_target bzImage
%define kernel_image x86

%define KVERREL %{PACKAGE_VERSION}-%{PACKAGE_RELEASE}
%define hdrarch %_target_cpu
%define asmarch %_target_cpu

# groups of related archs
%define all_x86 i386 i686
# we differ here b/c of the reloc patches

# Override generic defaults with per-arch defaults

%ifarch noarch
%define builddoc 1
%define buildheaders 0
%define builddebug 0
%define buildperf 0
%define all_arch_configs $RPM_SOURCE_DIR/kernel-%{rpmversion}-*.config
%endif

# Second, per-architecture exclusions (ifarch)

%ifarch ppc64iseries i686 i586
%define buildheaders 0
%endif

%ifarch %{all_x86}
%define asmarch x86
%define all_arch_configs $RPM_SOURCE_DIR/kernel-%{rpmversion}-i?86*.config
%define image_install_path boot
%define signmodules 0
%define hdrarch i386
%endif

%ifarch i686
%define buildrt 1
%define buildtrace 1
%define buildvanilla 1
%endif

%ifarch x86_64
%define asmarch x86
%define buildrt 1
%define buildtrace 1
%define buildvanilla 1
%define all_arch_configs $RPM_SOURCE_DIR/kernel-%{rpmversion}-x86_64*.config
%define image_install_path boot
%define signmodules 0
%endif

%ifarch ppc64 ppc64iseries
%define asmarch powerpc
%define all_arch_configs $RPM_SOURCE_DIR/kernel-%{rpmversion}-ppc64*.config
%define image_install_path boot
%define signmodules 0
%define make_target vmlinux
%define kernel_image vmlinux
%define kernel_image_elf 1
%define hdrarch powerpc
%endif

%ifarch sparc
%define asmarch sparc
%define all_arch_configs $RPM_SOURCE_DIR/kernel-%{rpmversion}-sparc.config
%define make_target image
%define kernel_image image
%endif

%ifarch sparc64
%define asmarch sparc
%define buildsmp 1
%define all_arch_configs $RPM_SOURCE_DIR/kernel-%{rpmversion}-sparc64*.config
%define make_target image
%define kernel_image image
%endif

%ifarch ppc
%define asmarch powerpc
%define all_arch_configs $RPM_SOURCE_DIR/kernel-%{rpmversion}-ppc{-,.}*config
%define image_install_path boot
%define make_target vmlinux
%define kernel_image vmlinux
%define kernel_image_elf 1
%define buildsmp 1
%define hdrarch powerpc
%endif

%if %{buildrt}
%ifarch x86_64
%define all_arch_configs $RPM_SOURCE_DIR/kernel-%{rpmversion}-x86_64-rt*.config
%endif
%ifarch i686
%define all_arch_configs $RPM_SOURCE_DIR/kernel-%{rpmversion}-i?86-rt*.config
%endif
%endif

# To temporarily exclude an architecture from being built, add it to
# %nobuildarches. Do _NOT_ use the ExclusiveArch: line, because if we
# don't build kernel-headers then the new build system will no longer let
# us use the previous build of that package -- it'll just be completely AWOL.
# Which is a BadThing(tm).

# We don't build a kernel on i386 or s390x -- we only do kernel-headers there.
%define nobuildarches i386 s390

%ifarch %nobuildarches
%define buildsmp 0
%define buildpae 0
#%define _enable_debug_packages 0
%endif

#
# Three sets of minimum package version requirements in the form of Conflicts:
# to versions below the minimum
#

#
# First the general kernel 2.6 required versions as per
# Documentation/Changes
#
%define kernel_dot_org_conflicts  ppp < 2.4.3-3, isdn4k-utils < 3.2-32, nfs-utils < 1.0.7-12, e2fsprogs < 1.37-4, util-linux < 2.12, jfsutils < 1.1.7-2, reiserfs-utils < 3.6.19-2, xfsprogs < 2.6.13-4, procps < 3.2.5-6.3, oprofile < 0.9.1-2

#
# Then a series of requirements that are distribution specific, either
# because we add patches for something, or the older versions have
# problems with the newer kernel or lack certain things that make
# integration in the distro harder than needed.
#
%define package_conflicts initscripts < 7.23, udev < 063-6, iptables < 1.3.2-1, ipw2200-firmware < 2.4, selinux-policy-targeted < 1.25.3-14

#
# The ld.so.conf.d file we install uses syntax older ldconfig's don't grok.
#
%define xen_conflicts glibc < 2.3.5-1, xen < 3.0.1

#
# Packages that need to be installed before the kernel is, because the %post
# scripts use them.
#
%define kernel_prereq  fileutils, module-init-tools, initscripts >= 8.11.1-1, mkinitrd >= 4.2.21-1

Name: %{kernel}-%{realtime}
Group: System Environment/Kernel
License: GPLv2
Version: %{rpmversion}
Release: %{pkg_release}
ExclusiveArch: noarch i686 x86_64
ExclusiveOS: Linux
Provides: kernel = %{rpmversion}-%{pkg_release}\
Provides: kernel-%{_target_cpu} = %{rpmversion}-%{pkg_release}%{?1}\
Provides: kernel-rt = %{rpmversion}
Provides: kernel-rt-drm = 4.3.0
Provides: kernel-rt-%{_target_cpu} = %{rpmversion}-%{pkg_release}
Prereq: %{kernel_prereq}
Conflicts: %{kernel_dot_org_conflicts}
Conflicts: %{package_conflicts}

#
# prevent the x86 kernel-rt package from being picked up by yum
# on an x86_64 box (this prevents multilib behavior, yum special-cases
# the "kernel" package-name, but not the kernel-rt package name):
#
%ifarch x86_64
Conflicts: kernel-i686
%endif

# We can't let RPM do the dependencies automatic because it'll then pick up
# a correct but undesirable perl dependency from the module headers which
# isn't required for the kernel proper to function
AutoReq: no
AutoProv: yes


#
# List the packages used during the kernel build
#
BuildPreReq: module-init-tools, patch >= 2.5.4, bash >= 2.03, sh-utils, tar
BuildPreReq: bzip2, findutils, gzip, m4, perl, make >= 3.78, diffutils
%if %{signmodules}
BuildPreReq: gnupg
%endif
BuildRequires: gcc >= 3.4.2, binutils >= 2.12, redhat-rpm-config
%if %{buildheaders}
BuildRequires: unifdef
%endif
BuildRequires: python
BuildConflicts: rhbuildsys(DiskFree) < 500Mb

# Base kernel source
Source0: ftp://ftp.kernel.org/pub/linux/kernel/v2.6/linux-%{kversion}.tar.bz2

%if 0%{?stable_update}
Source1: patch-%{rpmversion}.bz2
%endif

%if 0%{?rcrev}
Source2: patch-%{rpmversion}-%{rctag}.bz2
%endif

# current release candidate

Source3: Makefile.config

Source10: COPYING.modules
Source11: genkey
%if %{buildkabi}
Source12: kabitool
%endif
Source14: find-provides
Source15: merge.pl

Source20: config-debug
Source21: config-generic
Source22: config-i686-PAE
Source23: config-nodebug
Source24: config-rt
Source25: config-trace
Source26: config-vanilla
Source27: config-x86_64-generic
Source28: config-x86-generic
Source30: sanity_check.py
Source31: perf-manpage.tar.bz2

# START OF PATCH DEFINITIONS
%if 0%{?rcrev}
Patch2: patch-%{rpmversion}-%{rctag}-%{rttag}.bz2
%else
Patch2: patch-%{rpmversion}-%{rttag}.bz2
%endif
Patch4: Allocate-RTSJ-memory-for-TCK-conformance-test.patch
Patch5: Add-dev-rmem-device-driver-for-real-time-JVM-testing.patch
Patch6: ibm-rmem-for-rtsj.patch
Patch7: linux-2.6-dynticks-off-by-default.patch
# Patch8: linux-2.6-rt-oomkill.patch
# Patch9: RT-die.patch
# Patch10: pci-nommconf-noseg.patch

### 2.6.29.1-3
Patch11: linux-2.6-panic-on-oops.patch

### 2.6.29.1-4
# Only a config change

### 2.6.29.1-5
# Patch12: RHEL-RT_AMD_TSC_sync_PN.patch
# Patch14: bz465837-rtc-compat-rhel5.patch-ported-to-V2.patch
# Patch15: bz467739-ibm-add-amd64_edac-driver.patch
# Patch16: irq-tracer-fix.patch
# Patch17: forward-port-of-limit-i386-ram-to-16gb.patch
# Patch18: forward-port-of-quiet-plist-warning.patch

### 2.6.29.1-6
# Patch19: bz460217-nagle-tunables.patch
Patch20: ibm-rtl-driver.patch
Patch21: ibm-hs21-tmid-fix.patch
Patch22: ibm-qla-disable-msi-on-rt.patch

### 2.6.29.1-7
# Patch23: aic94xx-inline-fw-rt.patch
# Patch24: qla2xxx-inline-fw-rt.patch
# Patch26: ntp-logarithmic.patch

### 2.6.29.1-8
# Patch28: ibm-amd-edac-rh-v1-2.6.29_forward_port.patch
# Patch30: net-link-workaround-silly-yield.patch

### 2.6.29.1-9
# Rebased to 2.6.29.1-rt8
# Removed redundant patches

### 2.6.29.1-10
# Rebased to 2.6.29.1-rt9

### 2.6.29.1-11
# rebased to 2.6.29.2
# rebased rt to 2.6.29.2-rt10
# Patch30: forward-port-from-bz465862-ib-fix-locking-order.patch
# Patch31: jstultz-ntp.patch

### 2.6.29.1-12
# rebased rt to 2.6.29.2-rt11

### 2.6.29.3-14
# rebased to 2.6.29.3
# rebased rt to 2.6.29.3-rt12

### 2.6.29.3-15
# rebased rt to 2.6.29.3-rt13

### 2.6.29.3-16
# rebased rt to 2.6.29.3-rt14

### 2.6.29.4-17
# rebased rt to 2.6.29.4-rt15
# Patch40: ftrace-function_profiler-bandaid.patch
# Patch41: net-avoid-extra-wakeups-in-wait_for_packet.patch

### 2.6.29.4-18
# rebased rt to 2.6.29.4-rt16

### 2.6.29.4-19
# Patch42: ftrace-fix-profile-race.patch

### 2.6.29.4-20
# Patch43: bz503758-increase-hung_task_timeout_secs-on-rt.patch

### 2.6.29.4-21
# Rebased to 2.6.29.4-rt17
# The following mrg patches are no longer necessary.
# Patch32, Patch33, Patch34, Patch35, Patch36, Patch37, Patch38, Patch39
# Patch40, Patch42

### 2.6.29.4-22
# Rebased to 2.6.29.4-rt18
# The mrg patch smi-detector.patch is replaced by
# the 2.6.29.4-rt18 hwlat_detector-a-system-hardware-latency-detector.patch

### 2.6.29.4-23
# Rebased to 2.6.29.4-rt19

### 2.6.29.5-24
# Rebased to 2.6.29.5-rt20

### 2.6.29.5-25
# Rebased to 2.6.29.5-rt21

### 2.6.29.5-26
# Rebased to 2.6.29.5-rt22
# Added mrg patch increase-max-lockdep-entries-chains.patch
# Patch44: increase-max-lockdep-entries-chains.patch

### 2.6.29.6-27
# Rebased to 2.6.29.6-rt23

### 2.6.29.6-28
# Patch45: hw_lat-simple-solution-001.patch

### 2.6.31-rc4-rt1
# Rebased to 2.6.31-rc4

### 2.6.31-rc5-rt1.1
# Rebased to 2.6.31-rc5

### 2.6.31-rc5-3
# Iteration 3

### 2.6.31-rc5-4
# Rebased to 2.6.31-rc5-rt1.2
# Added these patches that are lined up for 2.6.31-rc5-rt1.3
# Patch46: pre1.3-0002-add-hunks-dropped-in-rt-2.6.31-rc5-rt1.1-forward-por.patch
# Patch47: pre1.3-0003-ARM-5627-1-Fix-restoring-of-lr-at-the-end-of-mcoun.patch
# Patch48: pre1.3-0004-ARM-5613-1-implement-CALLER_ADDRESSx.patch
# Patch49: pre1.3-0005-futex-detect-mismatched-requeue-targets.patch

### 2.6.31-rc5-5
# Patch50: perf-use-cplus_demangle.patch

### 2.6.31-rc6-6
# Rebased to 2.6.31-rc6-rt2
# Remove the pre1.3 patches that either be included or dropped in the above

### 2.6.31-rc6-rt4-7
# new naming style (see above)
# Rebased to 2.6.31-rc6-rt4
# Removed bz503758 which increases hung_task_timeout_secs default on RT
# Reason 1 - We can modify this in /etc/sysctl.conf instead of with a patch
# Reason 2 - We are turning off hung_task_panic
Patch51: firmware.patch

### 2.6.31-rc6-rt5-8
# Rebased to 2.6.31-rc6-rt5

### 2.6.31-rc6-rt5-9
# PATCH52: timer-delay-waking-softirqs-from-the-jiffy-tick.patch

### 2.6.31-rc6-rt5-10
# Specfile changes related to firmware files install

### 2.6.31-rc6-rt6-11
# Rebased to 2.6.31-rc6-rt6
# Dropped timer-delay-waking-softirqs-from-the-jiffy-tick.patch
# because it already integrated in the rt6 version.

### 2.6.31-rc7-rt7-12
# Rebased to 2.6.31-rc7-rt7
Patch53: scsi-fc-transport-removal-of-target-configurable.patch

### 2.6.31-rc7-rt8-mrg13
# Rebased to 2.6.31-rc7-rt8

### 2.6.31-rc8-rt9-mrg14
# Rebased to 2.6.31-rc8-rt9

### 2.6.31-rc9-rt9.1-mrg16
# Rebased to 2.6.31-rc9-rt9.1

### 2.6.31-rt9.2-mrg17
# Rebased to 2.6.31-rt9.2

### 2.6.31-rt10-mrg18
# Rebased to 2.6.31-rt10

### 2.6.31-rt11-mrg19
# Rebased to 2.6.31-rt11

### 2.6.31-rt11-mrg20
# Dropped forward-port-from-bz465862-ib-fix-locking-order.patch
# Dropped forward-port-of-quiet-plist-warning.patch

### 2.6.31-rt13-mrg21
# Patch26: implement_logarithmic_time_accumulation.patch
# Patch54: remove_xtime_cache.patch

### 2.6.31-rt13-mrg22
# PATCH55: bz523604-remove-pulse-code-from-the-bnx2x-driver.patch
# PATCH56: bz517166-amd64_edac_trival_fix.patch
# PATCH57: bz517166-patch-amd-csfix.patch

### 2.6.31.2-rt13-mrg23
# PATCH58: bz528136-ftrace-check-for-failure-for-all-conversions.patch 
# PATCH59: bz528136-tracing-correct-module-boundaries-for-ftrace_releas.patch
# PATCH60: bz524789-Add-support-for-i7core.patch
# PATCH61: bz527421-smi-remediation-simple-fix.patch

### 2.6.31.4-rt14-mrg24
# Rebased to 2.6.31.4-rt14
# Dropped implement_logarithmic_time_accumulation.patch
# Dropped remove_xtime_cache.patch
# Dropped bz528136-ftrace-check-for-failure-for-all-conversions.patch
# Dropped bz528136-tracing-correct-module-boundaries-for-ftrace_releas.patch

### 2.6.31.4-rt14-mrg25
# PATCH62: bz528747-net-Introduce-recvmmsg-socket-syscall.patch

### 2.6.31.4-rt14-mrg28
# PATCH70: bz531589-net-Make-setsockopt-optlen-be-unsigned.patch

### 2.6.31.4-rt14-mrg29
# Updated bz529839-futex-comment-fixups.patch

### 2.6.31.4-rt14-mrg30
# Do not set CONFIG_SCHED_MC (bz527658)
# Do not set CONFIG_SECURITY_DEFAULT_MMAP_MIN_ADDR

### 2.6.33-rt4-mrg3
# Patch23: mm-highmem.c-Fix-pkmap_count-undeclared.patch

### 2.6.33-rt4-mrg4
Patch24: trace-Update-the-comm-field-in-the-right-variable-i.patch

### 2.6.33-rt7-mrg5
# Updated to 2.6.33-rt7

### 2.6.33.1-rt9-mrg8

### 2.6.33.1-rt10-mrg8
# rebasing to 2.6.33.1-rt10

### 2.6.33.1-rt11-mrg9
# rebasing to 2.6.33.1-rt11

### 2.6.33.2-rt13-mrg11
# rebasing to 2.6.33.2-rt13

### 2.6.33.2-rt13-mrg12
Patch85: bz568621-hvc_console-Fix-race-between_close_and_remov.patch

### 2.6.33.2-rt13-mrg13
Patch86: tracing-x86-Add-check-to-detect-GCC-messing-with-mco.patch
Patch87: lockdep-Make-MAX_STACK_TRACE_ENTRIES-configurable.patch

### 2.6.33.4-rt20-mrg17
# rebasing to 2.6.33.4-rt20
Patch88: perf_events-fix-errors-path-in-perf_output_begin.patch

### 2.6.33.4-rt20-mrg18
Patch89: bz585096-KEYS-find_keyring_by_name-can-gain-access-to-a-freed.patch

### 2.6.33.4-rt20-mrg19
Patch92: i7core_edac-Bring-the-i7core_edac-up-to-date-with-li.patch
Patch93: i7core_edac-Always-call-i7core_-ur-dimm_check_mc_ecc.patch
Patch94: i7core_edac-i7core_register_mci-should-not-fall-thro.patch
Patch95: i7core_edac-Add-support-for-Westmere-to-i7core_edac.patch

### 2.6.33.5-rt22-mrg21
### 2.6.33.5-rt23-mrg22
# Rebasing

# END OF PATCH DEFINITIONS

Patch10000: linux-2.6-build-nonintconfig.patch

# empty final patch file to facilitate testing of kernel patches
Patch99999: linux-kernel-test.patch

BuildRoot: %{_tmppath}/%{name}-%{KVERREL}-root

# Override find_provides to use a script that provides "kernel(symbol) = hash".
# Pass path of the RPM temp dir containing kabideps to find-provides script.
%global _use_internal_dependency_generator 0
%define __find_provides %_sourcedir/find-provides %{_tmppath}
%define __find_requires /usr/lib/rpm/redhat/find-requires kernel

%description
The kernel package contains the Linux kernel (vmlinuz), the core of any
Linux operating system.  The kernel handles the basic functions
of the operating system:  memory allocation, process allocation, device
input and output, etc.

%package devel
Summary: Development package for building kernel modules to match the kernel.
Group: System Environment/Kernel
AutoReqProv: no
Provides: kernel-rt-devel-%{_target_cpu} = %{rpmversion}-%{pkg_release}
Prereq: /usr/bin/find

%description devel
This package provides kernel headers and makefiles sufficient to build modules
against the kernel package.


%package doc
Summary: Various documentation bits found in the kernel source.
Group: Documentation

%description doc
This package contains documentation files from the kernel
source. Various bits of information about the Linux kernel and the
device drivers shipped with it are documented in these files.

You'll want to install this package if you need a reference to the
options that can be passed to Linux kernel modules at load time.

%package headers
Summary: Header files for the Linux kernel for use by glibc
Group: Development/System
Obsoletes: glibc-kernheaders
Provides: glibc-kernheaders = 3.0-46

%description headers
Kernel-headers includes the C header files that specify the interface
between the Linux kernel and userspace libraries and programs.  The
header files define structures and constants that are needed for
building most standard programs and are also needed for rebuilding the
glibc package.

%if %{buildperf}
%package -n perf
Summary: Tool to record and inspect hw/sw performance counters data 
Group: Development/System
BuildRequires: binutils-devel, elfutils-libelf-devel, elfutils-libelf

%description -n perf
Performance counters are special hardware registers available on most modern
CPUs. These registers count the number of certain types of hw events: such as
instructions executed, cache-misses suffered, or branches mispredicted -
without slowing down the kernel or applications. These registers can also
trigger interrupts when a threshold number of events have passed - and can thus
be used to profile the code that runs on that CPU. 

Use the perf tool to collect and process performance counters data.
%endif

%package vanilla
Summary: The vanilla upstream kernel the -rt kernel is based on

Group: System Environment/Kernel
Provides: kernel = %{rpmversion}
Provides: kernel-drm = 4.3.0
Provides: kernel-%{_target_cpu} = %{rpmversion}-%{pkg_release}vanilla
Prereq: %{kernel_prereq}
Conflicts: %{kernel_dot_org_conflicts}
Conflicts: %{package_conflicts}
# We can't let RPM do the dependencies automatic because it'll then pick up
# a correct but undesirable perl dependency from the module headers which
# isn't required for the kernel proper to function
AutoReq: no
AutoProv: yes

%description vanilla
This package includes a vanilla version of the Linux kernel. It is
useful for those who dont want a real-time kernel, or who'd like to
quickly check whether a problem seen on -rt is also present in the
vanilla kernel.

%package trace
Summary: The realtime kernel with tracing options turned on

Group: System Environment/Kernel
Provides: kernel = %{rpmversion}
Provides: kernel-drm = 4.3.0
Provides: kernel-%{_target_cpu} = %{rpmversion}-%{pkg_release}trace
Prereq: %{kernel_prereq}
Conflicts: %{kernel_dot_org_conflicts}
Conflicts: %{package_conflicts}
# We can't let RPM do the dependencies automatic because it'll then pick up
# a correct but undesirable perl dependency from the module headers which
# isn't required for the kernel proper to function
AutoReq: no
AutoProv: yes

%description trace
This package includes a version of the realtime Linux kernel with tracing
options compiled turned on and compield in. It is useful in tracking down
latency hot-spots in kernel code.


%package trace-devel
Summary: Development package for building kernel modules to match the tracing kernel.
Group: System Environment/Kernel
AutoReqProv: no
Provides: kernel-rt-trace-devel-%{_target_cpu} = %{rpmversion}-%{pkg_release}
Provides: kernel-rt-trace-devel = %{rpmversion}-%{pkg_release}trace
Prereq: /usr/bin/find

%description trace-devel
This package provides kernel headers and makefiles sufficient to build modules
against the tracing kernel package.


%package vanilla-devel
Summary: Development package for building kernel modules to match the vanilla kernel.
Group: System Environment/Kernel
Provides: kernel-rt-vanilla-devel-%{_target_cpu} = %{rpmversion}-%{pkg_release}
Provides: kernel-rt-vanilla-devel-%{_target_cpu} = %{rpmversion}-%{pkg_release}vanilla
Provides: kernel-rt-vanilla-devel = %{rpmversion}-%{pkg_release}vanilla
AutoReqProv: no
Prereq: /usr/bin/find

%description vanilla-devel
This package provides kernel headers and makefiles sufficient to build modules
against the vanilla kernal package.

%package debug
Summary: A debug realtime kernel and modules
Group: System Environment/Kernel
License: GPLv2
Provides: kernel-rt-debug = %{rpmversion}
Provides: kernel-rt-debug-drm = 4.3.0
Provides: kernel-rt-debug-%{_target_cpu} = %{rpmversion}-%{pkg_release}debug
Prereq: %{kernel_prereq}
Conflicts: %{kernel_dot_org_conflicts}
Conflicts: %{package_conflicts}
AutoReq: no
AutoProv: yes

%description debug
This package contains the realtime kernel and modules compiled with various
tracing and debugging options enabled. It is primarily useful for tracking
down problem discovered with the regular realtime kernel.

%package debug-devel
Summary: Development package for building kernel modules to match the debug realtime kernel.
Group: System Environment/Kernel
Provides: kernel-rt-debug-devel-%{_target_cpu} = %{rpmversion}-%{pkg_release}
Provides: kernel-rt-debug-devel-%{_target_cpu} = %{rpmversion}-%{pkg_release}debug
Provides: kernel-rt-debug-devel = %{rpmversion}-%{pkg_release}debug
AutoReqProv: no
Prereq: /usr/bin/find

%description debug-devel
This package provides kernel headers and makefiles sufficient to build modules
against the debug kernel-rt package.


%prep
patch_command='patch -p1 -F1 -s'
ApplyPatch()
{
local patch=$1
shift
if [ ! -f $RPM_SOURCE_DIR/$patch ]; then
echo "Can't find $RPM_SOURCE_DIR/$patch"
exit 1;
fi
case "$patch" in
*.bz2) bunzip2 < "$RPM_SOURCE_DIR/$patch" | $patch_command ${1+"$@"} ;;
*.gz) gunzip < "$RPM_SOURCE_DIR/$patch" | $patch_command ${1+"$@"} ;;
*) $patch_command ${1+"$@"} < "$RPM_SOURCE_DIR/$patch" ;;
esac
}

# First we unpack the kernel tarball.
# If this isn't the first make prep, we use links to the existing clean tarball
# which speeds things up quite a bit.

# Update to latest upstream.
%if 0%{?released_kernel}
%define vanillaversion 2.6.%{base_sublevel}
# released_kernel with stable_update available case
%if 0%{?stable_update}
%define vanillaversion 2.6.%{base_sublevel}.%{stable_update}
%endif
# non-released_kernel case
%else
%if 0%{?rcrev}
%define vanillaversion 2.6.%{upstream_sublevel}-rc%{rcrev}
%if 0%{?gitrev}
%define vanillaversion 2.6.%{upstream_sublevel}-rc%{rcrev}-git%{gitrev}
%endif
%else
# pre-{base_sublevel+1}-rc1 case
%if 0%{?gitrev}
%define vanillaversion 2.6.%{base_sublevel}-git%{gitrev}
%endif
%endif
%endif

if [ ! -d %{name}-%{rpmversion}-%{pkg_release}/vanilla-%{vanillaversion} ]; then
# Ok, first time we do a make prep.
rm -f pax_global_header
%setup -q -n %{name}-%{rpmversion}-%{pkg_release} -c
mv linux-%{kversion} vanilla-%{vanillaversion}
cd vanilla-%{vanillaversion}

# Update vanilla to the latest upstream.
# released_kernel with stable_update available case
%if 0%{?stable_update}
ApplyPatch patch-2.6.%{base_sublevel}.%{stable_update}.bz2
# non-released_kernel case
%else
%if 0%{?rcrev}
ApplyPatch patch-2.6.%{upstream_sublevel}-rc%{rcrev}.bz2
%if 0%{?gitrev}
ApplyPatch patch-2.6.%{upstream_sublevel}-rc%{rcrev}-git%{gitrev}.bz2
%endif
%else
# pre-{base_sublevel+1}-rc1 case
%if 0%{?gitrev}
ApplyPatch patch-2.6.%{base_sublevel}-git%{gitrev}.bz2
%endif
%endif
%endif

# This patch adds a "make nonint_oldconfig" which is non-interactive and
# also gives a list of missing options at the end. Useful for automated
# builds (as used in the buildsystem).
ApplyPatch linux-2.6-build-nonintconfig.patch

# create a directory to hold the config files
mkdir configs

# The change to the firmware dir must also bepresent on the vanilla kernel
ApplyPatch firmware.patch

# now move back up and get ready to work
cd ..

else
# We already have a vanilla dir.
cd %{name}-%{rpmversion}-%{pkg_release}
if [ -d linux-%{rpmversion}.%{_target_cpu} ]; then
# Just in case we ctrl-c'd a prep already
rm -rf deleteme.%{_target_cpu}
# Move away the stale away, and delete in background.
mv linux-%{rpmversion}.%{_target_cpu} deleteme.%{_target_cpu}
rm -rf deleteme.%{_target_cpu} &
fi
fi

cp -rl vanilla-%{vanillaversion} linux-%{rpmversion}.%{_target_cpu}

cd linux-%{rpmversion}.%{_target_cpu}

cp $RPM_SOURCE_DIR/config-* .
cp %{SOURCE15} .
cp %{SOURCE30} .

# Dynamically generate kernel .config files from config-* files
make -f %{SOURCE3} VERSION=%{rpmversion} configs

# START OF PATCH APPLICATIONS
%if 0%{?rcrev}
ApplyPatch patch-%{rpmversion}-%{rctag}-%{rttag}.bz2
%else
ApplyPatch patch-%{rpmversion}-%{rttag}.bz2
%endif

# ApplyPatch smi-detector.patch
ApplyPatch Allocate-RTSJ-memory-for-TCK-conformance-test.patch
ApplyPatch Add-dev-rmem-device-driver-for-real-time-JVM-testing.patch
ApplyPatch ibm-rmem-for-rtsj.patch
ApplyPatch linux-2.6-dynticks-off-by-default.patch
# ApplyPatch linux-2.6-rt-oomkill.patch
# ApplyPatch RT-die.patch
# ApplyPatch pci-nommconf-noseg.patch

### 2.6.29.1-3
ApplyPatch linux-2.6-panic-on-oops.patch

### 2.6.29-4
# Only a config change

### 2.6.29.1-5
# ApplyPatch RHEL-RT_AMD_TSC_sync_PN.patch
# ApplyPatch bz465837-rtc-compat-rhel5.patch-ported-to-V2.patch
# ApplyPatch bz467739-ibm-add-amd64_edac-driver.patch
# ApplyPatch irq-tracer-fix.patch
# ApplyPatch forward-port-of-limit-i386-ram-to-16gb.patch
# ApplyPatch forward-port-of-quiet-plist-warning.patch

### 2.6.29.1-6
# ApplyPatch bz460217-nagle-tunables.patch
ApplyPatch ibm-rtl-driver.patch
ApplyPatch ibm-hs21-tmid-fix.patch
ApplyPatch ibm-qla-disable-msi-on-rt.patch

### 2.6.29.1-7
# ApplyPatch aic94xx-inline-fw-rt.patch
# ApplyPatch qla2xxx-inline-fw-rt.patch
# ApplyPatch ntp-logarithmic.patch

### 2.6.29.1-8
# ApplyPatch ibm-amd-edac-rh-v1-2.6.29_forward_port.patch
# ApplyPatch net-link-workaround-silly-yield.patch

### 2.6.29.1-9
# Rebased to 2.6.29.1-rt8
# Removed redundant patches

### 2.6.29.1-10
# Rebased to 2.6.29.1-rt9

### 2.6.29.1-11
# rebased to 2.6.29.2
# rebased rt to 2.6.29.2-rt10
# ApplyPatch forward-port-from-bz465862-ib-fix-locking-order.patch
# ApplyPatch jstultz-ntp.patch

### 2.6.29.1-12
# rebased rt to 2.6.29.2-rt11

### 2.6.29.3-14
# rebased to 2.6.29.3
# rebased rt to 2.6.29.3-rt12

### 2.6.29.3-15
# rebased rt to 2.6.29.3-rt13

### 2.6.29.3-16
# rebased rt to 2.6.29.3-rt14

### 2.6.29.4-17
# rebased rt to 2.6.29.4-rt15
# ApplyPatch ftrace-function_profiler-bandaid.patch
# ApplyPatch net-avoid-extra-wakeups-in-wait_for_packet.patch

### 2.6.29.4-18
# rebased rt to 2.6.29.4-rt16

### 2.6.29.4-19
# ApplyPatch ftrace-fix-profile-race.patch

### 2.6.29.4-20
# ApplyPatch bz503758-increase-hung_task_timeout_secs-on-rt.patch

### 2.6.29.4-21
# Rebased to 2.6.29.4-rt17

### 2.6.29.4-22
# Rebased to 2.6.29.4-rt18

### 2.6.29.4-23
# Rebased to 2.6.29.4-rt19

### 2.6.29.5-24
# Rebased to 2.6.29.5-rt20

### 2.6.29.5-25
# Rebased to 2.6.29.5-rt21

### 2.6.29.5-26
# Rebased to 2.6.29.5-rt22
# Added mrg patch increase-max-lockdep-entries-chains.patch
# ApplyPatch increase-max-lockdep-entries-chains.patch

### 2.6.29.6-27
# Rebased to 2.6.29.6-rt23

### 2.6.29.6-28
# ApplyPatch hw_lat-simple-solution-001.patch

### 2.6.31-rc4-rt1
# Rebased to 2.6.31-rc4

### 2.6.31-rc5-rt1.1
# Rebased to 2.6.31-rc5

### 2.6.31-rc5-3
# Iteration 3

### 2.6.31-rc5-4
# Rebased to 2.6.31-rc5-rt1.2
# Added these patches that are lined up for 2.6.31-rc5-rt1.3
# ApplyPatch pre1.3-0002-add-hunks-dropped-in-rt-2.6.31-rc5-rt1.1-forward-por.patch
# ApplyPatch pre1.3-0003-ARM-5627-1-Fix-restoring-of-lr-at-the-end-of-mcoun.patch
# ApplyPatch pre1.3-0004-ARM-5613-1-implement-CALLER_ADDRESSx.patch
# ApplyPatch pre1.3-0005-futex-detect-mismatched-requeue-targets.patch

### 2.6.31-rc5-5
# ApplyPatch perf-use-cplus_demangle.patch

### 2.6.31-rc6-6
# Rebased to 2.6.31-rc6-rt2
# Remove the pre1.3 patches that either be included or dropped in the above

### 2.6.31-rc6-rt4-7
# new naming style (see above)
# Rebased to 2.6.31-rc6-rt4
# Removed bz503758 which increases hung_task_timeout_secs default on RT
# Reason 1 - We can modify this in /etc/sysctl.conf instead of with a patch
# Reason 2 - We are turning off hung_task_panic

## The following patch is now applied in the very beginning of the prep
## stage, also affecting the vanilla tree.
## ApplyPatch firmware.patch

### 2.6.31-rc6-rt5-8
# Rebased to 2.6.31-rc6-rt5

### 2.6.31-rc6-rt5-9
# ApplyPatch timer-delay-waking-softirqs-from-the-jiffy-tick.patch

### 2.6.31-rc6-rt5-10
# Specfile changes related to firmware files install

### 2.6.31-rc6-rt6-11
# Rebased to 2.6.31-rc6-rt6
# Dropped timer-delay-waking-softirqs-from-the-jiffy-tick.patch
# because it already integrated in the rt6 version.

### 2.6.31-rc7-rt7-12
# Rebased to 2.6.31-rc7-rt7
ApplyPatch scsi-fc-transport-removal-of-target-configurable.patch

### 2.6.31-rc7-rt8-mrg13
# Rebased to 2.6.31-rc7-rt8

### 2.6.31-rc8-rt9-mrg14
# Rebased to 2.6.31-rc8-rt9

### 2.6.31-rc9-rt9.1-mrg16
# Rebased to 2.6.31-rc9-rt9.1

### 2.6.31-rt9.2-mrg17
# Rebased to 2.6.31-rt9.2

### 2.6.31-rt10-mrg18
# Rebased to 2.6.31-rt10

### 2.6.31-rt11-mrg19
# Rebased to 2.6.31-rt11

### 2.6.31-rt11-mrg20
# Dropped forward-port-from-bz465862-ib-fix-locking-order.patch
# Dropped forward-port-of-quiet-plist-warning.patch

### 2.6.31-rt13.2-mrg21
# ApplyPatch implement_logarithmic_time_accumulation.patch
# ApplyPatch remove_xtime_cache.patch

### 2.6.31-rt13.2-mrg22
# ApplyPatch bz523604-remove-pulse-code-from-the-bnx2x-driver.patch
# ApplyPatch bz517166-amd64_edac_trival_fix.patch
# ApplyPatch bz517166-patch-amd-csfix.patch

### 2.6.31.2-rt13-mrg23
# ApplyPatch bz528136-ftrace-check-for-failure-for-all-conversions.patch
# ApplyPatch bz528136-tracing-correct-module-boundaries-for-ftrace_releas.patch
# ApplyPatch bz524789-Add-support-for-i7core.patch
# ApplyPatch bz527421-smi-remediation-simple-fix.patch

### 2.6.31.4-rt14-mrg24
# Rebased to 2.6.31.4-rt14
# Dropped implement_logarithmic_time_accumulation.patch
# Dropped remove_xtime_cache.patch
# Dropped bz528136-ftrace-check-for-failure-for-all-conversions.patch
# Dropped bz528136-tracing-correct-module-boundaries-for-ftrace_releas.patch

### 2.6.31.4-rt14-mrg25
# ApplyPatch bz528747-net-Introduce-recvmmsg-socket-syscall.patch

### 2.6.31.4-rt14-mrg28
# ApplyPatch bz531589-net-Make-setsockopt-optlen-be-unsigned.patch

### 2.6.31.4-rt14-mrg29
# Updated bz529839-futex-comment-fixups.patch

### 2.6.31.4-rt14-mrg30
# Do not set CONFIG_SCHED_MC (bz527658)
# Do not set CONFIG_SECURITY_DEFAULT_MMAP_MIN_ADDR

### 2.6.33-rt4-mrg3
# ApplyPatch mm-highmem.c-Fix-pkmap_count-undeclared.patch

### 2.6.33-rt4-mrg4
ApplyPatch trace-Update-the-comm-field-in-the-right-variable-i.patch

### 2.6.33.2-rt13-mrg12
ApplyPatch bz568621-hvc_console-Fix-race-between_close_and_remov.patch

### 2.6.33.2-rt13-mrg13
ApplyPatch tracing-x86-Add-check-to-detect-GCC-messing-with-mco.patch
ApplyPatch lockdep-Make-MAX_STACK_TRACE_ENTRIES-configurable.patch

### 2.6.33.4-rt20-mrg17
ApplyPatch perf_events-fix-errors-path-in-perf_output_begin.patch

### 2.6.33.4-rt20-mrg18
ApplyPatch bz585096-KEYS-find_keyring_by_name-can-gain-access-to-a-freed.patch

### 2.6.33.4-rt20-mrg19
ApplyPatch i7core_edac-Bring-the-i7core_edac-up-to-date-with-li.patch
ApplyPatch i7core_edac-Always-call-i7core_-ur-dimm_check_mc_ecc.patch
ApplyPatch i7core_edac-i7core_register_mci-should-not-fall-thro.patch
ApplyPatch i7core_edac-Add-support-for-Westmere-to-i7core_edac.patch

### 2.6.33.5-rt22-mrg21
### 2.6.33.5.-rt23-mrg22

# END OF PATCH APPLICATIONS

# empty final patch to facilitate testing of kernel patches
ApplyPatch linux-kernel-test.patch

cp %{SOURCE10} Documentation/

# Necessary for BZ459141 (ftrace daemon removal)
chmod +x scripts/recordmcount.pl

# now run oldconfig over all the config files
for i in *.config
do
if [ "$i" != ${i//vanilla/chocolate/} ]; then
isvanilla=true
OLDDIR=`pwd`
else
isvanilla=false
fi
mv $i .config
Arch=`head -1 .config | cut -b 3-`
if [ "$isvanilla" = "true" ]; then
pushd ../vanilla-%{vanillaversion};
mv $OLDDIR/.config .
fi
pwd
make ARCH=$Arch nonint_oldconfig > /dev/null
pwd
echo "# $Arch" > configs/$i
cat .config >> configs/$i
if [ "$isvanilla" = "true" ]; then
popd
fi
done

# make sure the kernel has the sublevel we know it has. This looks weird
# but for -pre and -rc versions we need it since we only want to use
# the higher version when the final kernel is released.
perl -p -i -e "s/^EXTRAVERSION.*/EXTRAVERSION = -prep/" Makefile

# get rid of unwanted files resulting from patch fuzz
cd ..
find . \( -name "*.orig" -o -name "*~" \) -exec rm -f {} \; >/dev/null

###
### build
###
%build
#
# Create gpg keys for signing the modules
#

%if %{signmodules}
gpg --homedir . --batch --gen-key %{SOURCE11}
gpg --homedir . --export --keyring ./kernel.pub Red > extract.pub
make linux-%{rpmversion}.%{_target_cpu}/scripts/bin2c
linux-%{rpmversion}.%{_target_cpu}/scripts/bin2c ksign_def_public_key __initdata < extract.pub > linux-%{rpmversion}.%{_target_cpu}/crypto/signature/key.h
%endif

BuildKernel() {
MakeTarget=$1
KernelImage=$2
Flavour=$3
DoDevel=$4

if [ "vanilla" = "$Flavour" ]; then
pushd ../vanilla-%{vanillaversion}
fi

# Pick the right config file for the kernel we're building
if [ -n "$Flavour" ] ; then
Config=kernel-%{rpmversion}-%{_target_cpu}-%{realtime}$Flavour.config
DevelDir=%{_usrsrc}/kernels/%{KVERREL}-$Flavour-%{_target_cpu}
DevelLink=
else
Config=kernel-%{rpmversion}-%{_target_cpu}-%{realtime}.config
DevelDir=%{_usrsrc}/kernels/%{KVERREL}-%{_target_cpu}
DevelLink=
fi

KernelVer=%{KVERREL}$Flavour
echo BUILDING A KERNEL FOR $Flavour %{_target_cpu}...
echo "KernelVer => $KernelVer"
echo "_smp_mflags => %{_smp_mflags}"

# make sure EXTRAVERSION says what we want it to say
perl -p -i -e "s/^EXTRAVERSION.*/EXTRAVERSION = %{?stablerev}-%{pkg_release}$Flavour/" Makefile

# ensure the sublevel is correct (the upstream sublevel)
perl -p -i -e "s/^SUBLEVEL.*/SUBLEVEL = %{upstream_sublevel}/" Makefile

# and now to start the build process

make -s mrproper
cp configs/$Config .config

Arch=`head -1 .config | cut -b 3-`
echo USING ARCH=$Arch

if [ "$KernelImage" == "x86" ]; then
KernelImage=arch/$Arch/boot/bzImage
fi

make -s ARCH=$Arch nonint_oldconfig > /dev/null
make -s ARCH=$Arch %{?_smp_mflags} $MakeTarget
make -s ARCH=$Arch %{?_smp_mflags} modules || exit 1

# Start installing the results

%if "%{_enable_debug_packages}" == "1"
mkdir -p $RPM_BUILD_ROOT/usr/lib/debug/boot
mkdir -p $RPM_BUILD_ROOT/usr/lib/debug/%{image_install_path}
%endif
mkdir -p $RPM_BUILD_ROOT/%{image_install_path}
install -m 644 .config $RPM_BUILD_ROOT/boot/config-$KernelVer
install -m 644 System.map $RPM_BUILD_ROOT/boot/System.map-$KernelVer
touch $RPM_BUILD_ROOT/boot/initrd-$KernelVer.img
cp $KernelImage $RPM_BUILD_ROOT/%{image_install_path}/vmlinuz-$KernelVer
if [ -f arch/$Arch/boot/zImage.stub ]; then
cp arch/$Arch/boot/zImage.stub $RPM_BUILD_ROOT/%{image_install_path}/zImage.stub-$KernelVer || :
fi

mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer
make ARCH=$Arch INSTALL_MOD_PATH=$RPM_BUILD_ROOT modules_install KERNELRELEASE=$KernelVer

%if %{buildkabi}
# Create the kABI metadata for use in packaging
echo "**** GENERATING kernel ABI metadata ****"
gzip -c9 < Module.symvers > $RPM_BUILD_ROOT/boot/symvers-$KernelVer.gz
chmod 0755 %_sourcedir/kabitool
if [ ! -e $RPM_SOURCE_DIR/kabi_whitelist ]; then
%_sourcedir/kabitool -b $RPM_BUILD_ROOT/$DevelDir -k $KernelVer -l $RPM_BUILD_ROOT/kabi_whitelist
else
cp $RPM_SOURCE_DIR/kabi_whitelist $RPM_BUILD_ROOT/kabi_whitelist
fi
rm -f %{_tmppath}/kernel-$KernelVer-kabideps
%_sourcedir/kabitool -b . -d %{_tmppath}/kernel-$KernelVer-kabideps -k $KernelVer -w $RPM_BUILD_ROOT/kabi_whitelist
%endif

# And save the headers/makefiles etc for building modules against
#
# This all looks scary, but the end result is supposed to be:
# * all arch relevant include/ files
# * all Makefile/Kconfig files
# * all script/ files

if [ "$DoDevel" = "True" ]
then
rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/source
mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
(cd $RPM_BUILD_ROOT/lib/modules/$KernelVer ; ln -s build source)
# dirs for additional modules per module-init-tools, kbuild/modules.txt
mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/extra
mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/updates
mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/weak-updates
# first copy everything
cp --parents `find  -type f -name "Makefile*" -o -name "Kconfig*"` $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
cp Module.symvers $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
%if %{buildkabi}
mv $RPM_BUILD_ROOT/kabi_whitelist $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
cp symsets-$KernelVer.tar.gz $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
%endif
# then drop all but the needed Makefiles/Kconfig files
rm -rf $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/Documentation
rm -rf $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/scripts
rm -rf $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include
cp .config $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
cp -a scripts $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
if [ -d arch/%{_arch}/scripts ]; then
  cp -a arch/%{_arch}/scripts $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/arch/%{_arch} || :
fi
if [ -f arch/%{_arch}/*lds ]; then
  cp -a arch/%{_arch}/*lds $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/arch/%{_arch}/ || :
fi
rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/scripts/*.o
rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/scripts/*/*.o
if [ -d arch/%{asmarch}/include ]; then
    cp -a --parents arch/%{asmarch}/include $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/
fi
mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include
cd include
# The following two commands are the result of an experiment that 
# is not finished, do not simply uncomment
# cp -a ../arch/x86/include/asm $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include/asm-x86
# ln -s asm-x86 asm 
cp -a acpi config keys linux math-emu media mtd net pcmcia rdma rxrpc scsi sound video asm-generic $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include
# cp -a `readlink asm` $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include
# While arch/powerpc/include/asm is still a symlink to the old
# include/asm-ppc{64,} directory, include that in kernel-devel too.
if [ "$Arch" = "powerpc" -a -r ../arch/powerpc/include/asm ]; then
  cp -a `readlink ../arch/powerpc/include/asm` $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include
  mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/arch/$Arch/include
  pushd $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/arch/$Arch/include
  ln -sf ../../../include/asm-ppc* asm
  popd
fi
# Make sure the Makefile and version.h have a matching timestamp so that
# external modules can be built
touch -r $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/Makefile $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include/linux/version.h
touch -r $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/.config $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include/linux/autoconf.h
# Copy .config to include/config/auto.conf so "make prepare" is unnecessary.
cp $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/.config $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include/config/auto.conf
cd ..

#
# save the vmlinux file for kernel debugging into the kernel-debuginfo rpm
#
%if "%{_enable_debug_packages}" == "1"
mkdir -p $RPM_BUILD_ROOT/usr/lib/debug/lib/modules/$KernelVer
cp vmlinux $RPM_BUILD_ROOT/usr/lib/debug/lib/modules/$KernelVer
%endif

find $RPM_BUILD_ROOT/lib/modules/$KernelVer -name "*.ko" -type f >modnames

# gpg sign the modules
%if %{signmodules}
gcc -o scripts/modsign/mod-extract scripts/modsign/mod-extract.c -Wall
KEYFLAGS="--no-default-keyring --homedir .."
KEYFLAGS="$KEYFLAGS --secret-keyring ../kernel.sec"
KEYFLAGS="$KEYFLAGS --keyring ../kernel.pub"
export KEYFLAGS

for i in `cat modnames`
do
  sh ./scripts/modsign/modsign.sh $i Red
  mv -f $i.signed $i
done
unset KEYFLAGS
%endif
# mark modules executable so that strip-to-file can strip them
cat modnames | xargs chmod u+x

# detect missing or incorrect license tags
for i in `cat modnames`
do
  echo -n "$i "
  /sbin/modinfo -l $i >> modinfo
done
cat modinfo |\
  grep -v "^GPL" |
  grep -v "^Dual BSD/GPL" |\
  grep -v "^Dual MPL/GPL" |\
  grep -v "^GPL and additional rights" |\
  grep -v "^GPL v2" && exit 1
rm -f modinfo
rm -f modnames
# remove files that will be auto generated by depmod at rpm -i time
rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/modules.*

# Move the devel headers out of the root file system
mkdir -p $RPM_BUILD_ROOT%{_usrsrc}/kernels
mv $RPM_BUILD_ROOT/lib/modules/$KernelVer/build $RPM_BUILD_ROOT/$DevelDir
ln -sf ../../..$DevelDir $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
[ -z "$DevelLink" ] || ln -sf `basename $DevelDir` $RPM_BUILD_ROOT/$DevelLink
fi
if [ "vanilla" = "$Flavour" ]; then
popd
fi
}

###
# DO it...
###

# prepare directories
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/boot

cd linux-%{rpmversion}.%{_target_cpu}

%if %{buildrt}
BuildKernel %make_target %kernel_image "" True
%endif

%if %{builddebug}
BuildKernel %make_target %kernel_image debug  True
%endif

%if %{buildtrace}
BuildKernel %make_target %kernel_image trace True
%endif

%if %{buildvanilla}
BuildKernel %make_target %kernel_image vanilla True
%endif

# Perf
%if %{buildperf}
# Build the perf binary and doc and install it.
chmod 755 tools/perf/util/generate-cmdlist.sh
make prefix=$RPM_BUILD_ROOT%{_prefix} -C tools/perf install
# make prefix=$RPM_BUILD_ROOT%{_prefix} -C tools/perf install-man
# Perf docs are now created elsewhere and packed in a .tar.gz file
tar xvjf %{SOURCE31} -C $RPM_BUILD_ROOT
%endif

###
### Special hacks for debuginfo subpackages.
###

# This macro is used by %%install, so we must redefine it before that.
%define debug_package %{nil}

%if "%{_enable_debug_packages}" == "1"
%ifnarch noarch
%global __debug_package 1
%package debuginfo-common
Summary: Kernel source files used by %{name}-debuginfo packages
Group: Development/Debug
Provides: %{name}-debuginfo-common-%{_target_cpu} = %{KVERREL}

%description debuginfo-common
This package is required by %{name}-debuginfo subpackages.
It provides the kernel source files common to all builds.

%files debuginfo-common
%defattr(-,root,root)
%{_usrsrc}/debug/%{name}-%{rpmversion}-%{pkg_release}/linux-%{rpmversion}.%{_target_cpu}
%{_usrsrc}/debug/.build-id
%if %{buildvanilla}
%{_usrsrc}/debug/%{name}-%{rpmversion}-%{pkg_release}/vanilla-%{vanillaversion}
%endif
%dir %{_usrsrc}/debug
%dir /usr/lib/debug
%dir /usr/lib/debug/%{image_install_path}
%dir /usr/lib/debug/lib
%dir /usr/lib/debug/lib/modules
%dir /usr/lib/debug%{_usrsrc}/kernels
%endif
%endif

%if %{buildperf}
%package -n perf-debuginfo
Summary: Debug information for kernel-rt-perf
Group: Development/Debug

%description -n perf-debuginfo
Provides the source files and debuginfo for kernel-rt-perf

%files -n perf-debuginfo
%defattr(-,root,root)
/usr/lib/debug%{_bindir}
%endif


###
### install
###

%install

cd linux-%{rpmversion}.%{_target_cpu}

# make the build-id directory (for building on fedora)
mkdir -p $RPM_BUILD_ROOT%{_usrsrc}/debug/.build-id

%if %{builddoc}
mkdir -p $RPM_BUILD_ROOT%{_docdir}/kernel-doc-%{rpmversion}/Documentation

# sometimes non-world-readable files sneak into the kernel source tree
chmod -R a+r *
# copy the source over
tar cf - Documentation | tar xf - -C $RPM_BUILD_ROOT%{_docdir}/kernel-doc-%{rpmversion}
%endif

%if %{buildheaders}
# Install kernel headers
make ARCH=%{hdrarch} INSTALL_HDR_PATH=$RPM_BUILD_ROOT/usr headers_install

# Manually go through the 'headers_check' process for every file, but
# don't die if it fails
chmod +x scripts/hdrcheck.sh
echo -e '*****\n*****\nHEADER EXPORT WARNINGS:\n*****' > hdrwarnings.txt
for FILE in `find $RPM_BUILD_ROOT%{_includedir}` ; do
scripts/hdrcheck.sh $RPM_BUILD_ROOT%{_includedir} $FILE >> hdrwarnings.txt || :
done
echo -e '*****\n*****' >> hdrwarnings.txt
if grep -q exist hdrwarnings.txt; then
sed s:^$RPM_BUILD_ROOT%{_includedir}:: hdrwarnings.txt
# Temporarily cause a build failure if header inconsistencies.
# exit 1
fi

# glibc provides scsi headers for itself, for now
rm -rf $RPM_BUILD_ROOT%{_includedir}/scsi
rm -f $RPM_BUILD_ROOT%{_includedir}/asm*/atomic.h
rm -f $RPM_BUILD_ROOT%{_includedir}/asm*/io.h
rm -f $RPM_BUILD_ROOT%{_includedir}/asm*/irq.h
%endif

###
### clean
###

%clean
rm -rf $RPM_BUILD_ROOT

###
### scripts
###

%post
/sbin/new-kernel-pkg --package kernel-rt --banner "Red Hat Enterprise Linux (realtime)" --mkinitrd --depmod --install %{KVERREL} || exit $?

%post devel
if [ -f /etc/sysconfig/kernel ]
then
. /etc/sysconfig/kernel || exit $?
fi
if [ "$HARDLINK" != "no" -a -x /usr/sbin/hardlink ] ; then
pushd %{_usrsrc}/kernels/%{KVERREL}-%{_target_cpu} > /dev/null
/usr/bin/find . -type f | while read f; do hardlink -c %{_usrsrc}/kernels/*FC*/$f $f ; done
popd > /dev/null
fi

%post vanilla
/sbin/new-kernel-pkg --package kernel-rt-vanilla --mkinitrd --depmod --install %{KVERREL}vanilla || exit $?

%post trace
/sbin/new-kernel-pkg --package kernel-rt-trace --mkinitrd --depmod --install %{KVERREL}trace || exit $?

%post debug
/sbin/new-kernel-pkg --package kernel-rt --banner "Red Hat Enterprise Linux (realtime debug)" --mkinitrd --depmod --install %{KVERREL}debug || exit $?

%preun
/sbin/new-kernel-pkg --rminitrd --rmmoddep --remove %{KVERREL} || exit $?

%preun vanilla
/sbin/new-kernel-pkg --rminitrd --rmmoddep --remove %{KVERREL}vanilla || exit $?

%preun trace
/sbin/new-kernel-pkg --rminitrd --rmmoddep --remove %{KVERREL}trace || exit $?

%preun debug
/sbin/new-kernel-pkg --rminitrd --rmmoddep --remove %{KVERREL}debug || exit $?

###
### file lists
###

# This is %{image_install_path} on an arch where that includes ELF files,
# or empty otherwise.
%define elf_image_install_path %{?kernel_image_elf:%{image_install_path}}

%if %{buildrt}
%if "%{_enable_debug_packages}" == "1"
%ifnarch noarch
%global __debug_package 1
%package debuginfo
Summary: Debug information for package %{name}
Group: Development/Debug
Requires: %{name}-debuginfo-common-%{_target_cpu} = %{KVERREL}
Provides: %{name}-debuginfo-%{_target_cpu} = %{KVERREL}
%description debuginfo
This package provides debug information for package %{name}
This is required to use SystemTap with %{name}-%{KVERREL}.
%files debuginfo
%defattr(-,root,root)
%if "%{elf_image_install_path}" != ""
/usr/lib/debug/%{elf_image_install_path}/*-%{KVERREL}.debug
%endif
/usr/lib/debug/lib/modules/%{KVERREL}
/usr/lib/debug%{_usrsrc}/kernels/%{KVERREL}-%{_target_cpu}
%endif
%endif

%files
%defattr(-,root,root)
/%{image_install_path}/vmlinuz-%{KVERREL}
/boot/System.map-%{KVERREL}
%if %{buildkabi}
/boot/symvers-%{KVERREL}.gz
%endif
/boot/config-%{KVERREL}
%dir /lib/modules/%{KVERREL}
/lib/modules/%{KVERREL}/kernel
/lib/modules/%{KVERREL}/build
/lib/modules/%{KVERREL}/source
/lib/modules/%{KVERREL}/extra
/lib/modules/%{KVERREL}/updates
/lib/modules/%{KVERREL}/weak-updates
/lib/firmware/%{KVERREL}
%ghost /boot/initrd-%{KVERREL}.img


%files devel
%defattr(-,root,root)
%{_usrsrc}/kernels/%{KVERREL}-%{_target_cpu}

%endif # buildrt

%if %{buildheaders}
%files headers
%defattr(-,root,root)
%{_includedir}/*
%endif

%if %{buildperf}
%files -n perf
%defattr(-,root,root)
%{_bindir}/perf
%{_mandir}/man1
%{_libexecdir}/perf-core
%endif

%if %{builddebug}
%files debug
%defattr(-,root,root)
/%{image_install_path}/vmlinuz-%{KVERREL}debug
/boot/System.map-%{KVERREL}debug
%if %{buildkabi}
/boot/symvers-%{KVERREL}debug.gz
%endif
/boot/config-%{KVERREL}debug
%dir /lib/modules/%{KVERREL}debug
/lib/modules/%{KVERREL}debug/kernel
/lib/modules/%{KVERREL}debug/build
/lib/modules/%{KVERREL}debug/source
/lib/modules/%{KVERREL}debug/extra
/lib/modules/%{KVERREL}debug/updates
/lib/modules/%{KVERREL}debug/weak-updates
/lib/firmware/%{KVERREL}debug
%ghost /boot/initrd-%{KVERREL}debug.img

%files debug-devel
%defattr(-,root,root)
%{_usrsrc}/kernels/%{KVERREL}-debug-%{_target_cpu}

%if "%{_enable_debug_packages}" == "1"
%ifnarch noarch
%global __debug_package 1
%package debug-debuginfo
Summary: Debug information for package %{name}-debug
Group: Development/Debug
Requires: %{name}-debuginfo-common-%{_target_cpu} = %{KVERREL}
Provides: %{name}-debug-debuginfo-%{_target_cpu} = %{KVERREL}
%description debug-debuginfo
This package provides debug information for package %{name}-debug
This is required to use SystemTap with %{name}-debug-%{KVERREL}.
%files debug-debuginfo
%defattr(-,root,root)
%if "%{elf_image_install_path}" != ""
/usr/lib/debug/%{elf_image_install_path}/*-%{KVERREL}debug.debug
%endif
/usr/lib/debug/lib/modules/%{KVERREL}debug
/usr/lib/debug%{_usrsrc}/kernels/%{KVERREL}-debug-%{_target_cpu}
%endif
%endif
%endif # builddebug

%if %{buildvanilla}
%if "%{_enable_debug_packages}" == "1"
%ifnarch noarch
%global __debug_package 1
%package vanilla-debuginfo
Summary: Debug information for package %{name}-vanilla
Group: Development/Debug
Requires: %{name}-debuginfo-common-%{_target_cpu} = %{KVERREL}
Provides: %{name}-vanilla-debuginfo-%{_target_cpu} = %{KVERREL}
%description vanilla-debuginfo
This package provides debug information for package %{name}-vanilla
This is required to use SystemTap with %{name}-vanilla-%{KVERREL}.
%files vanilla-debuginfo
%defattr(-,root,root)
%if "%{elf_image_install_path}" != ""
/usr/lib/debug/%{image_install_path}/*-%{KVERREL}vanilla.debug
%endif
/usr/lib/debug/lib/modules/%{KVERREL}vanilla
/usr/lib/debug%{_usrsrc}/kernels/%{KVERREL}-vanilla-%{_target_cpu}
%endif
%endif

%files vanilla
%defattr(-,root,root)
/%{image_install_path}/vmlinuz-%{KVERREL}vanilla
/boot/System.map-%{KVERREL}vanilla
%if %{buildkabi}
/boot/symvers-%{KVERREL}vanilla.gz
%endif
/boot/config-%{KVERREL}vanilla
%dir /lib/modules/%{KVERREL}vanilla
/lib/modules/%{KVERREL}vanilla/kernel
/lib/modules/%{KVERREL}vanilla/build
/lib/modules/%{KVERREL}vanilla/source
/lib/modules/%{KVERREL}vanilla/extra
/lib/modules/%{KVERREL}vanilla/updates
/lib/modules/%{KVERREL}vanilla/weak-updates
/lib/firmware/%{KVERREL}vanilla
%ghost /boot/initrd-%{KVERREL}vanilla.img

%files vanilla-devel
%defattr(-,root,root)
%{_usrsrc}/kernels/%{KVERREL}-vanilla-%{_target_cpu}
%endif

%if %{buildtrace}
%if "%{_enable_debug_packages}" == "1"
%ifnarch noarch
%global __debug_package 1
%package trace-debuginfo
Summary: Debug information for package %{name}-trace
Group: Development/Debug
Requires: %{name}-debuginfo-common-%{_target_cpu} = %{KVERREL}
Provides: %{name}-trace-debuginfo-%{_target_cpu} = %{KVERREL}
%description trace-debuginfo
This package provides debug information for package %{name}-trace
This is required to use SystemTap with %{name}-trace-%{KVERREL}.
%files trace-debuginfo
%defattr(-,root,root)
%if "%{elf_image_install_path}" != ""
/usr/lib/debug/%{elf_image_install_path}/*-%{KVERREL}trace.debug
%endif
/usr/lib/debug/lib/modules/%{KVERREL}trace
/usr/lib/debug%{_usrsrc}/kernels/%{KVERREL}-trace-%{_target_cpu}
%endif
%endif

%files trace
%defattr(-,root,root)
/%{image_install_path}/vmlinuz-%{KVERREL}trace
/boot/System.map-%{KVERREL}trace
%if %{buildkabi}
/boot/symvers-%{KVERREL}trace.gz
%endif
/boot/config-%{KVERREL}trace
%dir /lib/modules/%{KVERREL}trace
/lib/modules/%{KVERREL}trace/kernel
/lib/modules/%{KVERREL}trace/build
/lib/modules/%{KVERREL}trace/source
/lib/modules/%{KVERREL}trace/extra
/lib/modules/%{KVERREL}trace/updates
/lib/modules/%{KVERREL}trace/weak-updates
/lib/firmware/%{KVERREL}trace
%ghost /boot/initrd-%{KVERREL}trace.img

%files trace-devel
%defattr(-,root,root)
%{_usrsrc}/kernels/%{KVERREL}-trace-%{_target_cpu}
%endif

# only some architecture builds need kernel-doc

%if %{builddoc}
%files doc
%defattr(-,root,root)
%{_datadir}/doc/kernel-doc-%{rpmversion}/Documentation/*
%dir %{_datadir}/doc/kernel-doc-%{rpmversion}/Documentation
%dir %{_datadir}/doc/kernel-doc-%{rpmversion}
%endif

%changelog
* Thu Jun 10 2010 John Kacur <jkacur@redhat.com> - 2.6.33.5-rt23-mrg22
- Rebasing to 2.6.33.5-rt23

* Mon Jun 7 2010 John Kacur <jkacur@redhat.com> - 2.6.33.5-rt22-mrg21
- Replaced nfs4-Prevent-deadlock.patch with Avoid_NFS_igrab_deadlock.patch
- Added Fix_d_genocide-from_decrementing_d_count_more_than_once.patch
- Added Fix_select_parent_dentry_traversal_locking.patch

* Tue Jun 1 2010 John Kacur <jkacur@redhat.com> - 2.6.33.5-rt22-mrg20
- Rebasing to 2.6.33.5-rt22
- Removed net-ehea-make-rx-irq-handler-non-threaded-IRQF_NODEL.patch
- Removed fs-namespace-Fix-fuse-mount-fallout.patch

* Wed May 26 2010 John Kacur <jkacur@redhat.com> - 2.6.33.4-rt20-mrg19
- For the following four i7core_edac patches, see bz582574
- Added i7core_edac-Bring-the-i7core_edac-up-to-date-with-li.patch
- Added i7core_edac-Always-call-i7core_-ur-dimm_check_mc_ecc.patch
- Added i7core_edac-i7core_register_mci-should-not-fall-thro.patch
- Added i7core_edac-Add-support-for-Westmere-to-i7core_edac.patch
- Added nfs4-Prevent-deadlock.patch - see bz596111

* Thu May 20 2010 John Kacur <jkacur@redhat.com> - 2.6.33.4-rt20-mrg18
- Added bz585096-KEYS-find_keyring_by_name-can-gain-access-to-a-freed.patch
- Added net-ehea-make-rx-irq-handler-non-threaded-IRQF_NODEL.patch
- Added fs-namespace-Fix-fuse-mount-fallout.patch

* Tue May 18 2010 John Kacur <jkacur@redhat.com> - 2.6.33.4-rt20-mrg17
- Rebasing to 2.6.33.4-rt20
- Removed fs-Add-missing-parantheses.patch
- Removed autofs-deadlock-fixes.patch
- Removed fs-Fix-mnt_count-typo.patch
- Removed fs-Resolve-mntput_no_expire-issues.patch
- Added perf_events-fix-errors-path-in-perf_output_begin.patch

* Fri May 14 2010 John Kacur <jkacur@redhat.com> - 2.6.33.3-rt19-mrg16
- Added fs-Fix-mnt_count-typo.patch
- Added fs-Resolve-mntput_no_expire-issues.patch

* Tue May 11 2010 John Kacur <jkacur@redhat.com> - 2.6.33.3-rt19-mrg15
- Two fixes from John Stultz for autofs deadlocks

* Thu May 6 2010 John Kacur <jkacur@redhat.com> - 2.6.33.3-rt19-mrg14
- Rebased to 2.6.33.3-rt19
- Started clean-up of the spec file

* Fri Apr 16 2010 John Kacur <jkacur@redhat.com> - 2.6.33.2-rt13-mrg13
- Added tracing-x86-Add-check-to-detect-GCC-messing-with-mco.patch
-	Fixes the problem where the function_graph is not available on 32-bit
- Added lockdep-Make-MAX_STACK_TRACE_ENTRIES-configurable.patch
- Made the default MAX_STACK_TRACE_ENTRIES to 393216 for config-debug

* Fri Apr 9 2010 John Kacur <jkacur@redhat.com> - 2.6.33.2-rt13-mrg12
- Added bz568621-hvc_console-Fix-race-between_close_and_remov.patch

* Wed Apr 7 2010 John Kacur <jkacur@redhat.com> - 2.6.33.2-rt13-mrg11
- Rebased to 2.6.33.2-rt13

* Tue Mar 30 2010 John Kacur <jkacur@redhat.com> - 2.6.33.1-rt11-mrg10
- Added Fix-CONFIG_STACK_TRACER-warning.patch 
- Turned CONFIG_STACK_TRACER back on in the production build
- Added x86-32-clean-up-rwsem-inline-asm-statements.patch
- Added x86-clean-up-rwsem-type-system.patch
- Added x86-64-rwsem-64-bit-xadd-rwsem-implementation.patch
- Added x86-Fix-breakage-of-UML-from-the-changes-in-the-rwse.patch
- Added x86-64-support-native-xadd-rwsem-implementation.patch
- Added x86-64-rwsem-Avoid-store-forwarding-hazard-in-__down.patch
- Added sched-sched_getaffinity-Allow-less-than-NR_CPUS-leng.patch

* Mon Mar 22 2010 John Kacur <jkacur@redhat.com> 2.6.33.1-rt11-mrg9
- rebasing to 2.6.33.1-rt11
- Changing the production config to CONFIG_STACK_TRACER is not set

* Thu Mar 18 2010 John Kacur <jkacur@redhat.com> 2.6.33.1-rt10-mrg8
- rebasing to 2.6.33.1-rt10

* Wed Mar 17 2010 John Kacur <jkacur@redhat.com> - 2.6.33.1-rt9-mrg8
- Rebasing to 2.6.33.1-rt9
- disable the lockbreak-in-load-balancer-for-RT.patch as it is already there

* Tue Mar 16 2010 John Kacur <jkacur@redhat.com> - 2.6.33.1-rt7-mrg7
- Reenabling the IBM patches for raw memory eg:
	- Add-dev-rmem-device-driver-for-real-time-JVM-testing.patch
	- ibm-rmem-for-rtsj.patch
- Added stable patch 2.6.33.1
- added CONFIG_FTRACE=y to config-generic

* Tue Mar 16 2010 John Kacur <jkacur@redhat.com> - 2.6.33-rt7-mrg6
- Added the 0001-lockbreak-in-load-balancer-for-RT.patch

* Fri Mar 12 2010 John Kacur <jkacur@redhat.com> - 2.6.33-rt7-mrg5
- rebased to 2.6.33-rt7
- Due to the rebase, we can drop mm-highmem.c-Fix-pkmap_count-undeclared.patch

* Tue Mar 9 2010 John Kacur <jkacur@redhat.com> - 2.6.33-rt4-mrg4
- disabled forward-port-of-limit-i386-ram-to-16gb.patch
-     the only part of the above patch left was rejected as unnecessary on lkml
- Added trace-Update-the-comm-field-in-the-right-variable-i.patch

* Wed Mar 3 2010 John Kacur <jkacur@redhat.com> - 2.6.33-rt4-mrg3
- Rebased to 2.6.33-rt4
- Added mm-highmem.c-Fix-pkmap_count-undeclared.patch

* Thu Feb 25 2010 John Kacur <jkacur@redhat.com> - 2.6.33-rc8-rt2-mrg2
- Rebased to 2.6.33-rc8-rt2
- Fixed config options, where options are no longer available as modules

* Wed Feb 24 2010 John Kacur <jkacur@redhat.com> - 2.6.33-rc8-rt1-mrg1
- Respinning to 2.6.33-rc8-rt1-mrg1
- Removed many patches, most because they are already in 2.6.33-rc8,
	- some because they need review, and may no-longer apply.

* Thu Dec 3 2009 John Kacur <jkacur@redhat.com> - 2.6.31.6-rt19-mrg32
- Fixed the configuration files to properly disable the following
	CONFIG_DETECT_SOFTLOCKUP is not set
	CONFIG_DETECT_HUNG_TASK is not set
	CONFIG_BOOTPARAM_HUNG_TASK_PANIC is not set
	CONFIG_TIMER_STATS is not set

* Wed Nov 4 2009 John Kacur <jkacur@redhat.com> - 2.6.31.4-rt14-mrg30
- bz527658 do not set CONFIG_SCHED_MC
- do not set CONFIG_SECURITY_DEFAULT_MMAP_MIN_ADDR=0 (we want the default 4096)
- (note the above change is cosmetic, since putting it to 0 had not effect)

* Tue Nov 3 2009 John Kacur <jkacur@redhat.com> - 2.6.31.4-rt14-mrg29
- Updated Updates-from-Jon-Masters-for-hwlat_detector.c.patch
- Updated bz529839-futex-comment-fixups.patch

* Thu Oct 29 2009 John Kacur <jkacur@redhat.com> - 2.6.31.4-rt14-mrg28
- Added bz531589-net-Make-setsockopt-optlen-be-unsigned.patch
- Added bz531595-futex-Fix-spurious-wakeup-for-requeue_pi-really.patch
- Added bz531611-netlink-fix-typo-in-initialization.patch
- Added bz531630-drm-r128-Add-test-for-initialisation-to-all-ioctls.patch
- Added bz531633-AF_UNIX-Fix-deadlock-on-connecting-to-shutdown-sock.patch
- Added bz531656-fs-pipe.c-null-pointer-dereference.patch
- Added bz531665-KEYS-get_instantiation_keyring-should-inc-the-key.patch

* Thu Oct 22 2009 John Kacur <jkacur@redhat.com> - 2.6.31.4-rt14-mrg27
- Added bz529832-futex-Detect-mismatched-requeue-targets.patch
- Added bz529839-futex-comment-fixups.patch
- Added bz529855-futex-Move-drop_futex_key_refs-out-of-spinlock.patch
- Added bz529856-futex-Check-for-NULL-keys-in-match_futex.patch
- Added bz527658-Sched_load_balance.patch
- Added Updates-from-Jon-Masters-for-hwlat_detector.c.patch
- Edited the config files to add CONFIG_EDAC_AMD64=m

* Fri Oct 16 2009 John Kacur <jkacur@redhat.com> - 2.6.31.4-rt14-mrg26
- Added bz529365-preempt_disable_rt-should-be-paired-with-preempt_rt.patch

* Wed Oct 14 2009 John Kacur <jkacur@redhat.com> - 2.6.31.4-rt14-mrg25
- Added bz528747-net-Introduce-recvmmsg-socket-syscall.patch

* Wed Oct 14 2009 John Kacur <jkacur@redhat.com> - 2.6.31.4-rt14-mrg24
- Rebased to 2.6.31.4-rt14
- The following patches were dropped because they are included in 2.6.31.4-rt14
- 	Dropped implement_logarithmic_time_accumulation.patch
- 	Dropped remove_xtime_cache.patch
-	bz528136-ftrace-check-for-failure-for-all-conversions.patch
-	bz528136-tracing-correct-module-boundaries-for-ftrace_releas.patch

* Fri Oct 9 2009 John Kacur <jkacur@redhat.com> - 2.6.31.2-rt13-mrg23
- Added bz528136-ftrace-check-for-failure-for-all-conversions.patch
- Added bz528136-tracing-correct-module-boundaries-for-ftrace_releas.patch
- Added bz524789-Add-support-for-i7core.patch
- For bz526232, Added CONFIG_EDAC_MCE=y, CONFIG_EDAC_I7CORE=m to config-generic
- Added bz527421-smi-remediation-simple-fix.patch

* Thu Oct 8 2009 John Kacur <jkacur@redhat.com> - 2.6.31.2-rt13-mrg22
- Added bz523604-remove-pulse-code-from-the-bnx2x-driver.patch
- For bz526232, disabled DECNET, IPX, DEV_APPLETALK, ATALK, NCP_FS, CODA
- For bz526232, enabled CONFIG_X86_GENERICARCH
- Added bz517166-amd64_edac_trival_fix.patch
- Added bz517166-patch-amd-csfix.patch

* Tue Oct 6 2009 John Kacur <jkacur@redhat.com> - 2.6.31.2-rt13-mrg21
- implement_logarithmic_time_accumulation.patch replaces ntp-logarithmic.patch
- Added remove_xtime_cache.patch

* Tue Sep 29 2009 John Kacur <jkacur@redhat.com> - 2.6.31-rt11-mrg20 
- Dropped forward-port-from-bz465862-ib-fix-locking-order.patch
- Dropped forward-port-of-quiet-plist-warning.patch
- disabled CONFIG_DETECT_HUNG_TASK, CONFIG_BOOTPARAM_HUNG_TASK_PANIC,
- CONFIG_DETECT_SOFTLOCKUP, CONFIG_TIMER_STATS in production kernel

* Sat Sep 19 2009 John Kacur <jkacur@redhat.com> - 2.6.31-rt11-mrg19
- Rebased to 2.6.31-rt11

* Tue Sep 15 2009 John Kacur <jkacur@redhat.com> - 2.6.31-rt10-mrg18
- Rebased to 2.6.31-rt10

* Thu Sep 10 2009 John Kacur <jkacur@redhat.com> - 2.6.31-rt9.2-mrg17
- Rebased to 2.6.31-rt9.2

* Wed Sep 9 2009 John Kacur <jkacur@redhat.com> - 2.6.31-rc9-rt9.1-mrg16
- Rebased to 2.6.31-rc9-rt9.1

* Mon Aug 31 2009 Clark Williams <williams@redhat.com> - 2.6.31-rc8-rt9-mrg15
- turned off CONFIG_CALGARY_IOMMU_ENABLED_BY_DEFAULT per IBM request

* Fri Aug 28 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.31-rc8-rt9-mrg14
- Rebased to 2.6.31-rc8-rt9

* Wed Aug 26 2009 Luis Claudio R. Gonçalves <lgoncalv@redhat.com> - 2.6.31-rc7-rt8-mrg13
- Rebased to 2.6.31-rc7-rt8

* Tue Aug 25 2009 <jkacur@redhat.com> - 2.6.31-rc7-rt7-12
- Rebased to 2.6.31-rc7-rt7
- Ported jvrao's scsi-fc-transport-removal-of-target-configurable.patch
- via lclaudio's MRGV1 version to MRGV2 (bz514541)

* Mon Aug 24 2009 <jkacur@redhat.com> - 2.6.31-rc6-rt6-11
- Rebased to 2.6.31-rc6-rt6
- Dropped timer-delay-waking-softirqs-from-the-jiffy-tick.patch
- because it already integrated in the rt6 version.

* Sun Aug 23 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.31-rc6-rt5-10
- Fixed the firmware file versioning on all kernel flavors

* Fri Aug 20 2009 John Kacur <jkacur@redhat.com> - 2.6.31-rc6-rt5-9
- This patch solves the cpu_load balancing problem seen on recent kernels

* Thu Aug 20 2009 John Kacur <jkacur@redhat.com> - 2.6.31-rc6-rt5-8
- Rebased to 2.6.31-rc6-rt5
- Trying again to enable buildperf

* Wed Aug 19 2009 John Kacur <jkacur@redhat.com> - 2.6.31-rc6-rt4-7
- changed the naming style - see above
- Rebased to 2.6.31-rc6-rt4-7
- Removed bz503758 which increases hung_task_timeout_secs default on RT
-  Reason 1 - We can modify this in /etc/sysctl.conf instead of with a patch
-  Reason 2 - We are turning off hung_task_panic
- Added firmware.patch to change the location of the firmware for rt

* Mon Aug 17 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.31-rc6.7
- renamed perf subpackages to perf and perf-debuginfo

* Mon Aug 17 2009 John Kacur <jkacur@redhat.com> - 2.6.31-rc6.6
- Rebased to 2.6.31-rc6-rt2
- Re-enabled perf subpackage creation (Luis Goncalves)
 
* Fri Aug 14 2009 John Kacur
- Rebased to 2.6.31-rc5-rt1.2
- Added the following patches that are lined up for 2.6.31-rc5-rt1.3
- pre1.3-0002-add-hunks-dropped-in-rt-2.6.31-rc5-rt1.1-forward-por.patch
- pre1.3-0003-ARM-5627-1-Fix-restoring-of-lr-at-the-end-of-mcoun.patch
- pre1.3-0004-ARM-5613-1-implement-CALLER_ADDRESSx.patch
- pre1.3-0005-futex-detect-mismatched-requeue-targets.patch

* Tue Aug 11 2009 John Kacur
- disabling buildperf for now.
- reverting to iteration 3 (since this was somehow missed)

* Tue Aug 11 2009 John Kacur
- Two changes to linux-2.6-rt-oomkill.patch
- move oom_kill_enabled to the right proc subdirectory/table
- Change deprecated SPIN_LOCK_UNLOCKED to DEFINE_SPINLOCK
* Mon Aug 10 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.31-rc5-3
- Created the subpackage kernel-rt-perf (performance counters tool/doc)

* Thu Aug 6 2009 John Kacur <jkacur@redhat.com> - 2.6.31-rc5-2
- Rebased to 2.6.31-rc5-rt1.1
- Dropped hw_lat-simple-solution-001.patch from the mrg patches
- because it is already included in the 2.6.31-rc5-rt1.1
- Dropped ibm-amd-edac-rh-v1-2.6.29_forward_port.patch as it is obsolete

* Mon Aug 3 2009 John Kacur <jkacur@redhat.com> - 2.6.31-rc4-1
- Rebased to 2.6.31-rc4-rt1
- In other words, 2.6.30 + rc4 + rt1
- DROPPED bz467739-ibm-add-amd64_edac-driver.patch
- 	Much of the patch is integrated in 2.6.31-rc4, but need confirmation
- DROPPED irq-tracer-fix.patch - also needs review
- DROPPED jstultz-ntp.patch - already included in 2.6.31-rc4
- DROPPED net-avoid-extra-wakeups-in-wait_for_packet.patch
-	already included in 2.6.31-rc4
- DROPPED increase-max-lockdep-entries-chains.patch
-	already included in 2.6.31-rc4
- Modified the following patches for 2.6.31-rc4
- linux-2.6-rt-oomkill.patch
- RT-die.patch
- pci-nommconf-noseg.patch
- RHEL-RT_AMD_TSC_sync_PN.patch
- forward-port-of-quiet-plist-warning.patch
- ibm-qla-disable-msi-on-rt.patch
- ibm-amd-edac-rh-v1-2.6.29_forward_port.patch
- stacktrace-dump_out_last_preempt_disable_locations.patch

* Thu Jul 17 2009 John Kacur <jkacur@redhat.com> - 2.6.29.6-28
- [trace] hwlat: never call wake_up() inside of stop_machine()

* Thu Jul 9 2009 John Kacur <jkacur@redhat.com> - 2.6.29.6-27
- rebased to 2.6.29.6-rt23

* Fri Jun 26 2009 John Kacur <jkacur@redhat.com> - 2.6.29.5-26
- rebased to 2.6.29.5-rt22
- Added patch increase-max-lockdep-entries-chains.patch

* Wed Jun 17 2009 John Kacur <jkacur@redhat.com> - 2.6.29.5-25
- Rebased to 2.6.29.5-rt21

* Tue Jun 16 2009 John Kacur <jkacur@redhat.com> - 2.6.29.5-24
- Rebased to 2.6.29.5-rt20

* Mon Jun 15 2009 John Kacur <jkacur@redhat.com> - 2.6.29.4-23
- Rebased to 2.6.29.4-rt19

* Sat Jun 13 2009 John Kacur <jkacur@redhat.com> - 2.6.29.4-22
- Rebased to 2.6.29.4-rt18
- The mrg smi-detector.patch is replaced by the following patch
- hwlat_detector-a-system-hardware-latency-detector.patch

* Thu Jun 11 2009 John Kacur <jkacur@redhat.com> - 2.6.29.4-21
- Rebased to 2.6.29.4-rt17
- The following mrg patches are no longer necessary.
- Patch32, Patch33, Patch34, Patch35, Patch36, Patch37, Patch38, Patch39
- Patch40, Patch42

* Tue Jun 02 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.29.4-20
- [sched] Increase hung_task_timeout_secs defaul on RT [503758]

* Mon Jun 01 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.29.4-19
- [ftrace] fix function profiler race (Steven Rostedt) [500156]

* Mon May 25 2009 John Kacur <jkacur@redhat.com> - 2.6.29.4-18
- Rebased to 2.6.29.4-rt16

* Fri May 22 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.29.4-17
- Rebased to 2.6.29.4
- Rebased rt to 2.6.29.4-rt15
- Enhanced the stability of the function profiler
- Avoid extra wakeups of threads blocked in wait_for_packet()

* Mon May 18 2009 John Kacur <jkacur@redhat.com> - 2.6.29.3-16
- rebased rt to 2.6.29.3-rt14

* Wed May 14 2009 John Kacur <jkacur@redhat.com> - 2.6.29.3-15
- rebased rt to 2.6.29.3-rt13

* Tue May 12 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.29.3-14
- rebased to 2.6.29.3
- rebased rt to 2.6.29.3-rt12

* Tue May 05 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.29.2-13
- added the ftrace function profiler patches

* Mon May 04 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.29.2-12
- rebased RT to 2.6.29.2-rt11

* Wed Apr 29 2009 Clark Williams <williams@redhat.com> - 2.6.29.2-11
- rebased to upstream stable 2.6.29.2 patch
- rebased RT to 2.6.29.2-rt10
- turned on modules CONFIG_KVM, CONFIG_KVM_INTEL and CONFIG_KVM_AMD
  in config-x86-generic and config-x86_64-generic
- added jstultz patch to change NTP parameter SHIFT_PLL from 4 to 2 
  to improve client convergence times 


* Mon Apr 27 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.29.1-10
- Rebased to PREEMPT_RT patch-2.6.29.1-rt9

* Mon Apr 20 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.29.1-9
- Rebased to 2.6.29.1-rt8
- Removed redundant patches

* Fri Apr 17 2009 Clark Williams <williams@redhat.com> - 2.6.29.1-8
- Disabled CONFIG_DRM_I915_KMS again.
- Disabled CONFIG_GROUP_SCHED 
- added patch to fix lockdep selftest
- added ibm-amd-edac driver
- increased stack trace entries
- sched_yield() workaround

* Wed Apr 15 2009 Clark Williams <williams@redhat.com> - 2.6.29.1-7
- added patch for bundling f/w with AIC94xx driver
- added patch for bundling f/w with QLA2xxx driver
- added patch to implement rt_downgrade_write (rostedt)
- added logarithmic timekeeping accumulation patch from jstultz
- [rpm] added specfile logic to copy arch includes for devel package [495808]

* Mon Apr 13 2009 Clark Williams <williams@redhat.com> - 2.6.29.1-6
- rebased RT to 2.6.29.1-rt7 (lgoncalv)
- added Nagle algorthim tunable from Chris Snook (BZ460217)
- added IBM's rtl driver (for SMI remediation)
- modified specfile to copy stap trace headers (BZ495560)
- added forward ported HS21 tmid patch
- added jvrao's QLA2XXX MSI workaround patch from LKML

* Thu Apr  9 2009 Clark Williams <williams@redhat.com> - 2.6.29.1-5
- rebased RT to 2.6.29.1-rt6
- ported RHEL-RT_AMD_TSC_sync_PN.patch from previous MRG RT
- ported bz465745-rtc-fix_kernel_panic_on_second_use_of_SIGIO_nofitication.patch from previous MRG RT
- ported bz465837-rtc-compat-rhel5.patch-ported-to-V2.patch from previous MRG RT
- ported bz467739-ibm-add-amd64_edac-driver.patch from previous MRG RT
- ported forward-port-of-limit-i386-ram-to-16gb.patch from previous MRG RT
- ported forward-port-of-quiet-plist-warning.patch from previous MRG RT
- ported irq-tracer-fix.patch from previous MRG RT
- Disabled CONFIG_DRM_I915_KMS.
- added config sanity check script
- reworked configs to remove duplicate definitions
- added configs to satisfy new 2.6.29.1 requirements

* Mon Apr  6 2009 Clark Williams <williams@redhat.com> - 2.6.29.1-4
- enabled CONFIG_DYNAMIC_FTRACE for production kernels

* Fri Apr  3 2009 Clark Williams <williams@redhat.com> - 2.6.29-3
- rebased to stable 2.6.29.1
- rebased RT to 2.6.29.1-rt4
- added IBM RTSJ patches
- added RHEL OOM patches
- BZ 384881 patch forward port
- added dynticks-off-by-default patch
- turned off *_GROUP_SCHED in config-generic
- added compression configs in config-generic

* Thu Mar 26 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.29-2
- Redefined config options upon suggestions from the MRG-RT team

* Wed Mar 25 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.29-1
- Rebased to 2.6.29-rt1

* Tue Mar 24 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.29-rc8-2
- Rebased to 2.6.29-rc8-rt4

* Fri Mar 20 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.29-rc8-1
- Rebased to 2.6.29-rc8-rt1

* Mon Mar 16 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.29-rc7-1
- Rebased to 2.6.29-rc7-rt1

* Mon Mar 09 2009 Luis Claudio R. Goncalves <lgoncalv@redhat.com> - 2.6.29-rc6-1
- Added kernel 2.6.29-rc6
- Added PREEMPT_RT patch-2.6.29-rc6-rt3.patch
- Adjusted the spec file
