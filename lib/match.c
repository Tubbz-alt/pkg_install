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
 * Maxim Sobolev
 * 24 February 2001
 *
 * Routines used to query installed packages.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: stable/10/usr.sbin/pkg_install/lib/match.c 228990 2011-12-30 10:58:14Z uqs $");

#include "lib.h"
#include <err.h>
#include <fnmatch.h>
#include <fts.h>
#include <regex.h>
#include <pkg.h>

/*
 * Simple structure representing argv-like
 * NULL-terminated list.
 */
struct store {
    int currlen;
    int used;
    char **store;
};

static int rex_match(const char *, const char *, int);
static int csh_match(const char *, const char *, int);
struct store *storecreate(struct store *);
static int storeappend(struct store *, const char *);
static int fname_cmp(const FTSENT * const *, const FTSENT * const *);

/*
 * Function to query names of installed packages.
 * MatchType	- one of LEGACY_MATCH_ALL, LEGACY_MATCH_EREGEX, LEGACY_MATCH_REGEX, LEGACY_MATCH_GLOB, LEGACY_MATCH_NGLOB;
 * patterns	- NULL-terminated list of glob or regex patterns
 *		  (could be NULL for LEGACY_MATCH_ALL);
 * retval	- return value (could be NULL if you don't want/need
 *		  return value).
 * Returns NULL-terminated list with matching names.
 * Names in list returned are dynamically allocated and should
 * not be altered by the caller.
 */
char **
matchinstalled(legacy_match_t MatchType, char **patterns, int *retval)
{
    int len;
    static struct store *store = NULL;
    char pkgname[MAXPATHLEN];
    struct pkgdb_it *it = NULL;
    struct pkg *pkg;

    if (!pkg_initialized())
	return (NULL);

    store = storecreate(store);
    if (store == NULL) {
	if (retval != NULL)
	    *retval = 1;
	return NULL;
    }

    if (retval != NULL)
	*retval = 0;

    if (patterns != NULL && MatchType == LEGACY_MATCH_ALL) {
	    if ((it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL) {
		return (NULL);
	    }

	    pkg = NULL;
	    while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		pkg_snprintf(pkgname, sizeof(pkgname), "%n-%v", pkg, pkg);
		storeappend(store, pkgname);
	    }
	    pkgdb_it_free(it);
    } else if (patterns != NULL) {
	for (len = 0; patterns[len]; len++) {
	    match_t m;
	    switch (MatchType) {
	    case LEGACY_MATCH_EXACT:
		    m = MATCH_EXACT;
		    break;
	    case LEGACY_MATCH_GLOB:
		    m = MATCH_GLOB;
		    break;
	    case LEGACY_MATCH_NGLOB:
		    /* XXX unsupported yet */
		    return (NULL);
	    case LEGACY_MATCH_REGEX:
	    case LEGACY_MATCH_EREGEX:
		    m = MATCH_REGEX;
		    break;
	    case LEGACY_MATCH_ALL:
		    m = MATCH_ALL;
		    break;
	    }
	    if ((it = pkgdb_query(db, patterns[len], m)) == NULL)
		    continue;

	    pkg = NULL;
	    while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		pkg_snprintf(pkgname, sizeof(pkgname), "%n-%v", pkg, pkg);
		storeappend(store, pkgname);
	    }
	    pkgdb_it_free(it);
    	}
    }
    
    if (store->used == 0)
	return NULL;
    else
	return store->store;
}

int
pattern_match(legacy_match_t MatchType, char *pattern, const char *pkgname)
{
    int errcode = 0;
    const char *fname = pkgname;
    char basefname[PATH_MAX];
    char condchar = '\0';
    char *condition;

    /* do we have an appended condition? */
    condition = strpbrk(pattern, "<>=");
    if (condition) {
	const char *ch;
	/* yes, isolate the pattern from the condition ... */
	if (condition > pattern && condition[-1] == '!')
	    condition--;
	condchar = *condition;
	*condition = '\0';
	/* ... and compare the name without version */
	ch = strrchr(fname, '-');
	if (ch && ch - fname < PATH_MAX) {
	    strlcpy(basefname, fname, ch - fname + 1);
	    fname = basefname;
	}
    }

    switch (MatchType) {
    case LEGACY_MATCH_EREGEX:
    case LEGACY_MATCH_REGEX:
	errcode = rex_match(pattern, fname, MatchType == LEGACY_MATCH_EREGEX ? 1 : 0);
	break;
    case LEGACY_MATCH_NGLOB:
    case LEGACY_MATCH_GLOB:
	errcode = (csh_match(pattern, fname, 0) == 0) ? 1 : 0;
	break;
    case LEGACY_MATCH_EXACT:
	errcode = (strcmp(pattern, fname) == 0) ? 1 : 0;
	break;
    case LEGACY_MATCH_ALL:
	errcode = 1;
	break;
    default:
	break;
    }

    /* loop over all appended conditions */
    while (condition) {
	/* restore the pattern */
	*condition = condchar;
	/* parse the condition (fun with bits) */
	if (errcode == 1) {
	    char *nextcondition;
	    /* compare version numbers */
	    int match = 0;
	    if (*++condition == '=') {
		match = 2;
		condition++;
	    }
	    switch(condchar) {
	    case '<':
		match |= 1;
		break;
	    case '>':
		match |= 4;
		break;
	    case '=':
		match |= 2;
		break;
	    case '!':
		match = 5;
		break;
	    }
	    /* isolate the version number from the next condition ... */
	    nextcondition = strpbrk(condition, "<>=!");
	    if (nextcondition) {
		condchar = *nextcondition;
		*nextcondition = '\0';
	    }
	    /* and compare the versions (version_cmp removes the filename for us) */
	    if ((match & (1 << (version_cmp(pkgname, condition) + 1))) == 0)
		errcode = 0;
	    condition = nextcondition;
	} else {
	    break;
	}
    }

    return errcode;
}

/*
 * Synopsis is similar to matchinstalled(), but use origin
 * as a key for matching packages.
 */
char ***
matchallbyorigin(const char **origins, int *retval)
{
    char **installed, **allorigins = NULL;
    char ***matches = NULL;
    int i, j;

    if (retval != NULL)
	*retval = 0;

    installed = matchinstalled(LEGACY_MATCH_ALL, NULL, retval);
    if (installed == NULL)
	return NULL;

    /* Gather origins for all installed packages */
    for (i = 0; installed[i] != NULL; i++) {
	FILE *fp;
	char *buf, *cp, tmp[PATH_MAX];
	int cmd;

	allorigins = realloc(allorigins, (i + 1) * sizeof(*allorigins));
	allorigins[i] = NULL;

	snprintf(tmp, PATH_MAX, "%s/%s", LOG_DIR, installed[i]);
	/*
	 * SPECIAL CASE: ignore empty dirs, since we can can see them
	 * during port installation.
	 */
	if (isemptydir(tmp))
	    continue;
	strncat(tmp, "/" CONTENTS_FNAME, PATH_MAX);
	fp = fopen(tmp, "r");
	if (fp == NULL) {
	    warnx("the package info for package '%s' is corrupt", installed[i]);
	    continue;
	}

	cmd = -1;
	while (fgets(tmp, sizeof(tmp), fp)) {
	    int len = strlen(tmp);

	    while (len && isspace(tmp[len - 1]))
		tmp[--len] = '\0';
	    if (!len)
		continue;
	    cp = tmp;
	    if (tmp[0] != CMD_CHAR)
		continue;
	    cmd = plist_cmd(tmp + 1, &cp);
	    if (cmd == PLIST_ORIGIN) {
		asprintf(&buf, "%s", cp);
		allorigins[i] = buf;
		break;
	    }
	}
	if (cmd != PLIST_ORIGIN && ( Verbose || 0 != strncmp("bsdpan-", installed[i], 7 ) ) )
	    warnx("package %s has no origin recorded", installed[i]);
	fclose(fp);
    }

    /* Resolve origins into package names, retaining the sequence */
    for (i = 0; origins[i] != NULL; i++) {
	matches = realloc(matches, (i + 1) * sizeof(*matches));
	struct store *store = NULL;
	store = storecreate(store);

	for (j = 0; installed[j] != NULL; j++) {
	    if (allorigins[j]) {
		if (csh_match(origins[i], allorigins[j], FNM_PATHNAME) == 0) {
		    storeappend(store, installed[j]);
		}
	    }
	}
	if (store->used == 0)
	    matches[i] = NULL;
	else
	    matches[i] = store->store;
    }

    if (allorigins) {
	for (i = 0; installed[i] != NULL; i++)
	    if (allorigins[i])
		free(allorigins[i]);
	free(allorigins);
    }

    return matches;
}

/*
 * Synopsis is similar to matchinstalled(), but use origin
 * as a key for matching packages.
 */
char **
matchbyorigin(const char *origin, int *retval)
{
   const char *origins[2];
   char ***tmp;

   origins[0] = origin;
   origins[1] = NULL;

   tmp = matchallbyorigin(origins, retval);
   if (tmp && tmp[0]) {
	return tmp[0];
   } else {
	return NULL;
   }
}

struct pkg *
getpkg(const char *name)
{
    struct pkgdb_it *it;
    struct pkg *pkg = NULL;


    if ((it = pkgdb_query(db, name, MATCH_EXACT)) == NULL)
	    return (NULL);

    pkg = NULL;
    pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_DEPS|PKG_LOAD_RDEPS|PKG_LOAD_FILES);

    pkgdb_it_free(it);

    return (pkg);
}

/*
 * 
 * Return 1 if the specified package is installed,
 * 0 if not, and -1 if an error occurred.
 */
int
isinstalledpkg(const char *name)
{
    int result = 0;
    struct pkgdb_it *it;
    struct pkg *pkg;


    if ((it = pkgdb_query(db, name, MATCH_EXACT)) == NULL)
	    return (-1);

    pkg = NULL;
    if (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK)
	    result = 1;

    pkgdb_it_free(it);
    pkg_free(pkg);

    return (result);
}

/*
 * Returns 1 if specified pkgname matches RE pattern.
 * Otherwise returns 0 if doesn't match or -1 if RE
 * engine reported an error (usually invalid syntax).
 */
static int
rex_match(const char *pattern, const char *pkgname, int extended)
{
    char errbuf[128];
    int errcode;
    int retval;
    regex_t rex;

    retval = 0;

    errcode = regcomp(&rex, pattern, (extended ? REG_EXTENDED : REG_BASIC) | REG_NOSUB);
    if (errcode == 0)
	errcode = regexec(&rex, pkgname, 0, NULL, 0);

    if (errcode == 0) {
	retval = 1;
    } else if (errcode != REG_NOMATCH) {
	regerror(errcode, &rex, errbuf, sizeof(errbuf));
	warnx("%s: %s", pattern, errbuf);
	retval = -1;
    }

    regfree(&rex);

    return retval;
}

/*
 * Match string by a csh-style glob pattern. Returns 0 on
 * match and FNM_NOMATCH otherwise, to be compatible with
 * fnmatch(3).
 */
static int
csh_match(const char *pattern, const char *string, int flags)
{
    int ret = FNM_NOMATCH;


    const char *nextchoice = pattern;
    const char *current = NULL;

    int prefixlen = -1;
    int currentlen = 0;

    int level = 0;

    do {
	const char *pos = nextchoice;
	const char *postfix = NULL;

	Boolean quoted = FALSE;

	nextchoice = NULL;

	do {
	    const char *eb;
	    if (!*pos) {
		postfix = pos;
	    } else if (quoted) {
		quoted = FALSE;
	    } else {
		switch (*pos) {
		case '{':
		    ++level;
		    if (level == 1) {
			current = pos+1;
			prefixlen = pos-pattern;
		    }
		    break;
		case ',':
		    if (level == 1 && !nextchoice) {
			nextchoice = pos+1;
			currentlen = pos-current;
		    }
		    break;
		case '}':
		    if (level == 1) {
			postfix = pos+1;
			if (!nextchoice)
			    currentlen = pos-current;
		    }
		    level--;
		    break;
		case '[':
		    eb = pos+1;
		    if (*eb == '!' || *eb == '^')
			eb++;
		    if (*eb == ']')
			eb++;
		    while(*eb && *eb != ']')
			eb++;
		    if (*eb)
			pos=eb;
		    break;
		case '\\':
		    quoted = TRUE;
		    break;
		default:
		    ;
		}
	    }
	    pos++;
	} while (!postfix);

	if (current) {
	    char buf[FILENAME_MAX];
	    snprintf(buf, sizeof(buf), "%.*s%.*s%s", prefixlen, pattern, currentlen, current, postfix);
	    ret = csh_match(buf, string, flags);
	    if (ret) {
		current = nextchoice;
		level = 1;
	    } else
		current = NULL;
	} else
	    ret = fnmatch(pattern, string, flags);
    } while (current);

    return ret;
}

/*
 * Create an empty store, optionally deallocating
 * any previously allocated space if store != NULL.
 */
struct store *
storecreate(struct store *store)
{
    int i;

    if (store == NULL) {
	store = malloc(sizeof *store);
	if (store == NULL) {
	    warnx("%s(): malloc() failed", __func__);
	    return NULL;
	}
	store->currlen = 0;
	store->store = NULL;
    } else if (store->store != NULL) {
	    /* Free previously allocated memory */
	    for (i = 0; store->store[i] != NULL; i++)
		free(store->store[i]);
	    store->store[0] = NULL;
    }
    store->used = 0;

    return store;
}

/*
 * Append specified element to the provided store.
 */
static int
storeappend(struct store *store, const char *item)
{
    if (store->used + 2 > store->currlen) {
	store->currlen += 16;
	store->store = reallocf(store->store,
				store->currlen * sizeof(*(store->store)));
	if (store->store == NULL) {
	    store->currlen = 0;
	    warnx("%s(): reallocf() failed", __func__);
	    return 1;
	}
    }

    asprintf(&(store->store[store->used]), "%s", item);
    if (store->store[store->used] == NULL) {
	warnx("%s(): malloc() failed", __func__);
	return 1;
    }
    store->used++;
    store->store[store->used] = NULL;

    return 0;
}

static int
fname_cmp(const FTSENT * const *a, const FTSENT * const *b)
{
    return strcmp((*a)->fts_name, (*b)->fts_name);
}
