/*
 *
 * FreeBSD install - a package for the installation and maintenance
 * of non-core utilities.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * Jordan K. Hubbard
 * 18 July 1993
 *
 * This is the add module.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: stable/10/usr.sbin/pkg_install/add/main.c 254525 2013-08-19 14:04:35Z gjb $");

#include <sys/param.h>
#include <sys/sysctl.h>
#include <err.h>
#include <getopt.h>

#include "lib.h"
#include "add.h"

char	*Prefix		= NULL;
Boolean	PrefixRecursive	= FALSE;
char	*Chroot		= NULL;
Boolean	NoInstall	= FALSE;
Boolean	NoRecord	= FALSE;
Boolean Remote		= FALSE;
Boolean KeepPackage	= FALSE;
Boolean FailOnAlreadyInstalled	= TRUE;
Boolean IgnoreDeps	= FALSE;

char	*Mode		= NULL;
char	*Owner		= NULL;
char	*Group		= NULL;
char	*PkgName	= NULL;
char	*PkgAddCmd	= NULL;
char	*Directory	= NULL;
char	FirstPen[FILENAME_MAX];
add_mode_t AddMode	= NORMAL;

char	**pkgs;

static void usage(void);

static char opts[] = "hviIRfFnrp:P:SMt:C:K";
static struct option longopts[] = {
	{ "chroot",	required_argument,	NULL,		'C' },
	{ "dry-run",	no_argument,		NULL,		'n' },
	{ "force",	no_argument,		NULL,		'f' },
	{ "help",	no_argument,		NULL,		'h' },
	{ "keep",	no_argument,		NULL,		'K' },
	{ "master",	no_argument,		NULL,		'M' },
	{ "no-deps",	no_argument,		NULL,		'i' },
	{ "no-record",	no_argument,		NULL,		'R' },
	{ "no-script",	no_argument,		NULL,		'I' },
	{ "prefix",	required_argument,	NULL,		'p' },
	{ "remote",	no_argument,		NULL,		'r' },
	{ "template",	required_argument,	NULL,		't' },
	{ "slave",	no_argument,		NULL,		'S' },
	{ "verbose",	no_argument,		NULL,		'v' },
	{ NULL,		0,			NULL,		0 }
};

int
main(int argc, char **argv)
{
    int ch, error;
    char **start;
    char *cp, *remotepkg = NULL;
    static char pkgaddpath[MAXPATHLEN];

    if (*argv[0] != '/' && strchr(argv[0], '/') != NULL)
	PkgAddCmd = realpath(argv[0], pkgaddpath);
    else
	PkgAddCmd = argv[0];

    start = argv;
    while ((ch = getopt_long(argc, argv, opts, longopts, NULL)) != -1) {
	switch(ch) {
	case 'v':
	    Verbose++;
	    break;

	case 'p':
	    Prefix = optarg;
	    PrefixRecursive = FALSE;
	    break;

	case 'P':
	    Prefix = optarg;
	    PrefixRecursive = TRUE;
	    break;

	case 'I':
	    NoInstall = TRUE;
	    break;

	case 'R':
	    NoRecord = TRUE;
	    break;

	case 'f':
	    Force = TRUE;
	    break;

	case 'F':
	    FailOnAlreadyInstalled = FALSE;
	    break;

	case 'K':
	    KeepPackage = TRUE;
	    break;

	case 'n':
	    Fake = TRUE;
	    break;

	case 'r':
	    Remote = TRUE;
	    break;

	case 't':
	    if (strlcpy(FirstPen, optarg, sizeof(FirstPen)) >= sizeof(FirstPen))
		errx(1, "-t Argument too long.");
	    break;

	case 'S':
	    AddMode = SLAVE;
	    break;

	case 'M':
	    AddMode = MASTER;
	    break;

	case 'C':
	    Chroot = optarg;
	    break;

	case 'i':
	    IgnoreDeps = TRUE;
	    break;

	case 'h':
	default:
	    usage();
	    break;
	}
    }
    argc -= optind;
    argv += optind;

    if (AddMode != SLAVE) {
	pkgs = (char **)malloc((argc+1) * sizeof(char *));
	for (ch = 0; ch <= argc; pkgs[ch++] = NULL) ;

	/* Get all the remaining package names, if any */
	for (ch = 0; *argv; ch++, argv++) {
	    char temp[MAXPATHLEN];
    	    if (Remote) {
		err(1, "Remote is not supported yet");
    	    }
	    if (!strcmp(*argv, "-"))	/* stdin? */
		pkgs[ch] = (char *)"-";
	    else if (isURL(*argv)) {  	/* preserve URLs */
		if (strlcpy(temp, *argv, sizeof(temp)) >= sizeof(temp))
		    errx(1, "package name too long");
		pkgs[ch] = strdup(temp);
	    }
	    else if ((Remote) && isURL(remotepkg)) {
	    	if (strlcpy(temp, remotepkg, sizeof(temp)) >= sizeof(temp))
		    errx(1, "package name too long");
		pkgs[ch] = strdup(temp);
	    } else {			/* expand all pathnames to fullnames */
		if (fexists(*argv)) /* refers to a file directly */
		    pkgs[ch] = strdup(realpath(*argv, temp));
		else {		/* look for the file in the expected places */
		    if (!(cp = fileFindByPath(NULL, *argv))) {
			/* let pkg_do() fail later, so that error is reported */
			if (strlcpy(temp, *argv, sizeof(temp)) >= sizeof(temp))
			    errx(1, "package name too long");
			pkgs[ch] = strdup(temp);
		    } else {
			if (strlcpy(temp, cp, sizeof(temp)) >= sizeof(temp))
			    errx(1, "package name too long");
			pkgs[ch] = strdup(temp);
		    }
		}
	    }
	}
    }
    /* If no packages, yelp */
    if (!ch) {
	warnx("missing package name(s)");
	usage();
    }
    else if (ch > 1 && AddMode == MASTER) {
	warnx("only one package name may be specified with master mode");
	usage();
    }
    /* Perform chroot if requested */
    if (Chroot != NULL) {
	if (chdir(Chroot))
	    errx(1, "chdir to %s failed", Chroot);
	if (chroot("."))
	    errx(1, "chroot to %s failed", Chroot);
    }
    /* Make sure the sub-execs we invoke get found */
    setenv("PATH", 
	   "/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin",
	   1);

    /* Set a reasonable umask */
    umask(022);

    if ((error = pkg_perform(pkgs)) != 0) {
	if (Verbose)
	    warnx("%d package addition(s) failed", error);
	return error;
    }
    else
	return 0;
}

static void
usage(void)
{
    fprintf(stderr, "%s\n%s\n",
	"usage: pkg_add [-viInfFrRMSK] [-t template] [-p prefix] [-P prefix] [-C chrootdir]",
	"               pkg-name [pkg-name ...]");
    exit(1);
}
