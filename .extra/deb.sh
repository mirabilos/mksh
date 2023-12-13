set -e
dist=$1
libc=${2:-glibc}
LC_ALL=C; export LC_ALL
Grv=0
trybuild() {
	where=$1; shift
	cd builddir/$where
	echo "I: Attempting compilation of mksh in $where with CC='$CC'"
	echo "N: CFLAGS='$CFLAGS'"
	echo "N: CPPFLAGS='$CPPFLAGS'"
	echo "N: LDFLAGS='$LDFLAGS'"
	echo "N: LDSTATIC='$LDSTATIC' LIBS='$LIBS'"

	arg=-r
	if test x"$where" = x"legacy"; then
		arg="$arg -L"
		tfn=lksh
	else
		tfn=mksh
	fi
	gotbin=0
	env CC="$CC" CFLAGS="$CFLAGS" CPPFLAGS="$CPPFLAGS" \
	    LDFLAGS="$LDFLAGS" LDSTATIC="$LDSTATIC" LIBS="$LIBS" \
	    sh ../../Build.sh $arg && \
	    test -f $tfn && gotbin=1
	if test $gotbin = 0; then
		echo ::error::$where failed
		Grv=1
		cd ../..
		return
	fi
	if ./$tfn ../../.extra/rtchecks >rtchecks.out; then
		set -- $(md5sum rtchecks.out)
		test d5345290b8f3a343f6446710007d4e5a = "$1" || {
			echo "N: Input:"
			cat ../../.extra/rtchecks
			echo "N: Output:"
			cat rtchecks.out
			echo ::error::rtchecks failed
			Grv=1
		}
	else
		echo "N: Output:"
		cat rtchecks.out
		echo ::error::rtchecks aborted
		Grv=1
	fi
	perl ../../check.pl -U $tstloc -s ../../.extra/mtest.t \
	    -p ./$tfn -v 2>&1 | tee mtest.log
	if grep '^Total failed: 0$' mtest.log >/dev/null; then
		echo "I: Simple tests okay."
	else
		echo ::error::mtests failed
		Grv=1
	fi
	rm -f "$topdir/builddir/urtmp*" utest.*
	echo >test.wait
	topdir="$topdir" tstloc="$tstloc" script -c 'echo 1 >"$topdir/builddir/urtmp.f"; (./test.sh -U $tstloc -v; echo $? >utest.rv) 2>&1 | tee utest.log; x=$?; sleep 1; rm -f test.wait; exit $x'
	maxwait=0
	while test -e test.wait; do
		sleep 1
		maxwait=$(expr $maxwait + 1)
		test $maxwait -lt 900 || break
	done
	echo s >"$topdir/builddir/urtmp.p"
	sleep 1 # synchronise I/O
	# set $testrv to x unless it’s a positive integer
	testrv=$(cat utest.rv 2>/dev/null) || testrv=x
	case x$testrv in
	(x) testrv=x ;;
	(x*[!0-9]*) testrv=x ;;
	esac
	if test $testrv != x && \
	    grep '^pass .*:KSH_VERSION' utest.log >/dev/null 2>&1; then
		echo "I: Regression test suite run. Errorlevel: $testrv"
		case $testrv in
		(0) ;;
		(*)
			echo ::error::in the testsuite
			Grv=1 ;;
		esac
	else
		echo ::error::testsuite not run\?
		if test -e "$topdir/builddir/urtmp.f"; then
			echo "N: It was attempted, though..."
		fi
		if test -e utest.rv; then
			echo "N: Errorlevel: $(cat utest.rv)"
		else
			echo "N: Errorlevel unknown."
		fi
		echo "N: Failing this build."
		Grv=1
	fi
	cd ../..
}
echo ::group::Setup $0 on Debian $dist
buildessential=build-essential
bsdextrautils=bsdextrautils
deps=ed
tstloc=C.UTF-8
wextra=-Wextra
case $dist in
slink)
	buildessential='gcc g++'
	bsdextrautils=bsdmainutils
	deps="$deps locales"
	tstloc=en_US.UTF-8
	wextra=-W
	HAVE_ATTRIBUTE_BOUNDED=0; export HAVE_ATTRIBUTE_BOUNDED
	;;
potato|woody|sarge|etch)
	bsdextrautils=bsdmainutils
	deps="$deps locales"
	tstloc=en_US.UTF-8
	wextra=-W
	HAVE_ATTRIBUTE_BOUNDED=0; export HAVE_ATTRIBUTE_BOUNDED
	;;
lenny|squeeze)
	bsdextrautils=bsdmainutils
	deps="$deps locales"
	tstloc=en_US.UTF-8
	;;
wheezy)
	bsdextrautils=bsdmainutils
	;;
jessie)
	cat >/etc/apt/sources.list <<\EOF
deb http://archive.debian.org/debian/ jessie main non-free contrib
deb http://archive.debian.org/debian-security/ jessie/updates main non-free contrib
EOF
	rm -f /etc/apt/sources.list.d/*
	bsdextrautils=bsdmainutils
	;;
stretch)
	cat >/etc/apt/sources.list <<\EOF
deb http://archive.debian.org/debian/ stretch main non-free contrib
deb http://archive.debian.org/debian-security/ stretch/updates main non-free contrib
EOF
	rm -f /etc/apt/sources.list.d/*
	bsdextrautils=bsdmainutils
	;;
buster)
	bsdextrautils=bsdmainutils
	;;
bullseye|bookworm|sid)
	;;
*)
	echo ::error::unknown distro
	;;
esac
deps="$deps $bsdextrautils"
case $libc in
glibc|eglibc)
	;;
diet)
	deps="$deps dietlibc-dev" ;;
klibc)
	deps="$deps libklibc-dev" ;;
musl)
	deps="$deps musl-tools" ;;
*)
	echo ::error::unknown libc
	libc=glibc
	;;
esac
set -x
cat >>/etc/apt/apt.conf <<\EOF
debug::pkgproblemresolver "true";
APT::Install-Recommends "0";
EOF
apt-get update
apt-get install -y $buildessential $deps
rm -rf builddir
topdir=$(pwd)
mkdir builddir
case $tstloc in
C.UTF-8) ;;
*)
	mkdir builddir/tloc
	# cf. Debian #522776
	if localedef -i ${tstloc%.*} -c -f ${tstloc#*.} builddir/tloc/$tstloc; then
		LOCPATH=$topdir/builddir/tloc; export LOCPATH
	else
		echo ::error::localedef error, expect test failures
	fi
	;;
esac
echo ::endgroup::
echo ::group::Init on $dist with $libc
# get maximum of hardening flags
DEB_BUILD_MAINT_OPTIONS="hardening=+all optimize=-lto"
export DEB_BUILD_MAINT_OPTIONS
mkdir builddir/full builddir/legacy
cp .extra/printf.c builddir/legacy/
dCC=gcc
dCFLAGS=$(dpkg-buildflags --get CFLAGS 2>/dev/null) || dCFLAGS=-O2
dCPPFLAGS=$(dpkg-buildflags --get CPPFLAGS 2>/dev/null)
dLDFLAGS=$(dpkg-buildflags --get LDFLAGS 2>/dev/null)
dCFLAGS="$dCFLAGS -Wall $wextra"
HAVE_CAN_WALL=0; export HAVE_CAN_WALL
HAVE_LIBUTIL_H=0; export HAVE_LIBUTIL_H
# build mksh-static
sCC=$dCC
# drop optimisation, debugging, SSP and PIC flags for mksh-static
sCFLAGS=
for x in $dCFLAGS; do
	case $x in
	(-O*|-g*|-fstack-protector*|-fPIE|-specs=*) ;;
	(*) sCFLAGS="$sCFLAGS $x" ;;
	esac
done
sCPPFLAGS="$dCPPFLAGS -DMKSH_BINSHPOSIX -DMKSH_BINSHREDUCED"
# drop PIC flags for mksh-static
sLDFLAGS=
for x in $dLDFLAGS; do
	case $x in
	(-pie|-fPIE|-specs=*) ;;
	(*) sLDFLAGS="$sLDFLAGS $x" ;;
	esac
done
sLIBS=
echo ::endgroup::
echo ::group::Build mksh
case $libc in
diet)
	CC="diet -Os $sCC"
	CFLAGS=$sCFLAGS
	CPPFLAGS=$dCPPFLAGS
	LDFLAGS=$sLDFLAGS
	LDSTATIC=" " # diet defaults to static
	LIBS=$sLIBS
	;;
klibc)
	CC=klcc
	CFLAGS="$(klcc -print-klibc-optflags) $sCFLAGS"
	CPPFLAGS=$dCPPFLAGS
	LDFLAGS=$sLDFLAGS
	LDSTATIC="-static"
	LIBS=$sLIBS
	;;
musl)
	CC=musl-gcc
	CFLAGS="-Os $sCFLAGS"
	CPPFLAGS=$dCPPFLAGS
	LDFLAGS=$sLDFLAGS
	LDSTATIC="-static"
	LIBS=$sLIBS
	;;
*)
	CC=$dCC
	CFLAGS=$dCFLAGS
	CPPFLAGS=$dCPPFLAGS
	LDFLAGS=$dLDFLAGS
	unset LDSTATIC
	LIBS=
	;;
esac
set +e
trybuild full
set -e
echo ::endgroup::
echo ::group::Build lksh
USE_PRINTF_BUILTIN=1; export USE_PRINTF_BUILTIN
case $libc in
diet)
	CC="diet -Os $sCC"
	CFLAGS=$sCFLAGS
	CPPFLAGS=$sCPPFLAGS
	LDFLAGS=$sLDFLAGS
	LDSTATIC=" " # diet defaults to static
	LIBS=$sLIBS
	;;
klibc)
	CC=klcc
	CFLAGS="$(klcc -print-klibc-optflags) $sCFLAGS"
	CPPFLAGS=$sCPPFLAGS
	LDFLAGS=$sLDFLAGS
	LDSTATIC="-static"
	LIBS=$sLIBS
	;;
musl)
	CC=musl-gcc
	CFLAGS="-O2 $sCFLAGS"
	CPPFLAGS=$sCPPFLAGS
	LDFLAGS=$sLDFLAGS
	LDSTATIC="-static"
	LIBS=$sLIBS
	;;
*)
	CC=$dCC
	CFLAGS=$dCFLAGS
	CPPFLAGS=$dCPPFLAGS
	LDFLAGS=$dLDFLAGS
	unset LDSTATIC
	LIBS=
	;;
esac
set +e
trybuild legacy
echo ::endgroup::
exit $Grv
