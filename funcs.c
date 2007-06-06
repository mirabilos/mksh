/*	$OpenBSD: c_ksh.c,v 1.29 2006/03/12 00:26:58 deraadt Exp $	*/
/*	$OpenBSD: c_sh.c,v 1.35 2006/04/10 14:38:59 jaredy Exp $	*/
/*	$OpenBSD: c_test.c,v 1.17 2005/03/30 17:16:37 deraadt Exp $	*/
/*	$OpenBSD: c_ulimit.c,v 1.16 2006/11/20 21:53:39 miod Exp $	*/

#include "sh.h"

__RCSID("$MirOS: src/bin/mksh/funcs.c,v 1.57 2007/06/06 23:41:23 tg Exp $");

int
c_cd(const char **wp)
{
	int optc;
	int physical = Flag(FPHYSICAL);
	int cdnode;			/* was a node from cdpath added in? */
	int printpath = 0;		/* print where we cd'd? */
	int rval;
	struct tbl *pwd_s, *oldpwd_s;
	XString xs;
	char *xp;
	char *dir, *try, *pwd;
	int phys_path;
	char *cdpath;
	bool dir_ = false;

	while ((optc = ksh_getopt(wp, &builtin_opt, "LP")) != -1)
		switch (optc) {
		case 'L':
			physical = 0;
			break;
		case 'P':
			physical = 1;
			break;
		case '?':
			return 1;
		}
	wp += builtin_opt.optind;

	if (Flag(FRESTRICTED)) {
		bi_errorf("restricted shell - can't cd");
		return 1;
	}

	pwd_s = global("PWD");
	oldpwd_s = global("OLDPWD");

	if (!wp[0]) {
		/* No arguments - go home */
		if ((dir = str_val(global("HOME"))) == null) {
			bi_errorf("no home directory (HOME not set)");
			return 1;
		}
	} else if (!wp[1]) {
		/* One argument: - or dir */
		dir = str_save(wp[0], ATEMP);
		if (ksh_isdash(dir)) {
			afree(dir, ATEMP);
			dir = str_val(oldpwd_s);
			if (dir == null) {
				bi_errorf("no OLDPWD");
				return 1;
			}
			printpath++;
		} else
			dir_ = true;
	} else if (!wp[2]) {
		/* Two arguments - substitute arg1 in PWD for arg2 */
		int ilen, olen, nlen, elen;
		char *cp;

		if (!current_wd[0]) {
			bi_errorf("don't know current directory");
			return 1;
		}
		/* substitute arg1 for arg2 in current path.
		 * if the first substitution fails because the cd fails
		 * we could try to find another substitution. For now
		 * we don't
		 */
		if ((cp = strstr(current_wd, wp[0])) == NULL) {
			bi_errorf("bad substitution");
			return 1;
		}
		ilen = cp - current_wd;
		olen = strlen(wp[0]);
		nlen = strlen(wp[1]);
		elen = strlen(current_wd + ilen + olen) + 1;
		dir = alloc(ilen + nlen + elen, ATEMP);
		dir_ = true;
		memcpy(dir, current_wd, ilen);
		memcpy(dir + ilen, wp[1], nlen);
		memcpy(dir + ilen + nlen, current_wd + ilen + olen, elen);
		printpath++;
	} else {
		bi_errorf("too many arguments");
		return 1;
	}

	Xinit(xs, xp, PATH_MAX, ATEMP);
	/* xp will have a bogus value after make_path() - set it to 0
	 * so that if it's used, it will cause a dump
	 */
	xp = NULL;

	cdpath = str_val(global("CDPATH"));
	do {
		cdnode = make_path(current_wd, dir, &cdpath, &xs, &phys_path);
		if (physical)
			rval = chdir(try = Xstring(xs, xp) + phys_path);
		else {
			simplify_path(Xstring(xs, xp));
			rval = chdir(try = Xstring(xs, xp));
		}
	} while (rval < 0 && cdpath != NULL);

	if (rval < 0) {
		if (cdnode)
			bi_errorf("%s: bad directory", dir);
		else
			bi_errorf("%s - %s", try, strerror(errno));
		afreechv(dir_, dir);
		return 1;
	}

	/* Clear out tracked aliases with relative paths */
	flushcom(0);

	/* Set OLDPWD (note: unsetting OLDPWD does not disable this
	 * setting in at&t ksh)
	 */
	if (current_wd[0])
		/* Ignore failure (happens if readonly or integer) */
		setstr(oldpwd_s, current_wd, KSH_RETURN_ERROR);

	if (Xstring(xs, xp)[0] != '/') {
		pwd = NULL;
	} else
	if (!physical || !(pwd = get_phys_path(Xstring(xs, xp))))
		pwd = Xstring(xs, xp);

	/* Set PWD */
	if (pwd) {
		char *ptmp = pwd;
		set_current_wd(ptmp);
		/* Ignore failure (happens if readonly or integer) */
		setstr(pwd_s, ptmp, KSH_RETURN_ERROR);
	} else {
		set_current_wd(null);
		pwd = Xstring(xs, xp);
		/* XXX unset $PWD? */
	}
	if (printpath || cdnode)
		shprintf("%s\n", pwd);

	afreechv(dir_, dir);
	return 0;
}

int
c_pwd(const char **wp)
{
	int optc;
	int physical = Flag(FPHYSICAL);
	char *p;

	while ((optc = ksh_getopt(wp, &builtin_opt, "LP")) != -1)
		switch (optc) {
		case 'L':
			physical = 0;
			break;
		case 'P':
			physical = 1;
			break;
		case '?':
			return 1;
		}
	wp += builtin_opt.optind;

	if (wp[0]) {
		bi_errorf("too many arguments");
		return 1;
	}
	p = current_wd[0] ? (physical ? get_phys_path(current_wd) : current_wd) :
	    NULL;
	if (p && access(p, R_OK) < 0)
		p = NULL;
	if (!p && !(p = ksh_get_wd(NULL))) {
		bi_errorf("can't get current directory - %s", strerror(errno));
		return 1;
	}
	shprintf("%s\n", p);
	return 0;
}

int
c_print(const char **wp)
{
#define PO_NL		BIT(0)	/* print newline */
#define PO_EXPAND	BIT(1)	/* expand backslash sequences */
#define PO_PMINUSMINUS	BIT(2)	/* print a -- argument */
#define PO_HIST		BIT(3)	/* print to history instead of stdout */
#define PO_COPROC	BIT(4)	/* printing to coprocess: block SIGPIPE */
	int fd = 1;
	int flags = PO_EXPAND|PO_NL;
	const char *s;
	const char *emsg;
	XString xs;
	char *xp;

	if (wp[0][0] == 'e') {	/* echo command */
		int nflags = flags;

		/* A compromise between sysV and BSD echo commands:
		 * escape sequences are enabled by default, and
		 * -n, -e and -E are recognised if they appear
		 * in arguments with no illegal options (ie, echo -nq
		 * will print -nq).
		 * Different from sysV echo since options are recognised,
		 * different from BSD echo since escape sequences are enabled
		 * by default.
		 */
		wp += 1;
		while ((s = *wp) && *s == '-' && s[1]) {
			while (*++s)
				if (*s == 'n')
					nflags &= ~PO_NL;
				else if (*s == 'e')
					nflags |= PO_EXPAND;
				else if (*s == 'E')
					nflags &= ~PO_EXPAND;
				else
					/* bad option: don't use nflags, print
					 * argument
					 */
					break;
			if (*s)
				break;
			wp++;
			flags = nflags;
		}
	} else {
		int optc;
		const char *opts = "Rnprsu,";
		while ((optc = ksh_getopt(wp, &builtin_opt, opts)) != -1)
			switch (optc) {
			case 'R': /* fake BSD echo command */
				flags |= PO_PMINUSMINUS;
				flags &= ~PO_EXPAND;
				opts = "ne";
				break;
			case 'e':
				flags |= PO_EXPAND;
				break;
			case 'n':
				flags &= ~PO_NL;
				break;
			case 'p':
				if ((fd = coproc_getfd(W_OK, &emsg)) < 0) {
					bi_errorf("-p: %s", emsg);
					return 1;
				}
				break;
			case 'r':
				flags &= ~PO_EXPAND;
				break;
			case 's':
				flags |= PO_HIST;
				break;
			case 'u':
				if (!*(s = builtin_opt.optarg))
					fd = 0;
				else if ((fd = check_fd(s, W_OK, &emsg)) < 0) {
					bi_errorf("-u: %s: %s", s, emsg);
					return 1;
				}
				break;
			case '?':
				return 1;
			}
		if (!(builtin_opt.info & GI_MINUSMINUS)) {
			/* treat a lone - like -- */
			if (wp[builtin_opt.optind] &&
			    ksh_isdash(wp[builtin_opt.optind]))
				builtin_opt.optind++;
		} else if (flags & PO_PMINUSMINUS)
			builtin_opt.optind--;
		wp += builtin_opt.optind;
	}

	Xinit(xs, xp, 128, ATEMP);

	while (*wp != NULL) {
		int c;
		s = *wp;
		while ((c = *s++) != '\0') {
			Xcheck(xs, xp);
			if ((flags & PO_EXPAND) && c == '\\') {
				int i;

				switch ((c = *s++)) {
				/* Oddly enough, \007 seems more portable than
				 * \a (due to HP-UX cc, Ultrix cc, old PCCs,
				 * etc.).
				 */
				case 'a': c = '\007'; break;
				case 'b': c = '\b'; break;
				case 'c': flags &= ~PO_NL;
					  continue; /* AT&T brain damage */
				case 'f': c = '\f'; break;
				case 'n': c = '\n'; break;
				case 'r': c = '\r'; break;
				case 't': c = '\t'; break;
				case 'v': c = 0x0B; break;
				case '0':
					/* Look for an octal number: can have
					 * three digits (not counting the
					 * leading 0).  Truly burnt.
					 */
					c = 0;
					for (i = 0; i < 3; i++) {
						if (*s >= '0' && *s <= '7')
							c = c*8 + *s++ - '0';
						else
							break;
					}
					break;
				case '\0': s--; c = '\\'; break;
				case '\\': break;
				default:
					Xput(xs, xp, '\\');
				}
			}
			Xput(xs, xp, c);
		}
		if (*++wp != NULL)
			Xput(xs, xp, ' ');
	}
	if (flags & PO_NL)
		Xput(xs, xp, '\n');

	if (flags & PO_HIST) {
		Xput(xs, xp, '\0');
		source->line++;
		histsave(source->line, Xstring(xs, xp), 1);
		Xfree(xs, xp);
	} else {
		int n, len = Xlength(xs, xp);
		int opipe = 0;

		/* Ensure we aren't killed by a SIGPIPE while writing to
		 * a coprocess.  at&t ksh doesn't seem to do this (seems
		 * to just check that the co-process is alive which is
		 * not enough).
		 */
		if (coproc.write >= 0 && coproc.write == fd) {
			flags |= PO_COPROC;
			opipe = block_pipe();
		}
		for (s = Xstring(xs, xp); len > 0; ) {
			n = write(fd, s, len);
			if (n < 0) {
				if (flags & PO_COPROC)
					restore_pipe(opipe);
				if (errno == EINTR) {
					/* allow user to ^C out */
					intrcheck();
					if (flags & PO_COPROC)
						opipe = block_pipe();
					continue;
				}
				return 1;
			}
			s += n;
			len -= n;
		}
		if (flags & PO_COPROC)
			restore_pipe(opipe);
	}

	return 0;
}

int
c_whence(const char **wp)
{
	struct tbl *tp;
	const char *id;
	int pflag = 0, vflag = 0, Vflag = 0;
	int ret = 0;
	int optc;
	int iam_whence = wp[0][0] == 'w';
	int fcflags;
	const char *opts = iam_whence ? "pv" : "pvV";

	while ((optc = ksh_getopt(wp, &builtin_opt, opts)) != -1)
		switch (optc) {
		case 'p':
			pflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		case 'V':
			Vflag = 1;
			break;
		case '?':
			return 1;
		}
	wp += builtin_opt.optind;


	fcflags = FC_BI | FC_PATH | FC_FUNC;
	if (!iam_whence) {
		/* Note that -p on its own is deal with in comexec() */
		if (pflag)
			fcflags |= FC_DEFPATH;
		/* Convert command options to whence options - note that
		 * command -pV uses a different path search than whence -v
		 * or whence -pv.  This should be considered a feature.
		 */
		vflag = Vflag;
	}
	if (pflag)
		fcflags &= ~(FC_BI | FC_FUNC);

	while ((vflag || ret == 0) && (id = *wp++) != NULL) {
		tp = NULL;
		if ((iam_whence || vflag) && !pflag)
			tp = ktsearch(&keywords, id, hash(id));
		if (!tp && !pflag) {
			tp = ktsearch(&aliases, id, hash(id));
			if (tp && !(tp->flag & ISSET))
				tp = NULL;
		}
		if (!tp)
			tp = findcom(id, fcflags);
		if (vflag || (tp->type != CALIAS && tp->type != CEXEC &&
		    tp->type != CTALIAS))
			shprintf("%s", id);
		switch (tp->type) {
		case CKEYWD:
			if (vflag)
				shprintf(" is a reserved word");
			break;
		case CALIAS:
			if (vflag)
				shprintf(" is an %salias for ",
				    (tp->flag & EXPORT) ? "exported " :
				    null);
			if (!iam_whence && !vflag)
				shprintf("alias %s=", id);
			print_value_quoted(tp->val.s);
			break;
		case CFUNC:
			if (vflag) {
				shprintf(" is a");
				if (tp->flag & EXPORT)
					shprintf("n exported");
				if (tp->flag & TRACE)
					shprintf(" traced");
				if (!(tp->flag & ISSET)) {
					shprintf(" undefined");
					if (tp->u.fpath)
						shprintf(" (autoload from %s)",
						    tp->u.fpath);
				}
				shprintf(" function");
			}
			break;
		case CSHELL:
			if (vflag)
				shprintf(" is a%s shell builtin",
				    (tp->flag & SPEC_BI) ? " special" : null);
			break;
		case CTALIAS:
		case CEXEC:
			if (tp->flag & ISSET) {
				if (vflag) {
					shprintf(" is ");
					if (tp->type == CTALIAS)
						shprintf("a tracked %salias for ",
						    (tp->flag & EXPORT) ?
						    "exported " : null);
				}
				shprintf("%s", tp->val.s);
			} else {
				if (vflag)
					shprintf(" not found");
				ret = 1;
			}
			break;
		default:
			shprintf("%s is *GOK*", id);
			break;
		}
		if (vflag || !ret)
			shprintf(newline);
	}
	return ret;
}

/* Deal with command -vV - command -p dealt with in comexec() */
int
c_command(const char **wp)
{
	/* Let c_whence do the work.  Note that c_command() must be
	 * a distinct function from c_whence() (tested in comexec()).
	 */
	return c_whence(wp);
}

/* typeset, export, and readonly */
int
c_typeset(const char **wp)
{
	struct block *l = e->loc;
	struct tbl *vp, **p;
	Tflag fset = 0, fclr = 0;
	int thing = 0, func = 0, localv = 0;
	const char *opts = "L#R#UZ#fi#lprtux";	/* see comment below */
	const char *fieldstr, *basestr;
	int field, base;
	int optc;
	Tflag flag;
	int pflag = 0;

	switch (**wp) {
	case 'e':		/* export */
		fset |= EXPORT;
		opts = "p";
		break;
	case 'r':		/* readonly */
		fset |= RDONLY;
		opts = "p";
		break;
	case 's':		/* set */
		/* called with 'typeset -' */
		break;
	case 't':		/* typeset */
		localv = 1;
		break;
	}

	fieldstr = basestr = NULL;
	builtin_opt.flags |= GF_PLUSOPT;
	/* at&t ksh seems to have 0-9 as options which are multiplied
	 * to get a number that is used with -L, -R, -Z or -i (eg, -1R2
	 * sets right justify in a field of 12).  This allows options
	 * to be grouped in an order (eg, -Lu12), but disallows -i8 -L3 and
	 * does not allow the number to be specified as a separate argument
	 * Here, the number must follow the RLZi option, but is optional
	 * (see the # kludge in ksh_getopt()).
	 */
	while ((optc = ksh_getopt(wp, &builtin_opt, opts)) != -1) {
		flag = 0;
		switch (optc) {
		case 'L':
			flag = LJUST;
			fieldstr = builtin_opt.optarg;
			break;
		case 'R':
			flag = RJUST;
			fieldstr = builtin_opt.optarg;
			break;
		case 'U':
			/* at&t ksh uses u, but this conflicts with
			 * upper/lower case.  If this option is changed,
			 * need to change the -U below as well
			 */
			flag = INT_U;
			break;
		case 'Z':
			flag = ZEROFIL;
			fieldstr = builtin_opt.optarg;
			break;
		case 'f':
			func = 1;
			break;
		case 'i':
			flag = INTEGER;
			basestr = builtin_opt.optarg;
			break;
		case 'l':
			flag = LCASEV;
			break;
		case 'p':
			/* posix export/readonly -p flag.
			 * typeset -p is the same as typeset (in pdksh);
			 * here for compatibility with ksh93.
			 */
			pflag = 1;
			continue;
		case 'r':
			flag = RDONLY;
			break;
		case 't':
			flag = TRACE;
			break;
		case 'u':
			flag = UCASEV_AL;	/* upper case / autoload */
			break;
		case 'x':
			flag = EXPORT;
			break;
		case '?':
			return 1;
		}
		if (builtin_opt.info & GI_PLUS) {
			fclr |= flag;
			fset &= ~flag;
			thing = '+';
		} else {
			fset |= flag;
			fclr &= ~flag;
			thing = '-';
		}
	}

	field = 0;
	if (fieldstr && !bi_getn(fieldstr, &field))
		return 1;
	base = 0;
	if (basestr && !bi_getn(basestr, &base))
		return 1;

	if (!(builtin_opt.info & GI_MINUSMINUS) && wp[builtin_opt.optind] &&
	    (wp[builtin_opt.optind][0] == '-' ||
	    wp[builtin_opt.optind][0] == '+') &&
	    wp[builtin_opt.optind][1] == '\0') {
		thing = wp[builtin_opt.optind][0];
		builtin_opt.optind++;
	}

	if (func && ((fset|fclr) & ~(TRACE|UCASEV_AL|EXPORT))) {
		bi_errorf("only -t, -u and -x options may be used with -f");
		return 1;
	}
	if (wp[builtin_opt.optind]) {
		/* Take care of exclusions.
		 * At this point, flags in fset are cleared in fclr and vise
		 * versa.  This property should be preserved.
		 */
		if (fset & LCASEV)	/* LCASEV has priority over UCASEV_AL */
			fset &= ~UCASEV_AL;
		if (fset & LJUST)	/* LJUST has priority over RJUST */
			fset &= ~RJUST;
		if ((fset & (ZEROFIL|LJUST)) == ZEROFIL) { /* -Z implies -ZR */
			fset |= RJUST;
			fclr &= ~RJUST;
		}
		/* Setting these attributes clears the others, unless they
		 * are also set in this command
		 */
		if (fset & (LJUST | RJUST | ZEROFIL | UCASEV_AL | LCASEV |
		    INTEGER | INT_U | INT_L))
			fclr |= ~fset & (LJUST | RJUST | ZEROFIL | UCASEV_AL |
			    LCASEV | INTEGER | INT_U | INT_L);
	}

	/* set variables and attributes */
	if (wp[builtin_opt.optind]) {
		int i;
		int rval = 0;
		struct tbl *f;

		if (localv && !func)
			fset |= LOCAL;
		for (i = builtin_opt.optind; wp[i]; i++) {
			if (func) {
				f = findfunc(wp[i], hash(wp[i]),
				    (fset&UCASEV_AL) ? true : false);
				if (!f) {
					/* at&t ksh does ++rval: bogus */
					rval = 1;
					continue;
				}
				if (fset | fclr) {
					f->flag |= fset;
					f->flag &= ~fclr;
				} else
					fptreef(shl_stdout, 0,
					    f->flag & FKSH ?
					    "function %s %T\n" :
					    "%s() %T\n", wp[i], f->val.t);
			} else if (!typeset(wp[i], fset, fclr, field, base)) {
				bi_errorf("%s: not identifier", wp[i]);
				return 1;
			}
		}
		return rval;
	}

	/* list variables and attributes */
	flag = fset | fclr; /* no difference at this point.. */
	if (func) {
		for (l = e->loc; l; l = l->next) {
			for (p = ktsort(&l->funs); (vp = *p++); ) {
				if (flag && (vp->flag & flag) == 0)
					continue;
				if (thing == '-')
					fptreef(shl_stdout, 0, vp->flag & FKSH ?
					    "function %s %T\n" : "%s() %T\n",
					    vp->name, vp->val.t);
				else
					shprintf("%s\n", vp->name);
			}
		}
	} else {
		for (l = e->loc; l; l = l->next) {
			for (p = ktsort(&l->vars); (vp = *p++); ) {
				struct tbl *tvp;
				int any_set = 0;
				/*
				 * See if the parameter is set (for arrays, if any
				 * element is set).
				 */
				for (tvp = vp; tvp; tvp = tvp->u.array)
					if (tvp->flag & ISSET) {
						any_set = 1;
						break;
					}

				/*
				 * Check attributes - note that all array elements
				 * have (should have?) the same attributes, so checking
				 * the first is sufficient.
				 *
				 * Report an unset param only if the user has
				 * explicitly given it some attribute (like export);
				 * otherwise, after "echo $FOO", we would report FOO...
				 */
				if (!any_set && !(vp->flag & USERATTRIB))
					continue;
				if (flag && (vp->flag & flag) == 0)
					continue;
				for (; vp; vp = vp->u.array) {
					/* Ignore array elements that aren't
					 * set unless there are no set elements,
					 * in which case the first is reported on */
					if ((vp->flag&ARRAY) && any_set &&
					    !(vp->flag & ISSET))
						continue;
					/* no arguments */
					if (thing == 0 && flag == 0) {
						/* at&t ksh prints things
						 * like export, integer,
						 * leftadj, zerofill, etc.,
						 * but POSIX says must
						 * be suitable for re-entry...
						 */
						shprintf("typeset ");
						if ((vp->flag&INTEGER))
							shprintf("-i ");
						if ((vp->flag&EXPORT))
							shprintf("-x ");
						if ((vp->flag&RDONLY))
							shprintf("-r ");
						if ((vp->flag&TRACE))
							shprintf("-t ");
						if ((vp->flag&LJUST))
							shprintf("-L%d ", vp->u2.field);
						if ((vp->flag&RJUST))
							shprintf("-R%d ", vp->u2.field);
						if ((vp->flag&ZEROFIL))
							shprintf("-Z ");
						if ((vp->flag&LCASEV))
							shprintf("-l ");
						if ((vp->flag&UCASEV_AL))
							shprintf("-u ");
						if ((vp->flag&INT_U))
							shprintf("-U ");
						shprintf("%s\n", vp->name);
						    if (vp->flag&ARRAY)
						break;
					} else {
						if (pflag)
							shprintf("typeset ");
						if ((vp->flag&ARRAY) && any_set)
							shprintf("%s[%d]",
							    vp->name, vp->index);
						else
							shprintf("%s", vp->name);
						if (thing == '-' && (vp->flag&ISSET)) {
							char *s = str_val(vp);

							shprintf("=");
							/* at&t ksh can't have
							 * justified integers.. */
							if ((vp->flag &
							    (INTEGER|LJUST|RJUST)) ==
							    INTEGER)
								shprintf("%s", s);
							else
								print_value_quoted(s);
						}
						shprintf(newline);
					}
					/* Only report first 'element' of an array with
					* no set elements.
					*/
					if (!any_set)
						break;
				}
			}
		}
	}
	return 0;
}

int
c_alias(const char **wp)
{
	struct table *t = &aliases;
	int rv = 0, rflag = 0, tflag, Uflag = 0, pflag = 0;
	int prefix = 0;
	Tflag xflag = 0;
	int optc;

	builtin_opt.flags |= GF_PLUSOPT;
	while ((optc = ksh_getopt(wp, &builtin_opt, "dprtUx")) != -1) {
		prefix = builtin_opt.info & GI_PLUS ? '+' : '-';
		switch (optc) {
		case 'd':
#ifdef MKSH_SMALL
			return (0);
#else
			t = &homedirs;
#endif
			break;
		case 'p':
			pflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 't':
			t = &taliases;
			break;
		case 'U':
			/*
			 * kludge for tracked alias initialization
			 * (don't do a path search, just make an entry)
			 */
			Uflag = 1;
			break;
		case 'x':
			xflag = EXPORT;
			break;
		case '?':
			return 1;
		}
	}
	wp += builtin_opt.optind;

	if (!(builtin_opt.info & GI_MINUSMINUS) && *wp &&
	    (wp[0][0] == '-' || wp[0][0] == '+') && wp[0][1] == '\0') {
		prefix = wp[0][0];
		wp++;
	}

	tflag = t == &taliases;

	/* "hash -r" means reset all the tracked aliases.. */
	if (rflag) {
		static const char *args[] = {
			"unalias", "-ta", NULL
		};

		if (!tflag || *wp) {
			shprintf("alias: -r flag can only be used with -t"
			    " and without arguments\n");
			return 1;
		}
		ksh_getopt_reset(&builtin_opt, GF_ERROR);
		return c_unalias(args);
	}

	if (*wp == NULL) {
		struct tbl *ap, **p;

		for (p = ktsort(t); (ap = *p++) != NULL; )
			if ((ap->flag & (ISSET|xflag)) == (ISSET|xflag)) {
				if (pflag)
					shf_puts("alias ", shl_stdout);
				shf_puts(ap->name, shl_stdout);
				if (prefix != '+') {
					shf_putc('=', shl_stdout);
					print_value_quoted(ap->val.s);
				}
				shprintf(newline);
			}
	}

	for (; *wp != NULL; wp++) {
		const char *alias = *wp;
		char *xalias = NULL;
		const char *val;
		const char *newval;
		struct tbl *ap;
		int h;

		if ((val = cstrchr(alias, '=')))
			alias = xalias = str_nsave(alias, val++ - alias, ATEMP);
		h = hash(alias);
		if (val == NULL && !tflag && !xflag) {
			ap = ktsearch(t, alias, h);
			if (ap != NULL && (ap->flag&ISSET)) {
				if (pflag)
					shf_puts("alias ", shl_stdout);
				shf_puts(ap->name, shl_stdout);
				if (prefix != '+') {
					shf_putc('=', shl_stdout);
					print_value_quoted(ap->val.s);
				}
				shprintf(newline);
			} else {
				shprintf("%s alias not found\n", alias);
				rv = 1;
			}
			continue;
		}
		ap = ktenter(t, alias, h);
		ap->type = tflag ? CTALIAS : CALIAS;
		/* Are we setting the value or just some flags? */
		if ((val && !tflag) || (!val && tflag && !Uflag)) {
			if (ap->flag&ALLOC) {
				ap->flag &= ~(ALLOC|ISSET);
				afree((void*)ap->val.s, APERM);
			}
			/* ignore values for -t (at&t ksh does this) */
			newval = tflag ? search(alias, path, X_OK, NULL) :
			    val;
			if (newval) {
				ap->val.s = str_save(newval, APERM);
				ap->flag |= ALLOC|ISSET;
			} else
				ap->flag &= ~ISSET;
		}
		ap->flag |= DEFINED;
		if (prefix == '+')
			ap->flag &= ~xflag;
		else
			ap->flag |= xflag;
		afreechk(xalias);
	}

	return rv;
}

int
c_unalias(const char **wp)
{
	struct table *t = &aliases;
	struct tbl *ap;
	int rv = 0, all = 0;
	int optc;

	while ((optc = ksh_getopt(wp, &builtin_opt, "adt")) != -1)
		switch (optc) {
		case 'a':
			all = 1;
			break;
		case 'd':
#ifdef MKSH_SMALL
			return (0);
#else
			t = &homedirs;
#endif
			break;
		case 't':
			t = &taliases;
			break;
		case '?':
			return 1;
		}
	wp += builtin_opt.optind;

	for (; *wp != NULL; wp++) {
		ap = ktsearch(t, *wp, hash(*wp));
		if (ap == NULL) {
			rv = 1;	/* POSIX */
			continue;
		}
		if (ap->flag&ALLOC) {
			ap->flag &= ~(ALLOC|ISSET);
			afree((void*)ap->val.s, APERM);
		}
		ap->flag &= ~(DEFINED|ISSET|EXPORT);
	}

	if (all) {
		struct tstate ts;

		for (ktwalk(&ts, t); (ap = ktnext(&ts)); ) {
			if (ap->flag&ALLOC) {
				ap->flag &= ~(ALLOC|ISSET);
				afree((void*)ap->val.s, APERM);
			}
			ap->flag &= ~(DEFINED|ISSET|EXPORT);
		}
	}

	return rv;
}

int
c_let(const char **wp)
{
	int rv = 1;
	long val;

	if (wp[1] == NULL) /* at&t ksh does this */
		bi_errorf("no arguments");
	else
		for (wp++; *wp; wp++)
			if (!evaluate(*wp, &val, KSH_RETURN_ERROR, true)) {
				rv = 2;	/* distinguish error from zero result */
				break;
			} else
				rv = val == 0;
	return rv;
}

int
c_jobs(const char **wp)
{
	int optc;
	int flag = 0;
	int nflag = 0;
	int rv = 0;

	while ((optc = ksh_getopt(wp, &builtin_opt, "lpnz")) != -1)
		switch (optc) {
		case 'l':
			flag = 1;
			break;
		case 'p':
			flag = 2;
			break;
		case 'n':
			nflag = 1;
			break;
		case 'z':	/* debugging: print zombies */
			nflag = -1;
			break;
		case '?':
			return 1;
		}
	wp += builtin_opt.optind;
	if (!*wp) {
		if (j_jobs(NULL, flag, nflag))
			rv = 1;
	} else {
		for (; *wp; wp++)
			if (j_jobs(*wp, flag, nflag))
				rv = 1;
	}
	return rv;
}

int
c_fgbg(const char **wp)
{
	int bg = strcmp(*wp, "bg") == 0;
	int rv = 0;

	if (!Flag(FMONITOR)) {
		bi_errorf("job control not enabled");
		return 1;
	}
	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;
	wp += builtin_opt.optind;
	if (*wp)
		for (; *wp; wp++)
			rv = j_resume(*wp, bg);
	else
		rv = j_resume("%%", bg);
	return bg ? 0 : rv;
}

struct kill_info {
	int num_width;
	int name_width;
};
static char *kill_fmt_entry(const void *, int, char *, int);

/* format a single kill item */
static char *
kill_fmt_entry(const void *arg, int i, char *buf, int buflen)
{
	const struct kill_info *ki = (const struct kill_info *)arg;

	i++;
	shf_snprintf(buf, buflen, "%*d %*s %s",
	    ki->num_width, i,
	    ki->name_width, sigtraps[i].name,
	    sigtraps[i].mess);
	return buf;
}


int
c_kill(const char **wp)
{
	Trap *t = NULL;
	const char *p;
	int lflag = 0;
	int i, n, rv, sig;

	/* assume old style options if -digits or -UPPERCASE */
	if ((p = wp[1]) && *p == '-' && (ksh_isdigit(p[1]) ||
	    ksh_isupper(p[1]))) {
		if (!(t = gettrap(p + 1, true))) {
			bi_errorf("bad signal '%s'", p + 1);
			return 1;
		}
		i = (wp[2] && strcmp(wp[2], "--") == 0) ? 3 : 2;
	} else {
		int optc;

		while ((optc = ksh_getopt(wp, &builtin_opt, "ls:")) != -1)
			switch (optc) {
			case 'l':
				lflag = 1;
				break;
			case 's':
				if (!(t = gettrap(builtin_opt.optarg, true))) {
					bi_errorf("bad signal '%s'",
					    builtin_opt.optarg);
					return 1;
				}
				break;
			case '?':
				return 1;
			}
		i = builtin_opt.optind;
	}
	if ((lflag && t) || (!wp[i] && !lflag)) {
#ifndef MKSH_SMALL
		shf_fprintf(shl_out,
		    "Usage: kill [ -s signame | -signum | -signame ] {pid|job}...\n"
		    "       kill -l [exit_status]\n");
#endif
		bi_errorf(null);
		return 1;
	}

	if (lflag) {
		if (wp[i]) {
			for (; wp[i]; i++) {
				if (!bi_getn(wp[i], &n))
					return 1;
				if (n > 128 && n < 128 + NSIG)
					n -= 128;
				if (n > 0 && n < NSIG)
					shprintf("%s\n", sigtraps[n].name);
				else
					shprintf("%d\n", n);
			}
		} else {
			int w, j;
			int mess_width;
			struct kill_info ki;

			for (j = NSIG, ki.num_width = 1; j >= 10; j /= 10)
				ki.num_width++;
			ki.name_width = mess_width = 0;
			for (j = 0; j < NSIG; j++) {
				w = strlen(sigtraps[j].name);
				if (w > ki.name_width)
					ki.name_width = w;
				w = strlen(sigtraps[j].mess);
				if (w > mess_width)
					mess_width = w;
			}

			print_columns(shl_stdout, NSIG - 1,
			    kill_fmt_entry, (void *)&ki,
			    ki.num_width + ki.name_width + mess_width + 3, 1);
		}
		return 0;
	}
	rv = 0;
	sig = t ? t->signal : SIGTERM;
	for (; (p = wp[i]); i++) {
		if (*p == '%') {
			if (j_kill(p, sig))
				rv = 1;
		} else if (!getn(p, &n)) {
			bi_errorf("%s: arguments must be jobs or process IDs",
			    p);
			rv = 1;
		} else {
			/* use killpg if < -1 since -1 does special things for
			 * some non-killpg-endowed kills
			 */
			if ((n < -1 ? killpg(-n, sig) : kill(n, sig)) < 0) {
				bi_errorf("%s: %s", p, strerror(errno));
				rv = 1;
			}
		}
	}
	return rv;
}

void
getopts_reset(int val)
{
	if (val >= 1) {
		ksh_getopt_reset(&user_opt, GF_NONAME | GF_PLUSOPT);
		user_opt.optind = user_opt.uoptind = val;
	}
}

int
c_getopts(const char **wp)
{
	int	argc;
	const char *opts;
	const char *var;
	int	optc;
	int	ret;
	char	buf[3];
	struct tbl *vq, *voptarg;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;
	wp += builtin_opt.optind;

	opts = *wp++;
	if (!opts) {
		bi_errorf("missing options argument");
		return 1;
	}

	var = *wp++;
	if (!var) {
		bi_errorf("missing name argument");
		return 1;
	}
	if (!*var || *skip_varname(var, true)) {
		bi_errorf("%s: is not an identifier", var);
		return 1;
	}

	if (e->loc->next == NULL) {
		internal_warningf("c_getopts: no argv");
		return 1;
	}
	/* Which arguments are we parsing... */
	if (*wp == NULL)
		wp = e->loc->next->argv;
	else
		*--wp = e->loc->next->argv[0];

	/* Check that our saved state won't cause a core dump... */
	for (argc = 0; wp[argc]; argc++)
		;
	if (user_opt.optind > argc ||
	    (user_opt.p != 0 &&
	    user_opt.p > strlen(wp[user_opt.optind - 1]))) {
		bi_errorf("arguments changed since last call");
		return 1;
	}

	user_opt.optarg = NULL;
	optc = ksh_getopt(wp, &user_opt, opts);

	if (optc >= 0 && optc != '?' && (user_opt.info & GI_PLUS)) {
		buf[0] = '+';
		buf[1] = optc;
		buf[2] = '\0';
	} else {
		/* POSIX says var is set to ? at end-of-options, at&t ksh
		 * sets it to null - we go with POSIX...
		 */
		buf[0] = optc < 0 ? '?' : optc;
		buf[1] = '\0';
	}

	/* at&t ksh does not change OPTIND if it was an unknown option.
	 * Scripts counting on this are prone to break... (ie, don't count
	 * on this staying).
	 */
	if (optc != '?') {
		user_opt.uoptind = user_opt.optind;
	}

	voptarg = global("OPTARG");
	voptarg->flag &= ~RDONLY;	/* at&t ksh clears ro and int */
	/* Paranoia: ensure no bizarre results. */
	if (voptarg->flag & INTEGER)
	    typeset("OPTARG", 0, INTEGER, 0, 0);
	if (user_opt.optarg == NULL)
		unset(voptarg, 0);
	else
		/* This can't fail (have cleared readonly/integer) */
		setstr(voptarg, user_opt.optarg, KSH_RETURN_ERROR);

	ret = 0;

	vq = global(var);
	/* Error message already printed (integer, readonly) */
	if (!setstr(vq, buf, KSH_RETURN_ERROR))
	    ret = 1;
	if (Flag(FEXPORT))
		typeset(var, EXPORT, 0, 0, 0);

	return optc < 0 ? 1 : ret;
}

int
c_bind(const char **wp)
{
	int optc, rv = 0, macro = 0, list = 0;
	const char *cp;
	char *up;

	while ((optc = ksh_getopt(wp, &builtin_opt, "lm")) != -1)
		switch (optc) {
		case 'l':
			list = 1;
			break;
		case 'm':
			macro = 1;
			break;
		case '?':
			return 1;
		}
	wp += builtin_opt.optind;

	if (*wp == NULL)	/* list all */
		rv = x_bind((char*)NULL, (char*)NULL, 0, list);

	for (; *wp != NULL; wp++) {
		if ((cp = cstrchr(*wp, '=')) == NULL)
			up = NULL;
		else {
			up = str_save(*wp, ATEMP);
			up[cp++ - *wp] = '\0';
		}
		if (x_bind(up ? up : *wp, cp, macro, 0))
			rv = 1;
		afreechk(up);
	}

	return rv;
}

/* A leading = means assignments before command are kept;
 * a leading * means a POSIX special builtin;
 * a leading + means a POSIX regular builtin
 * (* and + should not be combined).
 */
const struct builtin kshbuiltins [] = {
	{"+alias", c_alias},	/* no =: at&t manual wrong */
	{"+cd", c_cd},
	{"+command", c_command},
	{"echo", c_print},
	{"*=export", c_typeset},
	{"+fc", c_fc},
	{"+getopts", c_getopts},
	{"+jobs", c_jobs},
	{"+kill", c_kill},
	{"let", c_let},
	{"print", c_print},
	{"pwd", c_pwd},
	{"*=readonly", c_typeset},
	{"=typeset", c_typeset},
	{"+unalias", c_unalias},
	{"whence", c_whence},
	{"+bg", c_fgbg},
	{"+fg", c_fgbg},
	{"bind", c_bind},
	{NULL, (int (*)(const char **))NULL}
};

static void p_time(struct shf *, int, struct timeval *, int,
    const char *, const char *);

/* :, false and true */
int
c_label(const char **wp)
{
	return wp[0][0] == 'f' ? 1 : 0;
}

int
c_shift(const char **wp)
{
	struct block *l = e->loc;
	int n;
	long val;
	const char *arg;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;
	arg = wp[builtin_opt.optind];

	if (arg) {
		evaluate(arg, &val, KSH_UNWIND_ERROR, false);
		n = val;
	} else
		n = 1;
	if (n < 0) {
		bi_errorf("%s: bad number", arg);
		return (1);
	}
	if (l->argc < n) {
		bi_errorf("nothing to shift");
		return (1);
	}
	l->argv[n] = l->argv[0];
	l->argv += n;
	l->argc -= n;
	return 0;
}

int
c_umask(const char **wp)
{
	int i;
	const char *cp;
	int symbolic = 0;
	mode_t old_umask;
	int optc;

	while ((optc = ksh_getopt(wp, &builtin_opt, "S")) != -1)
		switch (optc) {
		case 'S':
			symbolic = 1;
			break;
		case '?':
			return 1;
		}
	cp = wp[builtin_opt.optind];
	if (cp == NULL) {
		old_umask = umask(0);
		umask(old_umask);
		if (symbolic) {
			char buf[18], *p;
			int j;

			old_umask = ~old_umask;
			p = buf;
			for (i = 0; i < 3; i++) {
				*p++ = "ugo"[i];
				*p++ = '=';
				for (j = 0; j < 3; j++)
					if (old_umask & (1 << (8 - (3*i + j))))
						*p++ = "rwx"[j];
				*p++ = ',';
			}
			p[-1] = '\0';
			shprintf("%s\n", buf);
		} else
			shprintf("%#3.3o\n", (unsigned) old_umask);
	} else {
		mode_t new_umask;

		if (ksh_isdigit(*cp)) {
			for (new_umask = 0; *cp >= '0' && *cp <= '7'; cp++)
				new_umask = new_umask * 8 + (*cp - '0');
			if (*cp) {
				bi_errorf("bad number");
				return 1;
			}
		} else {
			/* symbolic format */
			int positions, new_val;
			char op;

			old_umask = umask(0);
			umask(old_umask); /* in case of error */
			old_umask = ~old_umask;
			new_umask = old_umask;
			positions = 0;
			while (*cp) {
				while (*cp && vstrchr("augo", *cp))
					switch (*cp++) {
					case 'a':
						positions |= 0111;
						break;
					case 'u':
						positions |= 0100;
						break;
					case 'g':
						positions |= 0010;
						break;
					case 'o':
						positions |= 0001;
						break;
					}
				if (!positions)
					positions = 0111; /* default is a */
				if (!vstrchr("=+-", op = *cp))
					break;
				cp++;
				new_val = 0;
				while (*cp && vstrchr("rwxugoXs", *cp))
					switch (*cp++) {
					case 'r': new_val |= 04; break;
					case 'w': new_val |= 02; break;
					case 'x': new_val |= 01; break;
					case 'u': new_val |= old_umask >> 6;
						  break;
					case 'g': new_val |= old_umask >> 3;
						  break;
					case 'o': new_val |= old_umask >> 0;
						  break;
					case 'X': if (old_umask & 0111)
							new_val |= 01;
						  break;
					case 's': /* ignored */
						  break;
					}
				new_val = (new_val & 07) * positions;
				switch (op) {
				case '-':
					new_umask &= ~new_val;
					break;
				case '=':
					new_umask = new_val |
					    (new_umask & ~(positions * 07));
					break;
				case '+':
					new_umask |= new_val;
				}
				if (*cp == ',') {
					positions = 0;
					cp++;
				} else if (!vstrchr("=+-", *cp))
					break;
			}
			if (*cp) {
				bi_errorf("bad mask");
				return 1;
			}
			new_umask = ~new_umask;
		}
		umask(new_umask);
	}
	return 0;
}

int
c_dot(const char **wp)
{
	const char *file;
	const char *cp;
	const char **argv;
	int argc;
	int i;
	int err;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;

	if ((cp = wp[builtin_opt.optind]) == NULL)
		return 0;
	file = search(cp, path, R_OK, &err);
	if (file == NULL) {
		bi_errorf("%s: %s", cp, err ? strerror(err) : "not found");
		return 1;
	}

	/* Set positional parameters? */
	if (wp[builtin_opt.optind + 1]) {
		argv = wp + builtin_opt.optind;
		argv[0] = e->loc->argv[0]; /* preserve $0 */
		for (argc = 0; argv[argc + 1]; argc++)
			;
	} else {
		argc = 0;
		argv = NULL;
	}
	i = include(file, argc, argv, 0);
	if (i < 0) { /* should not happen */
		bi_errorf("%s: %s", cp, strerror(errno));
		return 1;
	}
	return i;
}

int
c_wait(const char **wp)
{
	int rv = 0;
	int sig;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;
	wp += builtin_opt.optind;
	if (*wp == NULL) {
		while (waitfor(NULL, &sig) >= 0)
			;
		rv = sig;
	} else {
		for (; *wp; wp++)
			rv = waitfor(*wp, &sig);
		if (rv < 0)
			rv = sig ? sig : 127; /* magic exit code: bad job-id */
	}
	return rv;
}

int
c_read(const char **wp)
{
	int c = 0;
	int expande = 1, historyr = 0;
	int expanding;
	int ecode = 0;
	const char *cp;
	char *ccp;
	int fd = 0;
	struct shf *shf;
	int optc;
	const char *emsg;
	XString cs, xs = { NULL, NULL, 0, NULL};
	struct tbl *vp;
	char *xp = NULL, *wpalloc = NULL;
	static char REPLY[] = "REPLY";

	while ((optc = ksh_getopt(wp, &builtin_opt, "prsu,")) != -1)
		switch (optc) {
		case 'p':
			if ((fd = coproc_getfd(R_OK, &emsg)) < 0) {
				bi_errorf("-p: %s", emsg);
				return 1;
			}
			break;
		case 'r':
			expande = 0;
			break;
		case 's':
			historyr = 1;
			break;
		case 'u':
			if (!*(cp = builtin_opt.optarg))
				fd = 0;
			else if ((fd = check_fd(cp, R_OK, &emsg)) < 0) {
				bi_errorf("-u: %s: %s", cp, emsg);
				return 1;
			}
			break;
		case '?':
			return 1;
		}
	wp += builtin_opt.optind;

	if (*wp == NULL)
		*--wp = REPLY;

	/* Since we can't necessarily seek backwards on non-regular files,
	 * don't buffer them so we can't read too much.
	 */
	shf = shf_reopen(fd, SHF_RD | SHF_INTERRUPT | can_seek(fd), shl_spare);

	if ((cp = cstrchr(*wp, '?')) != NULL) {
		wpalloc = str_save(*wp, ATEMP);
		wpalloc[cp - *wp] = '\0';
		*wp = wpalloc;
		if (isatty(fd)) {
			/* at&t ksh says it prints prompt on fd if it's open
			 * for writing and is a tty, but it doesn't do it
			 * (it also doesn't check the interactive flag,
			 * as is indicated in the Kornshell book).
			 */
			shellf("%s", cp+1);
		}
	}

	/* If we are reading from the co-process for the first time,
	 * make sure the other side of the pipe is closed first.  This allows
	 * the detection of eof.
	 *
	 * This is not compatible with at&t ksh... the fd is kept so another
	 * coproc can be started with same output, however, this means eof
	 * can't be detected...  This is why it is closed here.
	 * If this call is removed, remove the eof check below, too.
	 * coproc_readw_close(fd);
	 */

	if (historyr)
		Xinit(xs, xp, 128, ATEMP);
	expanding = 0;
	Xinit(cs, ccp, 128, ATEMP);
	for (; *wp != NULL; wp++) {
		for (ccp = Xstring(cs, ccp); ; ) {
			if (c == '\n' || c == EOF)
				break;
			while (1) {
				c = shf_getc(shf);
				if (c == '\0')
					continue;
				if (c == EOF && shf_error(shf) &&
				    shf_errno(shf) == EINTR) {
					/* Was the offending signal one that
					 * would normally kill a process?
					 * If so, pretend the read was killed.
					 */
					ecode = fatal_trap_check();

					/* non fatal (eg, CHLD), carry on */
					if (!ecode) {
						shf_clearerr(shf);
						continue;
					}
				}
				break;
			}
			if (historyr) {
				Xcheck(xs, xp);
				Xput(xs, xp, c);
			}
			Xcheck(cs, ccp);
			if (expanding) {
				expanding = 0;
				if (c == '\n') {
					c = 0;
					if (Flag(FTALKING_I) && isatty(fd)) {
						/* set prompt in case this is
						 * called from .profile or $ENV
						 */
						set_prompt(PS2, NULL);
						pprompt(prompt, 0);
					}
				} else if (c != EOF)
					Xput(cs, ccp, c);
				continue;
			}
			if (expande && c == '\\') {
				expanding = 1;
				continue;
			}
			if (c == '\n' || c == EOF)
				break;
			if (ctype(c, C_IFS)) {
				if (Xlength(cs, ccp) == 0 && ctype(c, C_IFSWS))
					continue;
				if (wp[1])
					break;
			}
			Xput(cs, ccp, c);
		}
		/* strip trailing IFS white space from last variable */
		if (!wp[1])
			while (Xlength(cs, ccp) && ctype(ccp[-1], C_IFS) &&
			    ctype(ccp[-1], C_IFSWS))
				ccp--;
		Xput(cs, ccp, '\0');
		vp = global(*wp);
		/* Must be done before setting export. */
		if (vp->flag & RDONLY) {
			shf_flush(shf);
			bi_errorf("%s is read only", *wp);
			afreechk(wpalloc);
			return 1;
		}
		if (Flag(FEXPORT))
			typeset(*wp, EXPORT, 0, 0, 0);
		if (!setstr(vp, Xstring(cs, ccp), KSH_RETURN_ERROR)) {
			shf_flush(shf);
			afreechk(wpalloc);
			return 1;
		}
	}

	shf_flush(shf);
	if (historyr) {
		Xput(xs, xp, '\0');
		source->line++;
		histsave(source->line, Xstring(xs, xp), 1);
		Xfree(xs, xp);
	}
	/* if this is the co-process fd, close the file descriptor
	 * (can get eof if and only if all processes are have died, ie,
	 * coproc.njobs is 0 and the pipe is closed).
	 */
	if (c == EOF && !ecode)
		coproc_read_close(fd);

	afreechk(wpalloc);
	return ecode ? ecode : c == EOF;
}

int
c_eval(const char **wp)
{
	struct source *s, *saves = source;
	char savef;
	int rv;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;
	s = pushs(SWORDS, ATEMP);
	s->u.strv = wp + builtin_opt.optind;

	/*
	 * Handle case where the command is empty due to failed
	 * command substitution, eg, eval "$(false)".
	 * In this case, shell() will not set/change exstat (because
	 * compiled tree is empty), so will use this value.
	 * subst_exstat is cleared in execute(), so should be 0 if
	 * there were no substitutions.
	 *
	 * A strict reading of POSIX says we don't do this (though
	 * it is traditionally done). [from 1003.2-1992]
	 *    3.9.1: Simple Commands
	 *	... If there is a command name, execution shall
	 *	continue as described in 3.9.1.1.  If there
	 *	is no command name, but the command contained a command
	 *	substitution, the command shall complete with the exit
	 *	status of the last command substitution
	 *    3.9.1.1: Command Search and Execution
	 *	...(1)...(a) If the command name matches the name of
	 *	a special built-in utility, that special built-in
	 *	utility shall be invoked.
	 * 3.14.5: Eval
	 *	... If there are no arguments, or only null arguments,
	 *	eval shall return an exit status of zero.
	 */
	exstat = subst_exstat;

	savef = Flag(FERREXIT);
	Flag(FERREXIT) = 0;
	rv = shell(s, false);
	Flag(FERREXIT) = savef;
	source = saves;
	return (rv);
}

int
c_trap(const char **wp)
{
	int i;
	const char *s;
	Trap *p;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;
	wp += builtin_opt.optind;

	if (*wp == NULL) {
		for (p = sigtraps, i = NSIG+1; --i >= 0; p++)
			if (p->trap != NULL) {
				shprintf("trap -- ");
				print_value_quoted(p->trap);
				shprintf(" %s\n", p->name);
			}
		return 0;
	}

	/*
	 * Use case sensitive lookup for first arg so the
	 * command 'exit' isn't confused with the pseudo-signal
	 * 'EXIT'.
	 */
	s = (gettrap(*wp, false) == NULL) ? *wp++ : NULL; /* get command */
	if (s != NULL && s[0] == '-' && s[1] == '\0')
		s = NULL;

	/* set/clear traps */
	while (*wp != NULL) {
		p = gettrap(*wp++, true);
		if (p == NULL) {
			bi_errorf("bad signal %s", wp[-1]);
			return 1;
		}
		settrap(p, s);
	}
	return 0;
}

int
c_exitreturn(const char **wp)
{
	int how = LEXIT;
	int n;
	const char *arg;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;
	arg = wp[builtin_opt.optind];

	if (arg) {
		if (!getn(arg, &n)) {
			exstat = 1;
			warningf(true, "%s: bad number", arg);
		} else
			exstat = n;
	}
	if (wp[0][0] == 'r') { /* return */
		struct env *ep;

		/* need to tell if this is exit or return so trap exit will
		 * work right (POSIX)
		 */
		for (ep = e; ep; ep = ep->oenv)
			if (STOP_RETURN(ep->type)) {
				how = LRETURN;
				break;
			}
	}

	if (how == LEXIT && !really_exit && j_stopped_running()) {
		really_exit = 1;
		how = LSHELL;
	}

	quitenv(NULL);	/* get rid of any i/o redirections */
	unwind(how);
	/* NOTREACHED */
}

int
c_brkcont(const char **wp)
{
	int n, quit;
	struct env *ep, *last_ep = NULL;
	const char *arg;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;
	arg = wp[builtin_opt.optind];

	if (!arg)
		n = 1;
	else if (!bi_getn(arg, &n))
		return 1;
	quit = n;
	if (quit <= 0) {
		/* at&t ksh does this for non-interactive shells only - weird */
		bi_errorf("%s: bad value", arg);
		return 1;
	}

	/* Stop at E_NONE, E_PARSE, E_FUNC, or E_INCL */
	for (ep = e; ep && !STOP_BRKCONT(ep->type); ep = ep->oenv)
		if (ep->type == E_LOOP) {
			if (--quit == 0)
				break;
			ep->flags |= EF_BRKCONT_PASS;
			last_ep = ep;
		}

	if (quit) {
		/* at&t ksh doesn't print a message - just does what it
		 * can.  We print a message 'cause it helps in debugging
		 * scripts, but don't generate an error (ie, keep going).
		 */
		if (n == quit) {
			warningf(true, "%s: cannot %s", wp[0], wp[0]);
			return 0;
		}
		/* POSIX says if n is too big, the last enclosing loop
		 * shall be used.  Doesn't say to print an error but we
		 * do anyway 'cause the user messed up.
		 */
		if (last_ep)
			last_ep->flags &= ~EF_BRKCONT_PASS;
		warningf(true, "%s: can only %s %d level(s)",
		    wp[0], wp[0], n - quit);
	}

	unwind(*wp[0] == 'b' ? LBREAK : LCONTIN);
	/* NOTREACHED */
}

int
c_set(const char **wp)
{
	int argi, setargs;
	struct block *l = e->loc;
	const char **owp = wp;

	if (wp[1] == NULL) {
		static const char *args [] = { "set", "-", NULL };
		return c_typeset(args);
	}

	argi = parse_args(wp, OF_SET, &setargs);
	if (argi < 0)
		return 1;
	/* set $# and $* */
	if (setargs) {
		owp = wp += argi - 1;
		wp[0] = l->argv[0]; /* save $0 */
		while (*++wp != NULL)
			*wp = str_save(*wp, &l->area);
		l->argc = wp - owp - 1;
		l->argv = (const char **) alloc(sizeofN(char *, l->argc+2),
		     &l->area);
		for (wp = l->argv; (*wp++ = *owp++) != NULL; )
			;
	}
	/* POSIX says set exit status is 0, but old scripts that use
	 * getopt(1), use the construct: set -- $(getopt ab:c "$@")
	 * which assumes the exit value set will be that of the $()
	 * (subst_exstat is cleared in execute() so that it will be 0
	 * if there are no command substitutions).
	 */
	return subst_exstat;
}

int
c_unset(const char **wp)
{
	const char *id;
	int optc, unset_var = 1;
	int ret = 0;

	while ((optc = ksh_getopt(wp, &builtin_opt, "fv")) != -1)
		switch (optc) {
		case 'f':
			unset_var = 0;
			break;
		case 'v':
			unset_var = 1;
			break;
		case '?':
			return 1;
		}
	wp += builtin_opt.optind;
	for (; (id = *wp) != NULL; wp++)
		if (unset_var) {	/* unset variable */
			struct tbl *vp = global(id);

			if (!(vp->flag & ISSET))
			    ret = 1;
			if ((vp->flag&RDONLY)) {
				bi_errorf("%s is read only", vp->name);
				return 1;
			}
			unset(vp, vstrchr(id, '[') ? 1 : 0);
		} else {		/* unset function */
			if (define(id, (struct op *) NULL))
				ret = 1;
		}
	return ret;
}

static void
p_time(struct shf *shf, int posix, struct timeval *tv, int width,
    const char *prefix, const char *suffix)
{
	if (posix)
		shf_fprintf(shf, "%s%*ld.%02d%s", prefix ? prefix : "",
		    width, (long)tv->tv_sec, (int)tv->tv_usec / 10000, suffix);
	else
		shf_fprintf(shf, "%s%*ldm%d.%02ds%s", prefix ? prefix : "",
		    width, (long)tv->tv_sec / 60, (int)tv->tv_sec % 60,
		    (int)tv->tv_usec / 10000, suffix);
}

int
c_times(const char **wp __unused)
{
	struct rusage usage;

#ifdef RUSAGE_SELF
	(void) getrusage(RUSAGE_SELF, &usage);
	p_time(shl_stdout, 0, &usage.ru_utime, 0, NULL, " ");
	p_time(shl_stdout, 0, &usage.ru_stime, 0, NULL, "\n");
#endif

#ifdef RUSAGE_CHILDREN
	(void) getrusage(RUSAGE_CHILDREN, &usage);
	p_time(shl_stdout, 0, &usage.ru_utime, 0, NULL, " ");
	p_time(shl_stdout, 0, &usage.ru_stime, 0, NULL, "\n");
#endif

	return 0;
}

/*
 * time pipeline (really a statement, not a built-in command)
 */
int
timex(struct op *t, int f)
{
#define TF_NOARGS	BIT(0)
#define TF_NOREAL	BIT(1)		/* don't report real time */
#define TF_POSIX	BIT(2)		/* report in posix format */
#if !defined(RUSAGE_SELF) || !defined(RUSAGE_CHILDREN)
	return (0);
#else
	int rv = 0;
	struct rusage ru0, ru1, cru0, cru1;
	struct timeval usrtime, systime, tv0, tv1;
	int tf = 0;
	char opts[1];

	gettimeofday(&tv0, NULL);
	getrusage(RUSAGE_SELF, &ru0);
	getrusage(RUSAGE_CHILDREN, &cru0);
	if (t->left) {
		/*
		 * Two ways of getting cpu usage of a command: just use t0
		 * and t1 (which will get cpu usage from other jobs that
		 * finish while we are executing t->left), or get the
		 * cpu usage of t->left. at&t ksh does the former, while
		 * pdksh tries to do the later (the j_usrtime hack doesn't
		 * really work as it only counts the last job).
		 */
		timerclear(&j_usrtime);
		timerclear(&j_systime);
		if (t->left->type == TCOM)
			t->left->str = opts;
		opts[0] = 0;
		rv = execute(t->left, f | XTIME);
		tf |= opts[0];
		gettimeofday(&tv1, NULL);
		getrusage(RUSAGE_SELF, &ru1);
		getrusage(RUSAGE_CHILDREN, &cru1);
	} else
		tf = TF_NOARGS;

	if (tf & TF_NOARGS) { /* ksh93 - report shell times (shell+kids) */
		tf |= TF_NOREAL;
		timeradd(&ru0.ru_utime, &cru0.ru_utime, &usrtime);
		timeradd(&ru0.ru_stime, &cru0.ru_stime, &systime);
	} else {
		timersub(&ru1.ru_utime, &ru0.ru_utime, &usrtime);
		timeradd(&usrtime, &j_usrtime, &usrtime);
		timersub(&ru1.ru_stime, &ru0.ru_stime, &systime);
		timeradd(&systime, &j_systime, &systime);
	}

	if (!(tf & TF_NOREAL)) {
		timersub(&tv1, &tv0, &tv1);
		if (tf & TF_POSIX)
			p_time(shl_out, 1, &tv1, 5, "real ", "\n");
		else
			p_time(shl_out, 0, &tv1, 5, NULL, " real ");
	}
	if (tf & TF_POSIX)
		p_time(shl_out, 1, &usrtime, 5, "user ", "\n");
	else
		p_time(shl_out, 0, &usrtime, 5, NULL, " user ");
	if (tf & TF_POSIX)
		p_time(shl_out, 1, &systime, 5, "sys  ", "\n");
	else
		p_time(shl_out, 0, &systime, 5, NULL, " system\n");
	shf_flush(shl_out);

	return (rv);
#endif
}

void
timex_hook(struct op *t, char **volatile *app)
{
	char **wp = *app;
	int optc;
	int i, j;
	Getopt opt;

	ksh_getopt_reset(&opt, 0);
	opt.optind = 0;	/* start at the start */
	while ((optc = ksh_getopt((const char **)wp, &opt, ":p")) != -1)
		switch (optc) {
		case 'p':
			t->str[0] |= TF_POSIX;
			break;
		case '?':
			errorf("time: -%s unknown option", opt.optarg);
		case ':':
			errorf("time: -%s requires an argument",
			    opt.optarg);
		}
	/* Copy command words down over options. */
	if (opt.optind != 0) {
		for (i = 0; i < opt.optind; i++)
			afree(wp[i], ATEMP);
		for (i = 0, j = opt.optind; (wp[i] = wp[j]); i++, j++)
			;
	}
	if (!wp[0])
		t->str[0] |= TF_NOARGS;
	*app = wp;
}

/* exec with no args - args case is taken care of in comexec() */
int
c_exec(const char **wp __unused)
{
	int i;

	/* make sure redirects stay in place */
	if (e->savefd != NULL) {
		for (i = 0; i < NUFILE; i++) {
			if (e->savefd[i] > 0)
				close(e->savefd[i]);
			/* For ksh keep anything > 2 private */
			if (i > 2 && e->savefd[i])
				fcntl(i, F_SETFD, FD_CLOEXEC);
		}
		e->savefd = NULL;
	}
	return 0;
}

#if !defined(MKSH_SMALL) || defined(MKSH_NEED_MKNOD)
static int
c_mknod(const char **wp)
{
	int argc, optc, rv = 0;
	bool ismkfifo = false;
	const char **argv;
	void *set = NULL;
	mode_t mode = 0, oldmode = 0;

	while ((optc = ksh_getopt(wp, &builtin_opt, "m:")) != -1) {
		switch (optc) {
		case 'm':
			set = setmode(builtin_opt.optarg);
			if (set == NULL) {
				bi_errorf("invalid file mode");
				return (1);
			}
			mode = getmode(set, DEFFILEMODE);
			free(set);
			break;
		default:
			goto c_mknod_usage;
		}
	}
	argv = &wp[builtin_opt.optind];
	if (argv[0] == '\0')
		goto c_mknod_usage;
	for (argc = 0; argv[argc]; argc++)
		;
	if (argc == 2 && argv[1][0] == 'p')
		ismkfifo = true;
	else if (argc != 4 || (argv[1][0] != 'b' && argv[1][0] != 'c'))
		goto c_mknod_usage;

	if (set != NULL)
		oldmode = umask(0);
	else
		mode = DEFFILEMODE;

	mode |= (argv[1][0] == 'b') ? S_IFBLK :
	    (argv[1][0] == 'c') ? S_IFCHR : 0;

	if (!ismkfifo) {
		unsigned long majnum, minnum;
		dev_t dv;
		char *c;

		majnum = strtoul(argv[2], &c, 0);
		if ((c == argv[2]) || (*c != '\0')) {
			bi_errorf("non-numeric device major '%s'", argv[2]);
			goto c_mknod_err;
		}
		minnum = strtoul(argv[3], &c, 0);
		if ((c == argv[3]) || (*c != '\0')) {
			bi_errorf("non-numeric device minor '%s'", argv[3]);
			goto c_mknod_err;
		}
		dv = makedev(majnum, minnum);
		if ((unsigned long)major(dv) != majnum) {
			bi_errorf("device major too large: %lu", majnum);
			goto c_mknod_err;
		}
		if ((unsigned long)minor(dv) != minnum) {
			bi_errorf("device minor too large: %lu", minnum);
			goto c_mknod_err;
		}
		if (mknod(argv[0], mode, dv))
			goto c_mknod_failed;
	} else if (mkfifo(argv[0], mode)) {
 c_mknod_failed:
		bi_errorf("%s: %s", *wp, strerror(errno));
 c_mknod_err:
		rv = 1;
	}

	if (set)
		umask(oldmode);
	return (rv);
 c_mknod_usage:
	bi_errorf("usage: mknod [-m mode] name [b | c] major minor");
	bi_errorf("usage: mknod [-m mode] name p");
	return (1);
}
#endif

/* dummy function, special case in comexec() */
int
c_builtin(const char **wp __unused)
{
	return 0;
}

/* A leading = means assignments before command are kept;
 * a leading * means a POSIX special builtin;
 * a leading + means a POSIX regular builtin
 * (* and + should not be combined).
 */
const struct builtin shbuiltins [] = {
	{"*=.", c_dot},
	{"*=:", c_label},
	{"[", c_test},
	{"*=break", c_brkcont},
	{"=builtin", c_builtin},
	{"*=continue", c_brkcont},
	{"*=eval", c_eval},
	{"*=exec", c_exec},
	{"*=exit", c_exitreturn},
	{"+false", c_label},
	{"*=return", c_exitreturn},
	{"*=set", c_set},
	{"*=shift", c_shift},
	{"=times", c_times},
	{"*=trap", c_trap},
	{"+=wait", c_wait},
	{"+read", c_read},
	{"test", c_test},
	{"+true", c_label},
	{"ulimit", c_ulimit},
	{"+umask", c_umask},
	{"*=unset", c_unset},
#if !defined(MKSH_SMALL) || defined(MKSH_NEED_MKNOD)
	{"mknod", c_mknod},
#endif
	{NULL, (int (*)(const char **))NULL}
};

/* test(1) accepts the following grammar:
	oexpr	::= aexpr | aexpr "-o" oexpr ;
	aexpr	::= nexpr | nexpr "-a" aexpr ;
	nexpr	::= primary | "!" nexpr ;
	primary	::= unary-operator operand
		| operand binary-operator operand
		| operand
		| "(" oexpr ")"
		;

	unary-operator ::= "-a"|"-r"|"-w"|"-x"|"-e"|"-f"|"-d"|"-c"|"-b"|"-p"|
			   "-u"|"-g"|"-k"|"-s"|"-t"|"-z"|"-n"|"-o"|"-O"|"-G"|
			   "-L"|"-h"|"-S"|"-H";

	binary-operator ::= "="|"=="|"!="|"-eq"|"-ne"|"-ge"|"-gt"|"-le"|"-lt"|
			    "-nt"|"-ot"|"-ef"|
			    "<"|">"	# rules used for [[ .. ]] expressions
			    ;
	operand ::= <any thing>
*/

#define T_ERR_EXIT	2	/* POSIX says > 1 for errors */

struct t_op {
	char	op_text[4];
	Test_op	op_num;
};
static const struct t_op u_ops [] = {
	{"-a",	TO_FILAXST },
	{"-b",	TO_FILBDEV },
	{"-c",	TO_FILCDEV },
	{"-d",	TO_FILID },
	{"-e",	TO_FILEXST },
	{"-f",	TO_FILREG },
	{"-G",	TO_FILGID },
	{"-g",	TO_FILSETG },
	{"-h",	TO_FILSYM },
	{"-H",	TO_FILCDF },
	{"-k",	TO_FILSTCK },
	{"-L",	TO_FILSYM },
	{"-n",	TO_STNZE },
	{"-O",	TO_FILUID },
	{"-o",	TO_OPTION },
	{"-p",	TO_FILFIFO },
	{"-r",	TO_FILRD },
	{"-s",	TO_FILGZ },
	{"-S",	TO_FILSOCK },
	{"-t",	TO_FILTT },
	{"-u",	TO_FILSETU },
	{"-w",	TO_FILWR },
	{"-x",	TO_FILEX },
	{"-z",	TO_STZER },
	{"",	TO_NONOP }
};
static const struct t_op b_ops [] = {
	{"=",	TO_STEQL },
	{"==",	TO_STEQL },
	{"!=",	TO_STNEQ },
	{"<",	TO_STLT },
	{">",	TO_STGT },
	{"-eq",	TO_INTEQ },
	{"-ne",	TO_INTNE },
	{"-gt",	TO_INTGT },
	{"-ge",	TO_INTGE },
	{"-lt",	TO_INTLT },
	{"-le",	TO_INTLE },
	{"-ef",	TO_FILEQ },
	{"-nt",	TO_FILNT },
	{"-ot",	TO_FILOT },
	{"",	TO_NONOP }
};

static int	test_eaccess(const char *, int);
static int	test_oexpr(Test_env *, int);
static int	test_aexpr(Test_env *, int);
static int	test_nexpr(Test_env *, int);
static int	test_primary(Test_env *, int);
static int	ptest_isa(Test_env *, Test_meta);
static const char *ptest_getopnd(Test_env *, Test_op, int);
static void	ptest_error(Test_env *, int, const char *);

int
c_test(const char **wp)
{
	int argc;
	int res;
	Test_env te;

	te.flags = 0;
	te.isa = ptest_isa;
	te.getopnd = ptest_getopnd;
	te.eval = test_eval;
	te.error = ptest_error;

	for (argc = 0; wp[argc]; argc++)
		;

	if (strcmp(wp[0], "[") == 0) {
		if (strcmp(wp[--argc], "]") != 0) {
			bi_errorf("missing ]");
			return T_ERR_EXIT;
		}
	}

	te.pos.wp = wp + 1;
	te.wp_end = wp + argc;

	/*
	 * Handle the special cases from POSIX.2, section 4.62.4.
	 * Implementation of all the rules isn't necessary since
	 * our parser does the right thing for the omitted steps.
	 */
	if (argc <= 5) {
		const char **owp = wp;
		int invert = 0;
		Test_op	op;
		const char *opnd1, *opnd2;

		while (--argc >= 0) {
			if ((*te.isa)(&te, TM_END))
				return !0;
			if (argc == 3) {
				opnd1 = (*te.getopnd)(&te, TO_NONOP, 1);
				if ((op = (*te.isa)(&te, TM_BINOP))) {
					opnd2 = (*te.getopnd)(&te, op, 1);
					res = (*te.eval)(&te, op, opnd1,
					    opnd2, 1);
					if (te.flags & TEF_ERROR)
						return T_ERR_EXIT;
					if (invert & 1)
						res = !res;
					return !res;
				}
				/* back up to opnd1 */
				te.pos.wp--;
			}
			if (argc == 1) {
				opnd1 = (*te.getopnd)(&te, TO_NONOP, 1);
				if (strcmp(opnd1, "-t") == 0)
				    break;
				res = (*te.eval)(&te, TO_STNZE, opnd1,
				    NULL, 1);
				if (invert & 1)
					res = !res;
				return !res;
			}
			if ((*te.isa)(&te, TM_NOT)) {
				invert++;
			} else
				break;
		}
		te.pos.wp = owp + 1;
	}

	return test_parse(&te);
}

/*
 * Generic test routines.
 */

Test_op
test_isop(Test_meta meta, const char *s)
{
	char sc1;
	const struct t_op *tbl;

	tbl = meta == TM_UNOP ? u_ops : b_ops;
	if (*s) {
		sc1 = s[1];
		for (; tbl->op_text[0]; tbl++)
			if (sc1 == tbl->op_text[1] && !strcmp(s, tbl->op_text))
				return tbl->op_num;
	}
	return TO_NONOP;
}

int
test_eval(Test_env *te, Test_op op, const char *opnd1, const char *opnd2,
    int do_eval)
{
	int i;
	size_t k;
	struct stat b1, b2;

	if (!do_eval)
		return 0;

	switch ((int)op) {
	/*
	 * Unary Operators
	 */
	case TO_STNZE: /* -n */
		return *opnd1 != '\0';
	case TO_STZER: /* -z */
		return *opnd1 == '\0';
	case TO_OPTION: /* -o */
		if ((i = *opnd1 == '!'))
			opnd1++;
		if ((k = option(opnd1)) == (size_t)-1)
			k = 0;
		else {
			k = Flag(k);
			if (i)
				k = !k;
		}
		return k;
	case TO_FILRD: /* -r */
		return test_eaccess(opnd1, R_OK) == 0;
	case TO_FILWR: /* -w */
		return test_eaccess(opnd1, W_OK) == 0;
	case TO_FILEX: /* -x */
		return test_eaccess(opnd1, X_OK) == 0;
	case TO_FILAXST: /* -a */
	case TO_FILEXST: /* -e */
		return stat(opnd1, &b1) == 0;
	case TO_FILREG: /* -r */
		return stat(opnd1, &b1) == 0 && S_ISREG(b1.st_mode);
	case TO_FILID: /* -d */
		return stat(opnd1, &b1) == 0 && S_ISDIR(b1.st_mode);
	case TO_FILCDEV: /* -c */
		return stat(opnd1, &b1) == 0 && S_ISCHR(b1.st_mode);
	case TO_FILBDEV: /* -b */
		return stat(opnd1, &b1) == 0 && S_ISBLK(b1.st_mode);
	case TO_FILFIFO: /* -p */
		return stat(opnd1, &b1) == 0 && S_ISFIFO(b1.st_mode);
	case TO_FILSYM: /* -h -L */
		return lstat(opnd1, &b1) == 0 && S_ISLNK(b1.st_mode);
	case TO_FILSOCK: /* -S */
		return stat(opnd1, &b1) == 0 && S_ISSOCK(b1.st_mode);
	case TO_FILCDF:/* -H HP context dependent files (directories) */
		return 0;
	case TO_FILSETU: /* -u */
		return stat(opnd1, &b1) == 0 &&
		    (b1.st_mode & S_ISUID) == S_ISUID;
	case TO_FILSETG: /* -g */
		return stat(opnd1, &b1) == 0 &&
		    (b1.st_mode & S_ISGID) == S_ISGID;
	case TO_FILSTCK: /* -k */
#ifdef S_ISVTX
		return stat(opnd1, &b1) == 0 &&
		    (b1.st_mode & S_ISVTX) == S_ISVTX;
#else
		return (0);
#endif
	case TO_FILGZ: /* -s */
		return stat(opnd1, &b1) == 0 && b1.st_size > 0L;
	case TO_FILTT: /* -t */
		if (opnd1 && !bi_getn(opnd1, &i)) {
			te->flags |= TEF_ERROR;
			i = 0;
		} else
			i = isatty(opnd1 ? i : 0);
		return (i);
	case TO_FILUID: /* -O */
		return stat(opnd1, &b1) == 0 && b1.st_uid == ksheuid;
	case TO_FILGID: /* -G */
		return stat(opnd1, &b1) == 0 && b1.st_gid == getegid();
	/*
	 * Binary Operators
	 */
	case TO_STEQL: /* = */
		if (te->flags & TEF_DBRACKET)
			return gmatchx(opnd1, opnd2, false);
		return strcmp(opnd1, opnd2) == 0;
	case TO_STNEQ: /* != */
		if (te->flags & TEF_DBRACKET)
			return !gmatchx(opnd1, opnd2, false);
		return strcmp(opnd1, opnd2) != 0;
	case TO_STLT: /* < */
		return strcmp(opnd1, opnd2) < 0;
	case TO_STGT: /* > */
		return strcmp(opnd1, opnd2) > 0;
	case TO_INTEQ: /* -eq */
	case TO_INTNE: /* -ne */
	case TO_INTGE: /* -ge */
	case TO_INTGT: /* -gt */
	case TO_INTLE: /* -le */
	case TO_INTLT: /* -lt */
		{
			long v1, v2;

			if (!evaluate(opnd1, &v1, KSH_RETURN_ERROR, false) ||
			    !evaluate(opnd2, &v2, KSH_RETURN_ERROR, false)) {
				/* error already printed.. */
				te->flags |= TEF_ERROR;
				return 1;
			}
			switch ((int) op) {
			case TO_INTEQ:
				return v1 == v2;
			case TO_INTNE:
				return v1 != v2;
			case TO_INTGE:
				return v1 >= v2;
			case TO_INTGT:
				return v1 > v2;
			case TO_INTLE:
				return v1 <= v2;
			case TO_INTLT:
				return v1 < v2;
			}
		}
	case TO_FILNT: /* -nt */
		{
			int s2;
			/* ksh88/ksh93 succeed if file2 can't be stated
			 * (subtly different from 'does not exist').
			 */
			return stat(opnd1, &b1) == 0 &&
			    (((s2 = stat(opnd2, &b2)) == 0 &&
			    b1.st_mtime > b2.st_mtime) || s2 < 0);
		}
	case TO_FILOT: /* -ot */
		{
			int s1;
			/* ksh88/ksh93 succeed if file1 can't be stated
			 * (subtly different from 'does not exist').
			 */
			return stat(opnd2, &b2) == 0 &&
			    (((s1 = stat(opnd1, &b1)) == 0 &&
			    b1.st_mtime < b2.st_mtime) || s1 < 0);
		}
	case TO_FILEQ: /* -ef */
		return stat (opnd1, &b1) == 0 && stat (opnd2, &b2) == 0 &&
		    b1.st_dev == b2.st_dev && b1.st_ino == b2.st_ino;
	}
	(*te->error)(te, 0, "internal error: unknown op");
	return 1;
}

/* On most/all unixen, access() says everything is executable for root... */
static int
test_eaccess(const char *pathl, int mode)
{
	int res = access(pathl, mode);

	if (res == 0 && ksheuid == 0 && (mode & X_OK)) {
		struct stat statb;

		if (stat(pathl, &statb) < 0)
			res = -1;
		else if (S_ISDIR(statb.st_mode))
			res = 0;
		else
			res = (statb.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH)) ?
			    0 : -1;
	}
	return res;
}

int
test_parse(Test_env *te)
{
	int res;

	res = test_oexpr(te, 1);

	if (!(te->flags & TEF_ERROR) && !(*te->isa)(te, TM_END))
		(*te->error)(te, 0, "unexpected operator/operand");

	return (te->flags & TEF_ERROR) ? T_ERR_EXIT : !res;
}

static int
test_oexpr(Test_env *te, int do_eval)
{
	int res;

	res = test_aexpr(te, do_eval);
	if (res)
		do_eval = 0;
	if (!(te->flags & TEF_ERROR) && (*te->isa)(te, TM_OR))
		return test_oexpr(te, do_eval) || res;
	return res;
}

static int
test_aexpr(Test_env *te, int do_eval)
{
	int res;

	res = test_nexpr(te, do_eval);
	if (!res)
		do_eval = 0;
	if (!(te->flags & TEF_ERROR) && (*te->isa)(te, TM_AND))
		return test_aexpr(te, do_eval) && res;
	return res;
}

static int
test_nexpr(Test_env *te, int do_eval)
{
	if (!(te->flags & TEF_ERROR) && (*te->isa)(te, TM_NOT))
		return !test_nexpr(te, do_eval);
	return test_primary(te, do_eval);
}

static int
test_primary(Test_env *te, int do_eval)
{
	const char *opnd1, *opnd2;
	int res;
	Test_op op;

	if (te->flags & TEF_ERROR)
		return 0;
	if ((*te->isa)(te, TM_OPAREN)) {
		res = test_oexpr(te, do_eval);
		if (te->flags & TEF_ERROR)
			return 0;
		if (!(*te->isa)(te, TM_CPAREN)) {
			(*te->error)(te, 0, "missing closing paren");
			return 0;
		}
		return res;
	}
	if ((op = (*te->isa)(te, TM_UNOP))) {
		/* unary expression */
		opnd1 = (*te->getopnd)(te, op, do_eval);
		if (!opnd1) {
			(*te->error)(te, -1, "missing argument");
			return 0;
		}

		return (*te->eval)(te, op, opnd1, NULL, do_eval);
	}
	opnd1 = (*te->getopnd)(te, TO_NONOP, do_eval);
	if (!opnd1) {
		(*te->error)(te, 0, "expression expected");
		return 0;
	}
	if ((op = (*te->isa)(te, TM_BINOP))) {
		/* binary expression */
		opnd2 = (*te->getopnd)(te, op, do_eval);
		if (!opnd2) {
			(*te->error)(te, -1, "missing second argument");
			return 0;
		}

		return (*te->eval)(te, op, opnd1, opnd2, do_eval);
	}
	if (te->flags & TEF_DBRACKET) {
		(*te->error)(te, -1, "missing expression operator");
		return 0;
	}
	return (*te->eval)(te, TO_STNZE, opnd1, NULL, do_eval);
}

/*
 * Plain test (test and [ .. ]) specific routines.
 */

/* Test if the current token is a whatever.  Accepts the current token if
 * it is.  Returns 0 if it is not, non-zero if it is (in the case of
 * TM_UNOP and TM_BINOP, the returned value is a Test_op).
 */
static int
ptest_isa(Test_env *te, Test_meta meta)
{
	/* Order important - indexed by Test_meta values */
	static const char *const tokens[] = {
		"-o", "-a", "!", "(", ")"
	};
	int ret;

	if (te->pos.wp >= te->wp_end)
		return meta == TM_END;

	if (meta == TM_UNOP || meta == TM_BINOP)
		ret = test_isop(meta, *te->pos.wp);
	else if (meta == TM_END)
		ret = 0;
	else
		ret = strcmp(*te->pos.wp, tokens[(int) meta]) == 0;

	/* Accept the token? */
	if (ret)
		te->pos.wp++;

	return ret;
}

static const char *
ptest_getopnd(Test_env *te, Test_op op, int do_eval __unused)
{
	if (te->pos.wp >= te->wp_end)
		return op == TO_FILTT ? "1" : NULL;
	return *te->pos.wp++;
}

static void
ptest_error(Test_env *te, int offset, const char *msg)
{
	const char *op = te->pos.wp + offset >= te->wp_end ?
	    NULL : te->pos.wp[offset];

	te->flags |= TEF_ERROR;
	if (op)
		bi_errorf("%s: %s", op, msg);
	else
		bi_errorf("%s", msg);
}

#define SOFT	0x1
#define HARD	0x2

int
c_ulimit(const char **wp)
{
	static const struct limits {
		const char	*name;
		enum { RLIMIT, ULIMIT } which;
		int	gcmd;	/* get command */
		int	scmd;	/* set command (or -1, if no set command) */
		int	factor;	/* multiply by to get rlim_{cur,max} values */
		char	option;
	} limits[] = {
		/* Do not use options -H, -S or -a */
#ifdef RLIMIT_CORE
		{ "coredump(blocks)", RLIMIT, RLIMIT_CORE, RLIMIT_CORE,
		    512, 'c' },
#endif
#ifdef RLIMIT_DATA
		{ "data(KiB)", RLIMIT, RLIMIT_DATA, RLIMIT_DATA,
		    1024, 'd' },
#endif
#ifdef RLIMIT_FSIZE
		{ "file(blocks)", RLIMIT, RLIMIT_FSIZE, RLIMIT_FSIZE,
		    512, 'f' },
#endif
#ifdef RLIMIT_LOCKS
		{ "flocks", RLIMIT, RLIMIT_LOCKS, RLIMIT_LOCKS,
		    -1, 'L' },
#endif
#ifdef RLIMIT_MEMLOCK
		{ "lockedmem(KiB)", RLIMIT, RLIMIT_MEMLOCK, RLIMIT_MEMLOCK,
		    1024, 'l' },
#endif
#ifdef RLIMIT_RSS
		{ "memory(KiB)", RLIMIT, RLIMIT_RSS, RLIMIT_RSS,
		    1024, 'm' },
#endif
#ifdef RLIMIT_NOFILE
		{ "nofiles(descriptors)", RLIMIT, RLIMIT_NOFILE, RLIMIT_NOFILE,
		    1, 'n' },
#endif
#ifdef RLIMIT_NPROC
		{ "processes", RLIMIT, RLIMIT_NPROC, RLIMIT_NPROC,
		    1, 'p' },
#endif
#ifdef RLIMIT_STACK
		{ "stack(KiB)", RLIMIT, RLIMIT_STACK, RLIMIT_STACK,
		    1024, 's' },
#endif
#ifdef RLIMIT_TIME
		{ "humantime(seconds)", RLIMIT, RLIMIT_TIME, RLIMIT_TIME,
		    1, 'T' },
#endif
#ifdef RLIMIT_CPU
		{ "time(cpu-seconds)", RLIMIT, RLIMIT_CPU, RLIMIT_CPU,
		    1, 't' },
#endif
#ifdef RLIMIT_VMEM
		{ "vmemory(KiB)", RLIMIT, RLIMIT_VMEM, RLIMIT_VMEM,
		    1024, 'v' },
#endif
#ifdef RLIMIT_SWAP
		{ "swap(KiB)", RLIMIT, RLIMIT_SWAP, RLIMIT_SWAP,
		    1024, 'w' },
#endif
		{ NULL, 0, 0, 0, 0, 0 }
	};
	static char	opts[3 + NELEM(limits)];
	rlim_t		val = 0;
	int		how = SOFT | HARD;
	const struct limits	*l;
	int		set, all = 0;
	int		optc, what;
	struct rlimit	limit;
	if (!opts[0]) {
		/* build options string on first call - yuck */
		char *p = opts;

		*p++ = 'H'; *p++ = 'S'; *p++ = 'a';
		for (l = limits; l->name; l++)
			*p++ = l->option;
		*p = '\0';
	}
	what = 'f';
	while ((optc = ksh_getopt(wp, &builtin_opt, opts)) != -1)
		switch (optc) {
		case 'H':
			how = HARD;
			break;
		case 'S':
			how = SOFT;
			break;
		case 'a':
			all = 1;
			break;
		case '?':
			return 1;
		default:
			what = optc;
		}

	for (l = limits; l->name && l->option != what; l++)
		;
	if (!l->name) {
		internal_warningf("ulimit: %c", what);
		return 1;
	}

	wp += builtin_opt.optind;
	set = *wp ? 1 : 0;
	if (set) {
		if (all || wp[1]) {
			bi_errorf("too many arguments");
			return 1;
		}
		if (strcmp(wp[0], "unlimited") == 0)
			val = RLIM_INFINITY;
		else {
			long rval;

			if (!evaluate(wp[0], &rval, KSH_RETURN_ERROR, false))
				return 1;
			/* Avoid problems caused by typos that
			 * evaluate misses due to evaluating unset
			 * parameters to 0...
			 * If this causes problems, will have to
			 * add parameter to evaluate() to control
			 * if unset params are 0 or an error.
			 */
			if (!rval && !ksh_isdigit(wp[0][0])) {
				bi_errorf("invalid limit: %s", wp[0]);
				return 1;
			}
			val = (rlim_t)rval * l->factor;
		}
	}
	if (all) {
		for (l = limits; l->name; l++) {
			if (l->which == RLIMIT) {
#ifdef RLIMIT_LOCKS
				if (getrlimit(l->gcmd, &limit) < 0)
					if ((errno == EINVAL) &&
					    (l->gcmd == RLIMIT_LOCKS)) {
						limit.rlim_cur = RLIM_INFINITY;
						limit.rlim_max = RLIM_INFINITY;
					}
#else
				getrlimit(l->gcmd, &limit);
#endif
				if (how & SOFT)
					val = limit.rlim_cur;
				else if (how & HARD)
					val = limit.rlim_max;
			}
			shprintf("%-20s ", l->name);
			if (val == (rlim_t)RLIM_INFINITY)
				shprintf("unlimited\n");
			else {
				val /= l->factor;
				shprintf("%ld\n", (long) val);
			}
		}
		return 0;
	}
	if (l->which == RLIMIT) {
		getrlimit(l->gcmd, &limit);
		if (set) {
			if (how & SOFT)
				limit.rlim_cur = val;
			if (how & HARD)
				limit.rlim_max = val;
			if (setrlimit(l->scmd, &limit) < 0) {
				how = errno;
				if (how == EPERM)
					bi_errorf("exceeds allowable limit");
				else
					bi_errorf("bad limit: %s",
					    strerror(how));
				return 1;
			}
		} else {
			if (how & SOFT)
				val = limit.rlim_cur;
			else if (how & HARD)
				val = limit.rlim_max;
		}
	}
	if (!set) {
		if (val == (rlim_t)RLIM_INFINITY)
			shprintf("unlimited\n");
		else {
			val /= l->factor;
			shprintf("%ld\n", (long) val);
		}
	}
	return (0);
}
