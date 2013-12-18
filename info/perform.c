/*
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
 * 23 Aug 1993
 *
 * This is the main body of the info module.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: stable/10/usr.sbin/pkg_install/info/perform.c 240682 2012-09-18 22:09:23Z bapt $");

#include "lib.h"
#include "info.h"
#include <err.h>
#include <signal.h>
#include <pkg.h>

static int pkg_do(char *);
static int find_pkg(struct which_head *);
static int cmp_path(const char *, const char *, const char *);
static char *abspath(const char *);
static int find_pkgs_by_origin(const char *);
static int matched_packages(char **pkgs);
struct pkgdb *db;

int
pkg_perform(char **pkgs)
{
    char **matched;
    int err_cnt = 0;
    int i, errcode;

    signal(SIGINT, cleanup);
    if (pkg_init(NULL, NULL))
	errx(1, "Cannot parse configuration file");

    db = NULL;
    if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
	errx(1, "Enable to open pkgdb");

    /* Overriding action? */
    if (Flags & SHOW_PKGNAME) {
	return matched_packages(pkgs);
    } else if (CheckPkg) {
	return isinstalledpkg(CheckPkg) > 0 ? 0 : 1;
	/* Not reached */
    } else if (!TAILQ_EMPTY(whead)) {
	return find_pkg(whead);
    } else if (LookUpOrigin != NULL) {
	return find_pkgs_by_origin(LookUpOrigin);
    }

    if (MatchType != LEGACY_MATCH_EXACT) {
	matched = matchinstalled(MatchType, pkgs, &errcode);
	if (errcode != 0)
	    return 1;
	    /* Not reached */

	if (matched != NULL)
	    pkgs = matched;
	else switch (MatchType) {
	    case LEGACY_MATCH_GLOB:
		break;
	    case LEGACY_MATCH_ALL:
		warnx("no packages installed");
		return 0;
		/* Not reached */
	    case LEGACY_MATCH_REGEX:
	    case LEGACY_MATCH_EREGEX:
		warnx("no packages match pattern(s)");
		return 1;
		/* Not reached */
	    default:
		break;
	}
    }

    for (i = 0; pkgs[i]; i++)
	err_cnt += pkg_do(pkgs[i]);

    pkgdb_close(db);
    pkg_shutdown();
    return err_cnt;
}

static int
pkg_do(char *pkg)
{
    Boolean installed = FALSE, isTMP = FALSE;
    char fname[FILENAME_MAX];
    Package plist;
    struct stat sb;
    const char *cp = NULL;
    int code = 0;
    struct pkg *p;
    struct pkg_dep *d;
    struct pkg_file *f;

    if (isURL(pkg)) {
	if ((cp = fileGetURL(NULL, pkg, KeepPackage)) != NULL) {
	    if (!getcwd(fname, FILENAME_MAX))
		upchuck("getcwd");
	    isTMP = TRUE;
	} else {
	    return (0);
	}
    }
    else if (fexists(pkg) && isfile(pkg)) {
	int len;

	if (*pkg != '/') {
	    if (!getcwd(fname, FILENAME_MAX))
		upchuck("getcwd");
	    len = strlen(fname);
	    snprintf(&fname[len], FILENAME_MAX - len, "/%s", pkg);
	}
	else
	    strcpy(fname, pkg);
	cp = fname;
    }
    else {
	if ((cp = fileFindByPath(NULL, pkg)) != NULL)
	    strncpy(fname, cp, FILENAME_MAX);
    }
    if (cp) {
	if (!isURL(pkg)) {
	    /*
	     * Apply a crude heuristic to see how much space the package will
	     * take up once it's unpacked.  I've noticed that most packages
	     * compress an average of 75%, but we're only unpacking the + files so
	     * be very optimistic.
	     */
	    if (stat(fname, &sb) == FAIL) {
	        warnx("can't stat package file '%s'", fname);
	        code = 1;
	        return (0);
	    }
	    make_playpen(PlayPen, sb.st_size / 2);
	    if (unpack(fname, "'+*'")) {
		warnx("error during unpacking, no info for '%s' available", pkg);
		code = 1;
		return (0);
	    }
	}
    }
    /* It's not an uninstalled package, try and find it among the installed */
    else {
	p = getpkg(pkg);
	if (p == NULL) {
	    warnx("can't find package '%s' installed or in a file!", pkg);
	    return 1;
	}
	installed = TRUE;
    }

    /*
     * Index is special info type that has to override all others to make
     * any sense.
     */
    if (Flags & SHOW_INDEX) {
	char tmp[FILENAME_MAX];

	snprintf(tmp, FILENAME_MAX, "%-19s ", pkg);
	if (!Quiet)
		printf("%s%-19s", InfoPrefix, pkg);
	pkg_printf("%c\n", p);
    }
    else {
	/* Start showing the package contents */
	if (!Quiet)
	    pkg_printf("%SInformation for %n-%v:\n\n", InfoPrefix, p, p);
	else if (QUIET)
	    pkg_printf("%S%n-%v:", InfoPrefix, p, p);
	if (Flags & SHOW_COMMENT) {
	    if (!Quiet)
		    printf("Comment:\n");
	    pkg_printf("%c\n", p);
	}
	if (Flags & SHOW_DEPEND) {
		if (!Quiet)
			printf("%sDepends on\n", InfoPrefix);
	    d = NULL;
	    while (pkg_deps(p, &d) == EPKG_OK)
		    pkg_printf("%dn-%dv\n", d, d);
	}
	if ((Flags & SHOW_REQBY)) {
		if (!Quiet)
			printf("%sRequired by:\n", InfoPrefix);
	    d = NULL;
	    while (pkg_rdeps(p, &d) == EPKG_OK)
		pkg_printf("%rn-%rv\n", d, d);
	}
	if (Flags & SHOW_DESC) {
	    if (!Quiet)
		printf("%sDescription;\n", InfoPrefix);
	    pkg_printf("%e\n", p);
	}
	if ((Flags & SHOW_DISPLAY) && pkg_has_message(p)) {
		if (!Quiet)
			printf("%sInstall notice:\n", InfoPrefix);
		pkg_printf("%M\n", p);
	}
	if (Flags & SHOW_PLIST) {
	    char *out = NULL;
	    pkg_to_old(p);
	    pkg_old_emit_content(p, &out);
	    if (!Quiet)
	        printf("%sPacking List:\n", InfoPrefix);
	    printf("%s\n", out);
	    free(out);
	}
	if (Flags & SHOW_REQUIRE && fexists(REQUIRE_FNAME))
	    show_file("Requirements script:\n", REQUIRE_FNAME);
	if ((Flags & SHOW_INSTALL) && fexists(INSTALL_FNAME))
	    show_file("Install script:\n", INSTALL_FNAME);
	if ((Flags & SHOW_INSTALL) && fexists(POST_INSTALL_FNAME))
	    show_file("Post-Install script:\n", POST_INSTALL_FNAME);
	if ((Flags & SHOW_DEINSTALL) && fexists(DEINSTALL_FNAME))
	    show_file("De-Install script:\n", DEINSTALL_FNAME);
	if ((Flags & SHOW_DEINSTALL) && fexists(POST_DEINSTALL_FNAME))
	    show_file("Post-DeInstall script:\n", POST_DEINSTALL_FNAME);
	if ((Flags & SHOW_MTREE) && fexists(MTREE_FNAME))
	    show_file("mtree file:\n", MTREE_FNAME);
	if (Flags & SHOW_PREFIX) {
	    if (!Quiet)
	        printf("%sPrefix(s):\n", InfoPrefix);
	    pkg_printf("%p\n", InfoPrefix, p);
	}
	if (Flags & SHOW_FILES) {
	    if (!Quiet)
		    printf("%sFiles:\n", InfoPrefix);
	    f = NULL;
	    while (pkg_files(p, &f) == EPKG_OK)
		pkg_printf("%Fn\n", f);
	}
	if ((Flags & SHOW_SIZE) && installed) {
	    if (!Quiet)
		printf("%sPackage SIze;\n", InfoPrefix);
	    pkg_printf("%s\n", p);
	}
	if ((Flags & SHOW_CKSUM) && installed)
	    code += show_cksum("Mismatched Checksums:\n", &plist);
	if (Flags & SHOW_ORIGIN) {
	    if (!Quiet)
	       printf("%sOrigin:\n", InfoPrefix);
	    pkg_printf("%o\n", p);
	}
	if (Flags & SHOW_FMTREV) {
	    if (!Quiet)
		printf("%sPacking list format revision:\n", InfoPrefix);
	    printf("1.1\n");
	}
	if (!Quiet)
	    puts(InfoPrefix);
    }
    return (code ? 1 : 0);
}

void
cleanup(int sig)
{
    static int in_cleanup = 0;

    if (!in_cleanup) {
	in_cleanup = 1;
	leave_playpen();
    }
    if (sig)
	exit(1);
}

/*
 * Return an absolute path, additionally removing all .'s, ..'s, and extraneous
 * /'s, as realpath() would, but without resolving symlinks, because that can
 * potentially screw up our comparisons later.
 */
static char *
abspath(const char *pathname)
{
    char *tmp, *tmp1, *resolved_path;
    char *cwd = NULL;
    int len;

    if (pathname[0] != '/') {
	cwd = getcwd(NULL, MAXPATHLEN);
	asprintf(&resolved_path, "%s/%s/", cwd, pathname);
    } else
	asprintf(&resolved_path, "%s/", pathname);

    if (resolved_path == NULL)
	errx(2, NULL);

    if (cwd != NULL)
	free(cwd);    

    while ((tmp = strstr(resolved_path, "//")) != NULL)
	strcpy(tmp, tmp + 1);
 
    while ((tmp = strstr(resolved_path, "/./")) != NULL)
	strcpy(tmp, tmp + 2);
 
    while ((tmp = strstr(resolved_path, "/../")) != NULL) {
	*tmp = '\0';
	if ((tmp1 = strrchr(resolved_path, '/')) == NULL)
	   tmp1 = resolved_path;
	strcpy(tmp1, tmp + 3);
    }

    len = strlen(resolved_path);
    if (len > 1 && resolved_path[len - 1] == '/')
	resolved_path[len - 1] = '\0';

    return resolved_path;
}

/*
 * Comparison to see if the path we're on matches the
 * one we are looking for.
 */
static int
cmp_path(const char *target, const char *current, const char *cwd) 
{
    char *resolved, *temp;
    int rval;

    asprintf(&temp, "%s/%s", cwd, current);
    if (temp == NULL)
        errx(2, NULL);

    /*
     * Make sure there's no multiple /'s or other weird things in the PLIST,
     * since some plists seem to have them and it could screw up our strncmp.
     */
    resolved = abspath(temp);

    if (strcmp(target, resolved) == 0)
	rval = 1;
    else
	rval = 0;

    free(temp);
    free(resolved);
    return rval;
}

/* 
 * Look through package dbs in LOG_DIR and find which
 * packages installed the files in which_list.
 */
static int 
find_pkg(struct which_head *which_list)
{
    char **installed;
    int errcode, i;
    struct which_entry *wp;

    TAILQ_FOREACH(wp, which_list, next) {
	const char *msg = "file cannot be found";
	char *tmp;

	wp->skip = TRUE;
	/* If it's not a file, we'll see if it's an executable. */
	if (isfile(wp->file) == FALSE) {
	    if (strchr(wp->file, '/') == NULL) {
		tmp = vpipe("/usr/bin/which %s", wp->file);
		if (tmp != NULL) {
		    strlcpy(wp->file, tmp, PATH_MAX);
		    wp->skip = FALSE;
		    free(tmp);
		} else
		    msg = "file is not in PATH";
	    }
	} else {
	    tmp = abspath(wp->file);
	    if (isfile(tmp)) {
	    	strlcpy(wp->file, tmp, PATH_MAX);
	    	wp->skip = FALSE;
	    }
	    free(tmp);
	}
	if (wp->skip == TRUE)
	    warnx("%s: %s", wp->file, msg);
    }

    installed = matchinstalled(LEGACY_MATCH_ALL, NULL, &errcode);
    if (installed == NULL)
        return errcode;
 
    for (i = 0; installed[i] != NULL; i++) {
     	FILE *fp;
     	Package pkg;
     	PackingList itr;
     	char *cwd = NULL;
     	char tmp[PATH_MAX];

	snprintf(tmp, PATH_MAX, "%s/%s/%s", LOG_DIR, installed[i],
		 CONTENTS_FNAME);
	fp = fopen(tmp, "r");
	if (fp == NULL) {
	    warn("%s", tmp);
	    return 1;
	}

	pkg.head = pkg.tail = NULL;
	read_plist(&pkg, fp);
	fclose(fp);
	for (itr = pkg.head; itr != pkg.tail; itr = itr->next) {
	    if (itr->type == PLIST_CWD) {
		cwd = itr->name;
	    } else if (itr->type == PLIST_FILE) {
		TAILQ_FOREACH(wp, which_list, next) {
		    if (wp->skip == TRUE)
			continue;
		    if (!cmp_path(wp->file, itr->name, cwd))
			continue;
		    if (wp->package[0] != '\0') {
			warnx("both %s and %s claim to have installed %s\n",
			      wp->package, installed[i], wp->file);
		    } else {
			strlcpy(wp->package, installed[i], PATH_MAX);
		    }
		}
	    }
	}
	free_plist(&pkg);
    }

    TAILQ_FOREACH(wp, which_list, next) {
	if (wp->package[0] != '\0') {
	    if (Quiet)
		puts(wp->package);
	    else
		printf("%s was installed by package %s\n", \
		       wp->file, wp->package);
	}
    }
    while (!TAILQ_EMPTY(which_list)) {
	wp = TAILQ_FIRST(which_list);
	TAILQ_REMOVE(which_list, wp, next);
	free(wp);
    }

    free(which_list);
    return 0;
}

/* 
 * Look through package dbs in LOG_DIR and find which
 * packages have the given origin. Don't use read_plist()
 * because this increases time necessary for lookup by 40
 * times, as we don't really have to parse all plist to
 * get origin.
 */
static int 
find_pkgs_by_origin(const char *origin)
{
    char **matched;
    int errcode, i;

    if (!Quiet)
	printf("The following installed package(s) has %s origin:\n", origin);

    matched = matchbyorigin(origin, &errcode);
    if (matched == NULL)
	return errcode;

    for (i = 0; matched[i] != NULL; i++)
	puts(matched[i]);

    return 0;
}

/*
 * List only the matching package names.
 * Mainly intended for scripts.
 */
static int
matched_packages(char **pkgs)
{
    char **matched;
    int i, errcode;

    matched = matchinstalled(MatchType == LEGACY_MATCH_GLOB ? LEGACY_MATCH_NGLOB : MatchType, pkgs, &errcode);

    if (errcode != 0 || matched == NULL)
	return 1;

    for (i = 0; matched[i]; i++)
	if (!Quiet)
	    printf("%s\n", matched[i]);
	else if (QUIET)
	    printf("%s%s\n", InfoPrefix, matched[i]);

    return 0;
}
