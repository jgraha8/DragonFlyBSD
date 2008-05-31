/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sbin/hammer/Attic/cmd_prune.c,v 1.12 2008/05/31 18:45:04 dillon Exp $
 */

#include "hammer.h"

static void hammer_prune_load_file(hammer_tid_t now_tid, 
			struct hammer_ioc_prune *prune,
			const char *filesystem, const char *filename);
static int hammer_prune_parse_line(hammer_tid_t now_tid,
			struct hammer_ioc_prune *prune,
			const char *filesystem, char **av, int ac);
static int hammer_prune_parse_line_short(hammer_tid_t now_tid,
			struct hammer_ioc_prune *prune,
			const char *filesystem, char **av);
static int hammer_prune_parse_line_long(hammer_tid_t now_tid,
			struct hammer_ioc_prune *prune,
			const char *filesystem, char **av);
static void hammer_prune_create_links(const char *filesystem,
			struct hammer_ioc_prune *prune);
static void hammer_prune_make_softlink(const char *filesystem,
			hammer_tid_t tid);
static int parse_modulo_time(const char *str, u_int64_t *delta);
static char *tid_to_stamp_str(hammer_tid_t tid);
static void prune_usage(int code);

/*
 * prune <filesystem> from <modulo_time> to <modulo_time> every <modulo_time>
 * prune <filesystem> [using <filename>]
 */
void
hammer_cmd_prune(char **av, int ac)
{
	struct hammer_ioc_prune prune;
	const char *filesystem;
	int fd;
	hammer_tid_t now_tid = (hammer_tid_t)time(NULL) * 1000000000LL;

	bzero(&prune, sizeof(prune));
	prune.elms = malloc(HAMMER_MAX_PRUNE_ELMS * sizeof(*prune.elms));
	prune.nelms = 0;
	prune.beg_localization = HAMMER_MIN_LOCALIZATION;
	prune.beg_obj_id = HAMMER_MIN_OBJID;
	prune.end_localization = HAMMER_MAX_LOCALIZATION;
	prune.end_obj_id = HAMMER_MAX_OBJID;
	hammer_get_cycle(&prune.end_obj_id, &prune.end_localization);

	prune.stat_oldest_tid = HAMMER_MAX_TID;

	if (ac == 0)
		prune_usage(1);
	filesystem = av[0];
	if (ac == 1) {
		hammer_prune_load_file(now_tid, &prune, filesystem, 
				      "/etc/hammer.conf");
	} else if (strcmp(av[1], "using") == 0) {
		if (ac == 2)
			prune_usage(1);
		hammer_prune_load_file(now_tid, &prune, filesystem, av[2]);
	} else if (strcmp(av[1], "everything") == 0) {
		prune.head.flags |= HAMMER_IOC_PRUNE_ALL;
		if (ac > 2)
			prune_usage(1);
	} else {
		if (hammer_prune_parse_line(now_tid, &prune, filesystem,
					    av, ac) < 0) {
			prune_usage(1);
		}
	}
	fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(1, "Unable to open %s", filesystem);
	if (ioctl(fd, HAMMERIOC_PRUNE, &prune) < 0) {
		printf("Prune %s failed: %s\n",
		       filesystem, strerror(errno));
	} else if (prune.head.flags & HAMMER_IOC_HEAD_INTR) {
		printf("Prune %s interrupted by timer at %016llx %04x\n",
		       filesystem,
		       prune.cur_obj_id, prune.cur_localization);
		if (CyclePath) {
			hammer_set_cycle(prune.cur_obj_id,
					 prune.cur_localization);
		}

	} else {
		if (CyclePath)
			hammer_reset_cycle();
		printf("Prune %s succeeded\n", filesystem);
	}
	close(fd);
	if (LinkPath)
		hammer_prune_create_links(filesystem, &prune);
	printf("Pruned %lld/%lld records (%lld directory entries) "
	       "and %lld bytes\n",
		prune.stat_rawrecords,
		prune.stat_scanrecords,
		prune.stat_dirrecords,
		prune.stat_bytes
	);
}

static void
hammer_prune_load_file(hammer_tid_t now_tid, struct hammer_ioc_prune *prune,
		       const char *filesystem, const char *filename)
{
	char buf[256];
	FILE *fp;
	char *av[16];
	int ac;
	int lineno;

	if ((fp = fopen(filename, "r")) == NULL)
		err(1, "Unable to read %s", filename);
	lineno = 0;
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		++lineno;
		if (strncmp(buf, "prune", 5) != 0)
			continue;
		ac = 0;
		av[ac] = strtok(buf, " \t\r\n");
		while (av[ac] != NULL) {
			++ac;
			if (ac == 16) {
				fclose(fp);
				errx(1, "Malformed prune directive in %s "
				     "line %d\n", filename, lineno);
			}
			av[ac] = strtok(NULL, " \t\r\n");
		}
		if (ac == 0)
			continue;
		if (strcmp(av[0], "prune") != 0)
			continue;
		printf("prune %s\n", av[2]);
		if (hammer_prune_parse_line(now_tid, prune, filesystem,
					    av + 1, ac - 1) < 0) {
			errx(1, "Malformed prune directive in %s line %d\n",
			     filename, lineno);
		}
	}
	fclose(fp);
}

static __inline
const char *
plural(int notplural)
{
	return(notplural ? "" : "s");
}

/*
 * Parse the following parameters:
 *
 * <filesystem> from <modulo_time> to <modulo_time> every <modulo_time>
 * <filesystem> from <modulo_time> everything
 */
static int
hammer_prune_parse_line(hammer_tid_t now_tid, struct hammer_ioc_prune *prune,
			const char *filesystem, char **av, int ac)
{
	int r;

	switch(ac) {
	case 4:
		r = hammer_prune_parse_line_short(now_tid, prune,
						  filesystem, av);
		break;
	case 7:
		r = hammer_prune_parse_line_long(now_tid, prune,
						 filesystem, av);
		break;
	default:
		r = -1;
		break;
	}
	return(r);
}

static int
hammer_prune_parse_line_short(hammer_tid_t now_tid,
			     struct hammer_ioc_prune *prune,
			     const char *filesystem, char **av)
{
	struct hammer_ioc_prune_elm *elm;
	u_int64_t from_time;
	char *from_stamp_str;

	if (strcmp(av[0], filesystem) != 0)
		return(0);
	if (strcmp(av[1], "from") != 0)
		return(-1);
	if (strcmp(av[3], "everything") != 0)
		return(-1);
	if (parse_modulo_time(av[2], &from_time) < 0)
		return(-1);
	if (from_time == 0) {
		fprintf(stderr, "Bad from or to time specification.\n");
		return(-1);
	}
	if (prune->nelms == HAMMER_MAX_PRUNE_ELMS) {
		fprintf(stderr, "Too many prune specifications in file! "
			"Max is %d\n", HAMMER_MAX_PRUNE_ELMS);
		return(-1);
	}

	/*
	 * Example:  from 1y everything
	 */
	elm = &prune->elms[prune->nelms++];
	elm->beg_tid = 1;
	elm->end_tid = now_tid - now_tid % from_time;
	if (now_tid - elm->end_tid < from_time)
		elm->end_tid -= from_time;
	assert(elm->beg_tid < elm->end_tid);
	elm->mod_tid = elm->end_tid - elm->beg_tid;

	/*
	 * Convert back to local time for pretty printing
	 */
	from_stamp_str = tid_to_stamp_str(elm->end_tid);
	printf("Prune everything older then %s", from_stamp_str);
	free(from_stamp_str);
	return(0);
}

static int
hammer_prune_parse_line_long(hammer_tid_t now_tid,
			     struct hammer_ioc_prune *prune,
			     const char *filesystem, char **av)
{
	struct hammer_ioc_prune_elm *elm;
	u_int64_t from_time;
	u_int64_t to_time;
	u_int64_t every_time;
	char *from_stamp_str;
	char *to_stamp_str;

	if (strcmp(av[0], filesystem) != 0)
		return(0);
	if (strcmp(av[1], "from") != 0)
		return(-1);
	if (strcmp(av[3], "to") != 0)
		return(-1);
	if (strcmp(av[5], "every") != 0)
		return(-1);
	if (parse_modulo_time(av[2], &from_time) < 0)
		return(-1);
	if (parse_modulo_time(av[4], &to_time) < 0)
		return(-1);
	if (parse_modulo_time(av[6], &every_time) < 0)
		return(-1);
	if (from_time > to_time)
		return(-1);
	if (from_time == 0 || to_time == 0) {
		fprintf(stderr, "Bad from or to time specification.\n");
		return(-1);
	}
	if (to_time % from_time != 0) {
		fprintf(stderr, "Bad TO time specification.\n"
			"It must be an integral multiple of FROM time\n");
		return(-1);
	}
	if (every_time == 0 ||
	    from_time % every_time != 0 ||
	    to_time % every_time != 0) {
		fprintf(stderr, "Bad 'every <modulo_time>' specification.\n"
			"It must be an integral subdivision of FROM and TO\n");
		return(-1);
	}
	if (prune->nelms == HAMMER_MAX_PRUNE_ELMS) {
		fprintf(stderr, "Too many prune specifications in file! "
			"Max is %d\n", HAMMER_MAX_PRUNE_ELMS);
		return(-1);
	}

	/*
	 * Example:  from 1m to 60m every 5m
	 */
	elm = &prune->elms[prune->nelms++];
	elm->beg_tid = now_tid - now_tid % to_time;
	if (now_tid - elm->beg_tid < to_time)
		elm->beg_tid -= to_time;

	elm->end_tid = now_tid - now_tid % from_time;
	if (now_tid - elm->end_tid < from_time)
		elm->end_tid -= from_time;

	elm->mod_tid = every_time;
	assert(elm->beg_tid < elm->end_tid);

	/*
	 * Convert back to local time for pretty printing
	 */
	from_stamp_str = tid_to_stamp_str(elm->beg_tid);
	to_stamp_str = tid_to_stamp_str(elm->end_tid);
	printf("Prune %s to %s every ", from_stamp_str, to_stamp_str);

	every_time /= 1000000000;
	if (every_time < 60)
		printf("%lld second%s\n", every_time, plural(every_time == 1));
	every_time /= 60;
	if (every_time && every_time < 60)
		printf("%lld minute%s\n", every_time, plural(every_time == 1));
	every_time /= 60;
	if (every_time && every_time < 24)
		printf("%lld hour%s\n", every_time, plural(every_time == 1));
	every_time /= 24;
	if (every_time)
		printf("%lld day%s\n", every_time, plural(every_time == 1));

	free(from_stamp_str);
	free(to_stamp_str);
	return(0);
}

/*
 * Create softlinks in the form $linkpath/snap_ddmmmyyyy[_hhmmss]
 */
static void
hammer_prune_create_links(const char *filesystem,
			  struct hammer_ioc_prune *prune)
{
	struct hammer_ioc_prune_elm *elm;
	hammer_tid_t tid;
	struct dirent *den;
	char *path;
	DIR *dir;

	if ((dir = opendir(LinkPath)) == NULL) {
		fprintf(stderr, "Unable to access linkpath %s\n", LinkPath);
		return;
	}
	while ((den = readdir(dir)) != NULL) {
		if (strncmp(den->d_name, "snap-", 5) == 0) {
			asprintf(&path, "%s/%s", LinkPath, den->d_name);
			remove(path);
			free(path);
		}
	}
	closedir(dir);

	for (elm = &prune->elms[0]; elm < &prune->elms[prune->nelms]; ++elm) {
		for (tid = elm->beg_tid;
		     tid < elm->end_tid;
		     tid += elm->mod_tid) {
			if (tid < prune->stat_oldest_tid)
				continue;
			hammer_prune_make_softlink(filesystem, tid);
		}
	}
}

static void
hammer_prune_make_softlink(const char *filesystem, hammer_tid_t tid)
{
	struct tm *tp;
	char *path;
	char *target;
	char buf[64];
	time_t t;

	t = (time_t)(tid / 1000000000);
	tp = localtime(&t);

	/*
	 * Construct the contents of the softlink.
	 */
	asprintf(&target, "%s/@@0x%016llx", filesystem, tid);

	/*
	 * Construct the name of the snap-shot softlink
	 */
	if (tid % (1000000000ULL * 60 * 60 * 24) == 0) {
		strftime(buf, sizeof(buf), "snap-%d%b%Y", tp);
	} else if (tid % (1000000000ULL * 60 * 60) == 0) {
		strftime(buf, sizeof(buf), "snap-%d%b%Y_%H%M", tp);
	} else if (tid % (1000000000ULL * 60) == 0) {
		strftime(buf, sizeof(buf), "snap-%d%b%Y_%H%M", tp);
	} else {
		strftime(buf, sizeof(buf), "snap-%d%b%Y_%H%M%S", tp);
	}

	asprintf(&path, "%s/%s", LinkPath, buf);
	symlink(target, path);
	free(path);
	free(target);
}

static
int
parse_modulo_time(const char *str, u_int64_t *delta)
{
	char *term;

	*delta = strtoull(str, &term, 10);

	switch(*term) {
	case 'y':
		*delta *= 12;
		/* fall through */
	case 'M':
		*delta *= 30;
		/* fall through */
	case 'd':
		*delta *= 24;
		/* fall through */
	case 'h':
		*delta *= 60;
		/* fall through */
	case 'm':
		*delta *= 60;
		/* fall through */
	case 's':
		break;
	default:
		return(-1);
	}
	*delta *= 1000000000LL;	/* TID's are in nanoseconds */
	return(0);
}

static char *
tid_to_stamp_str(hammer_tid_t tid)
{
	struct tm *tp;
	char *buf = malloc(256);
	time_t t;

	t = (time_t)(tid / 1000000000);
	tp = localtime(&t);
	strftime(buf, 256, "%e-%b-%Y %H:%M:%S %Z", tp);
	return(buf);
}

static void
prune_usage(int code)
{
	fprintf(stderr, "Bad prune directive, specify one of:\n"
			"prune filesystem [using filename]\n"
			"prune filesystem from <modulo_time> to <modulo_time> every <modulo_time>\n"
			"prune filesystem from <modulo_time> everything\n"
			"prune filesystem everything\n");
	exit(code);
}
