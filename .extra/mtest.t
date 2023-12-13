# Catch the most basic failures to run (broken ABI, signals, dietlibc bug,
# select not implemented)
# Failures to pass these leads to the binary being thrown away, if run

name: mtest-builtin
description:
	Minitest: can run a builtin
time-limit: 3
stdin:
	echo foo
expected-stdout:
	foo
---
name: mtest-external
description:
	Minitest: can run an external utility and return
time-limit: 3
stdin:
	echo baz | /usr/bin/tr z r
	echo baz
expected-stdout:
	bar
	baz
---
name: mtest-ascii1
description:
	Part of dollar-quoted-strings
time-limit: 3
stdin:
	printf '<\1\n'|while read x;do while [[ -n $x ]];do typeset -i16 hv=1#${x::1};x=${x:1};echo -n "$hv ";done;done;echo .
expected-stdout:
	16#3c 16#1 .
---
name: mtest-brkcontin
description:
	Check that break and continue work; used by test.sh itself
	and broken at least once on Debian derivate that cannot be named
time-limit: 3
stdin:
	for x in "echo 1" false "echo 2"; do $x && continue; echo 3; break; done; echo 4
expected-stdout:
	1
	3
	4
---
name: mtest-select-works
description:
	Check that we correctly detect our host system has select(2)
	and that it is actually implemented, exported from libc and working
time-limit: 3
stdin:
	print foo | while read -t 1 bar; do print ${bar}bar; done
	sleep 1
	print baz
expected-stdout:
	foobar
	baz
---
name: mtest-mtime
description:
	An advanced version of regression-62 to catch libc bugs
time-limit: 99
stdin:
	matrix() {
		local a b c d e f g h
		test a -nt b; a=$?
		test b -nt a; b=$?
		test a -ot b; c=$?
		test b -ot a; d=$?
		test a -nt a; e=$?
		test b -nt b; f=$?
		test a -ot a; g=$?
		test b -ot b; h=$?
		echo $1 $a $b $c $d / $e $f $g $h .
	}
	matrix a
	:>a
	matrix b
	sleep 2		# mtime granularity for OS/2 and FAT
	:>b
	matrix c
	i=0; while (( ++i < 100 )); do sleep 0.1; :>b; matrix C; done | uniq
	sleep 2
	echo dummy >a	# Debian GNU/Hurd #955270
	matrix d
	i=0; while (( ++i < 100 )); do sleep 0.1; echo dummy >a; matrix D; done | uniq
	rm a
	matrix e
expected-stdout:
	a 1 1 1 1 / 1 1 1 1 .
	b 0 1 1 0 / 1 1 1 1 .
	c 1 0 0 1 / 1 1 1 1 .
	C 1 0 0 1 / 1 1 1 1 .
	d 0 1 1 0 / 1 1 1 1 .
	D 0 1 1 0 / 1 1 1 1 .
	e 1 0 0 1 / 1 1 1 1 .
---
