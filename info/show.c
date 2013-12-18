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
 * Various display routines for the info module.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: stable/10/usr.sbin/pkg_install/info/show.c 240682 2012-09-18 22:09:23Z bapt $");

#include "lib.h"
#include "info.h"
#include <err.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <md5.h>

void
show_file(const char *title, const char *fname)
{
    FILE *fp;
    char line[1024];
    int n;

    if (!Quiet)
	printf("%s%s", InfoPrefix, title);
    fp = fopen(fname, "r");
    if (fp == (FILE *) NULL)
	printf("ERROR: show_file: Can't open '%s' for reading!\n", fname);
    else {
	int append_nl = 0;
	while ((n = fread(line, 1, 1024, fp)) != 0)
	    fwrite(line, 1, n, stdout);
	fclose(fp);
	append_nl = (line[n - 1] != '\n');	/* Do we have a trailing \n ? */
	if (append_nl)
	   printf("\n");
    }
    printf("\n");	/* just in case */
}

static const char *
elide_root(const char *dir)
{
    if (strcmp(dir, "/") == 0)
	return "";
    return dir;
}

/* Show files that don't match the recorded checksum */
int
show_cksum(const char *title, Package *plist)
{
    PackingList p;
    const char *dir = ".";
    char *prefix = NULL;
    char tmp[FILENAME_MAX];
    int errcode = 0;

    if (!Quiet) {
	printf("%s%s", InfoPrefix, title);
	fflush(stdout);
    }

    for (p = plist->head; p != NULL; p = p->next)
	if (p->type == PLIST_CWD) {
	    if (!prefix)
		prefix = p->name;
	    if (p->name == NULL)
		dir = prefix;
	    else
		dir = p->name;
	} else if (p->type == PLIST_FILE) {
	    snprintf(tmp, FILENAME_MAX, "%s/%s", elide_root(dir), p->name);
	    if (!fexists(tmp)) {
		warnx("%s doesn't exist", tmp);
		errcode = 1;
	    } else if (p->next && p->next->type == PLIST_COMMENT &&
	             (strncmp(p->next->name, "MD5:", 4) == 0)) {
		char *cp = NULL, buf[33];

		/*
		 * For packing lists whose version is 1.1 or greater, the md5
		 * hash for a symlink is calculated on the string returned
		 * by readlink().
		 */
		if (issymlink(tmp) && verscmp(plist, 1, 0) > 0) {
		    int len;
		    char linkbuf[FILENAME_MAX];

		    if ((len = readlink(tmp, linkbuf, FILENAME_MAX)) > 0)
			cp = MD5Data((unsigned char *)linkbuf, len, buf);
		} else if (isfile(tmp) || verscmp(plist, 1, 1) < 0)
		    cp = MD5File(tmp, buf);

		if (cp != NULL) {
		    /* Mismatch? */
		    if (strcmp(cp, p->next->name + 4))
			printf("%s fails the original MD5 checksum\n", tmp);
		    else if (Verbose)
			printf("%s matched the original MD5 checksum\n", tmp);
		}
	    }
	}
    return (errcode);
}
