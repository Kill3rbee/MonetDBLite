/*
 * The contents of this file are subject to the MonetDB Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://monetdb.cwi.nl/Legal/MonetDBLicense-1.1.html
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is the MonetDB Database System.
 *
 * The Initial Developer of the Original Code is CWI.
 * Portions created by CWI are Copyright (C) 1997-July 2008 CWI.
 * Copyright August 2008-2010 MonetDB B.V.
 * All Rights Reserved.
 */

/**
 * stethoscope
 * Martin Kersten
 *
 * The Stethoscope
 * The performance profiler infrastructure provides
 * precisely control through annotation of a MAL program. 
 * Often, however, inclusion of profiling statements is an afterthought.
 *
 * The program stethoscope addresses this situation by providing
 * a simple application that can attach itself to a running
 * server and extracts the profiler events from concurrent running queries.
 *
 * The arguments to @code{stethoscope} are the profiler properties to be traced
 * and the applicable filter expressions. For example,
 *   stethoscope -t bat.insert algebra.join
 * tracks the microsecond ticks of two specific MAL instructions.
 * A synopsis of the calling conventions:
 *   stethoscope [options] +[aefoTtcmibds] @{<mod>.<fcn> @}
 *     -d | --dbname=<database_name>
 *     -u | --user=<user>
 *     -P | --password=<password>
 *     -p | --port=<portnr>
 *     -g | --gnuplot=<boolean>
 *     -h | --host=<hostname>
 * 
 * Event selector:
 *     a =aggregates
 *     e =event
 *     f =function 
 *     o =operation called
 *     i =interpreter theread
 *     T =time
 *     t =ticks
 *     c =cpu statistics
 *     m =memory resources
 *     r =block reads
 *     w =block writes
 *     b =bytes read/written
 *     s =statement
 *     y =argument types
 *     p =pgfaults,cntxtswitches
 *     S =Start profiling instruction
 * 
 * Ideally, the stream of events should be piped into a
 * 2D graphical tool, like xosview (Linux).
 * A short term solution is to generate a gnuplot script
 * to display the numerics organized as time lines.
 * With a backup of the event lists give you all the
 * information needed for a decent post-mortem analysis.
 * 
 * A convenient way to watch most of the SQL interaction
 * you may use the command:
 * stethoscope -umonetdb -Pmonetdb -hhost +tis "algebra.*" "bat.*" "group.*" "sql.*" "aggr.*"
 */

#include "clients_config.h"
#include "monet_options.h"
#include <mapilib/Mapi.h>
#include <stream.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#ifndef HAVE_GETOPT_LONG
# include "monet_getopt.h"
#else
# ifdef HAVE_GETOPT_H
#  include "getopt.h"
# endif
#endif


static struct {
	char tag;
	char *ptag;     /* which profiler group counter is needed */
	char *name;     /* which logical counter is needed */
	int status;     /* trace it or not */
} profileCounter[] = {
	/*  0  */ { 'a', "aggregate", "total count", 0 },
	/*  1  */ { 'a', "aggregate", "total ticks", 0 },
	/*  2  */ { 'e', "event", "event id", 0 },
	/*  3  */ { 'f', "pc", "function", 0 },
	/*  4  */ { 'f', "pc", "pc", 0 },
	/*  5  */ { 'o', "operation", "operation", 0 },
	/*  6  */ { 'T', "time", "time stamp", 0 },
	/*  7  */ { 't', "ticks", "usec ticks", 1 },
	/*  8  */ { 'c', "cpu", "utime", 0 },
	/*  9  */ { 'c', "cpu", "cutime", 0 },
	/*  0  */ { 'c', "cpu", "stime", 0 },
	/*  1  */ { 'c', "cpu", "cstime", 0 },
	/*  2  */ { 'm', "memory", "arena", 0 },
	/*  3  */ { 'm', "memory", "ordblks", 0 },
	/*  4  */ { 'm', "memory", "smblks", 0 },
	/*  5  */ { 'm', "memory", "hblkhd", 0 },
	/*  6  */ { 'm', "memory", "hblks", 0 },
	/*  7  */ { 'm', "memory", "fsmblks", 0 },
	/*  8  */ { 'm', "memory", "uordblks", 0 },
	/*  9  */ { 'r', "reads", "blk reads", 0 },
	/*  0  */ { 'w', "writes", "blk writes", 0 },
	/*  1  */ { 'b', "rbytes", "rbytes", 0 },
	/*  2  */ { 'b', "wbytes", "wbytes", 0 },
	/*  3  */ { 's', "stmt", "stmt", 2 },
	/*  4  */ { 'p', "process", "pg reclaim", 0 },
	/*  5  */ { 'p', "process", "pg faults", 0 },
	/*  6  */ { 'p', "process", "swaps", 0 },
	/*  7  */ { 'p', "process", "ctxt switch", 0 },
	/*  8  */ { 'p', "process", "inv switch", 0 },
	/*  9  */ { 'i', "thread", "thread", 0 },
	/*  0  */ { 'u', "user", "user", 0 },
	/*  1  */ { 'S', "start", "start", 0 },
	/*  2  */ { 'y', "type", "type", 0 },
	/*  3  */ { 0, 0, 0, 0 }
};

typedef struct _wthread {
	pthread_t id;
	int tid;
	char *uri;
	char *user;
	char *pass;
	stream *s;
	size_t argc;
	char **argv;
	struct _wthread *next;
} wthread;

static wthread *thds = NULL;

static void
usage()
{
	fprintf(stderr, "stethoscope [options] +[trace options] {<mod>.<fcn>}\n");
	fprintf(stderr, "  -d | --dbname=<database_name>\n");
	fprintf(stderr, "  -u | --user=<user>\n");
	fprintf(stderr, "  -P | --password=<password>\n");
	fprintf(stderr, "  -p | --port=<portnr>\n");
	fprintf(stderr, "  -h | --host=<hostname>\n");
	fprintf(stderr, "  -g | --gnuplot\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "The trace options:\n");
	fprintf(stderr, "  S = start instruction profiling\n");
	fprintf(stderr, "  a = aggregates\n");
	fprintf(stderr, "  e = event\n");
	fprintf(stderr, "  f = function \n");
	fprintf(stderr, "  o = operation called\n");
	fprintf(stderr, "  i = interpreter thread\n");
	fprintf(stderr, "  T = time\n");
	fprintf(stderr, "  t = ticks\n");
	fprintf(stderr, "  c = cpu statistics\n");
	fprintf(stderr, "  m = memory resources\n");
	fprintf(stderr, "  r = block reads\n");
	fprintf(stderr, "  w = block writes\n");
	fprintf(stderr, "  b = bytes read/written\n");
	fprintf(stderr, "  s = statement\n");
	fprintf(stderr, "  y = argument types\n");
	fprintf(stderr, "  p = page faults, context switches\n");
	fprintf(stderr, "  u = user\n");
}

/* Any signal should be captured and turned into a graceful
 * termination of the profiling session. */
static void
stopListening(int i)
{
	wthread *walk;
	(void)i;
	/* kill all connections */
	for (walk = thds; walk != NULL; walk = walk->next) {
		if (walk->s != NULL)
			stream_close(walk->s);
	}
}

static void
setCounter(char *nme)
{
	int i, k = 1;

	for (i = 0; profileCounter[i].tag; i++)
		profileCounter[i].status = 0;

	for (; *nme; nme++)
		for (i = 0; profileCounter[i].tag; i++)
			if (profileCounter[i].tag == *nme)
				profileCounter[i].status = k++;
}

#if 0
static char *
getFieldName(int k)
{
	int i;
	for (i = 0; profileCounter[i].tag; i++)
		if (profileCounter[i].status == k)
			return profileCounter[i].name;
	return "unknown";
}
#endif

static void
plottemplate(int * colind, int n)
{
	FILE *pl, *pld;
	double sz = 1.0 / n;
	int i;

	pl = fopen("stet.gp", "w");
	fprintf(pl, "load \"stet_dyn.gp\"\n");
	fprintf(pl, "pause 1\nreread \n");
	fclose(pl);
/*	pld= fopen("stet_dyn_tmpl.gp","w");*/
	pld = fopen("stet_dyn.gp", "w");
	fprintf(pld, "set multiplot\n\n");
	for (i = 0; i < n; i++) {
		fprintf(pld, "set size 1.0, 1.0 \nset origin 0.0, 0.0 \n");
		fprintf(pld, "set size 1.0,%4.2f\n", sz);
		fprintf(pld, "set origin 0.0,%4.2f\n", 1 - (i + 1) * sz);
		fprintf(pld, "set ylabel \"%s\"\n", profileCounter[colind[i]].name);
		fprintf(pld, "unset key\n");
		fprintf(pld, "plot \"stet_cur.dat\" using 1:%d with boxes fs solid 0.7\n\n", i + 2);
	}
	fprintf(pld, "\nunset multiplot");

	fclose(pld);
}

#define die(dbh, hdl) while (1) {(hdl ? mapi_explain_query(hdl, stderr) :  \
					   dbh ? mapi_explain(dbh, stderr) :        \
					   fprintf(stderr, "!! %scommand failed\n", id)); \
					   goto stop_disconnect;}
#define doQ(X) \
	if ((hdl = mapi_query(dbh, X)) == NULL || mapi_error(dbh) != MOK) \
			 die(dbh, hdl);

static void *
doProfile(void *d)
{
	wthread *wthr = (wthread*)d;
	int i;
	size_t a;
	size_t ln = 1;
	char *response, *x;
	char buf[BUFSIZ];
	char *mod, *fcn;
	char *host;
	int portnr;
	char cmd[100];
	char id[10];
	int colind[30], colcnt = 0;
	int gnuplot = 0;
	Mapi dbh;
	MapiHdl hdl = NULL;

	/* set up the profiler */
	dbh = mapi_mapiuri(wthr->uri, wthr->user, wthr->pass, "mal");
	if (dbh == NULL || mapi_error(dbh))
		die(dbh, hdl);
	mapi_reconnect(dbh);
	if (mapi_error(dbh))
		die(dbh, hdl);
	if (wthr->tid > 0) {
		snprintf(id, 10, "[%d] ", wthr->tid);
		printf("-- connection with server %s is %s\n", wthr->uri, id);
	} else {
		id[0] = '\0';
		printf("-- connection with server %s\n", wthr->uri);
	}

	/* set counters */
	x = NULL;
	for (i = 0; profileCounter[i].tag; i++) {
		/* skip duplicates */
		if (x == profileCounter[i].ptag)
			continue;
		/* deactivate any left over counter first */
		snprintf(buf, BUFSIZ, "profiler.deactivate(\"%s\");",
				profileCounter[i].ptag);
		doQ(buf);
		if (profileCounter[i].status) {
			snprintf(buf, BUFSIZ, "profiler.activate(\"%s\");",
					profileCounter[i].ptag);
			doQ(buf);
			colind[colcnt++] = i;
			printf("-- %s%s\n", id, buf);
		}
		x = profileCounter[i].ptag;
	}

	snprintf(buf, BUFSIZ, "port := profiler.openUDPStream();");
	doQ(buf);
	snprintf(buf, BUFSIZ, "io.print(port);");
	doQ(buf);
	x = NULL;
	do {
		if (!mapi_fetch_row(hdl))
			break;
		x = mapi_fetch_field(hdl, 0);
		if (x == NULL)
			break;
		portnr = atoi(x);
	} while (0);
	if (x == NULL) {
		fprintf(stderr, "!! %sfailed to obtain port number from remote "
				"server for profiling\n", id);
		goto stop_cleanup;
	}

	host = mapi_get_host(dbh);
	printf("-- %sopening UDP profile stream for %s:%d\n",
			id, host, portnr);
	if ((wthr->s = udp_rastream(host, portnr, "profileStream")) == NULL) {
		fprintf(stderr, "!! %sopening stream failed: %s\n",
				id, strerror(errno));
		goto stop_cleanup;
	}

	/* Set Filters */
	doQ("profiler.setNone();");

	if (wthr->argc == 0) {
		doQ("profiler.setAll();");
	} else {
		for (a = 0; a < wthr->argc; a++) {
			char *c;
			c = strchr(wthr->argv[a], '.');
			if (c) {
				mod = wthr->argv[a];
				if (mod == c) mod = "*";
				fcn = c + 1;
				if (*fcn == 0) fcn = "*";
				*c = 0;
			} else {
				fcn = wthr->argv[a];
				mod = "*";
			}
			snprintf(buf, BUFSIZ, "profiler.setFilter(\"%s\",\"%s\");", mod, fcn);
			printf("-- %s%s\n", id, buf);
			doQ(buf);
		}
	}
	doQ("profiler.start();");

	if (gnuplot)
		plottemplate(colind, colcnt);

	printf("-- %sready to receive events\n", id);
	while (stream_read(wthr->s, buf, 1, BUFSIZ)) {
		char *e;
		response = buf;
		while ((e = strchr(response, '\n')) != NULL) {
			*e = 0;
			printf("%s%s\n", id, response);
			if (gnuplot && (x = strchr(response, '['))) {
				d = fopen("stet.dat", "a+");
				fprintf(d, "%zd\t", ln++);
				for (; *x != '\0'; x++) {
					if (*x == '"') {
						break;  /* stop at first string */
					} else if (strchr("[],\"", *x) == NULL) {
						fprintf(d, "%c", *x);
					}
				}
				fprintf(d, "\n");
				fclose(d);

				/* update plot file */

				if (ln > 20) {
					sprintf(cmd, "sed '1, %zd d' stet.dat > stet_cur.dat", ln - 20);
				} else {
					sprintf(cmd, "cp stet.dat stet_cur.dat");
				}
				/*	printf("%s \n",cmd);*/
				if (system(cmd) != 0)
					fprintf(stderr, "command `%s' failed\n", cmd);
			}
			response = e + 1;
		}
	}

stop_cleanup:
	doQ("profiler.setNone();");
	doQ("profiler.stop();");
	doQ("profiler.closeStream();");
stop_disconnect:
	mapi_disconnect(dbh);
	mapi_destroy(dbh);

	printf("-- %sconnection with server %s closed\n", id, wthr->uri);

	return(0);
}

int
main(int argc, char **argv)
{
	int a = 1;
	int i;
	char *host = "localhost";
	int portnr = 50000;
	char *dbname = NULL;
	char *user = NULL;
	char *password = NULL;
	int gnuplot = 0;
	char **alts, **oalts;
	wthread *walk;

	static struct option long_options[8] = {
		{ "dbname", 1, 0, 'd' },
		{ "user", 1, 0, 'u' },
		{ "password", 1, 0, 'P' },
		{ "port", 1, 0, 'p' },
		{ "host", 1, 0, 'h' },
		{ "help", 0, 0, '?' },
		{ "gnuplot", 1, 0, 'g' },
		{ 0, 0, 0, 0 }
	};
	while (1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "d:u:P:p:?:h:g",
			long_options, &option_index);
		if (c == -1)
			break;
		switch (c) {
		case 'd':
			dbname = optarg;
			break;
		case 'u':
			user = optarg;
			break;
		case 'P':
			password = optarg;
			break;
		case 'p':
			portnr = atol(optarg);
			break;
		case 'g':
			gnuplot = 1;
			break;
		case 'h':
			if (strcmp(long_options[option_index].name, "help") == 0) {
				usage();
				break;
			}
			if (strcmp(long_options[option_index].name, "host") == 0) {
				host = optarg;
				break;
			}
		default:
			usage();
			exit(0);
		}
	}

	a = optind;
	if (argc > 1 && a < argc && argv[a][0] == '+') {
		setCounter(argv[a] + 1);
		a++;
	} else
		setCounter("TtiesS");

	if (user == NULL || password == NULL) {
		fprintf(stderr, "%s: need -u and -P arguments\n", argv[0]);
		usage();
		exit(-1);
	}

	signal(SIGABRT, stopListening);
#ifdef SIGPIPE
	signal(SIGPIPE, stopListening);
#endif
#ifdef SIGHUP
	signal(SIGHUP, stopListening);
#endif
	signal(SIGTERM, stopListening);
	signal(SIGINT, stopListening);

	close(0); /* get rid of stdin */

	/* try and find multiple options, we assume that we always need a
	 * local merovingian for that, in the future we probably need to fix
	 * this in a decent manner */
	if (dbname != NULL) {
		oalts = alts = mapi_resolve(host, portnr, dbname);
	} else {
		alts = NULL;
		dbname = "";
	}

	if (alts == NULL) {
		/* nothing to redirect, so a single host to try */
		char uri[512];
		snprintf(uri, 512, "mapi:monetdb://%s:%d/%s", host, portnr, dbname);
		walk = thds = malloc(sizeof(wthread));
		walk->uri = uri;
		walk->user = user;
		walk->pass = password;
		walk->argc = argc - a;
		walk->argv = &argv[a - 1];
		walk->tid = 0;
		walk->s = NULL;
		walk->next = NULL;
		doProfile(walk);
		free(walk);
	} else {
		/* fork runner threads for all alternatives */
		walk = thds = malloc(sizeof(wthread));
		i = 1;
		if (*alts != NULL) while (1) {
			walk->tid = i++;
			walk->uri = *alts;
			walk->user = user;
			walk->pass = password;
			walk->argc = argc - a;
			walk->argv = &argv[a - 1];
			walk->s = NULL;
			pthread_create(&walk->id, NULL, &doProfile, walk);
			alts++;
			if (*alts == NULL)
				break;
			walk = walk->next = malloc(sizeof(wthread));
		}
		walk->next = NULL;
		free(oalts);
		for (walk = thds; walk != NULL; walk = walk->next) {
			pthread_join(walk->id, NULL);
			free(walk->uri);
			free(walk);
		}
	}
}
