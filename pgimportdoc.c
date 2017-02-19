/*-------------------------------------------------------------------------
 *
 * pgimportdoc.c
 *	  command line tool for import XML, JSON, BYTEA documents to PostgreSQL
 *
 * IDENTIFICATION
 *   pgimportdoc.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres_fe.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#include "libpq-fe.h"
#include "pg_getopt.h"

#define BUFSIZE			1024


enum trivalue
{
	TRI_DEFAULT,
	TRI_NO,
	TRI_YES
};

enum format
{
	FORMAT_XML,
	FORMAT_TEXT,
	FORMAT_BYTEA
};

struct _param
{
	char	   *pg_user;
	enum trivalue pg_prompt;
	char	   *pg_port;
	char	   *pg_host;
	const char *progname;
	int			verbose;
	enum format fmt;
	char	   *command;
};

static void usage(const char *progname);
/*
 * This imports stdin to target database
 */
static int
pgimportdoc(const char *database, const struct _param * param)
{
	PGconn	   *conn;
	bool		new_pass;
	static bool have_password = false;
	static char password[100];

	/* Note: password can be carried over from a previous call */
	if (param->pg_prompt == TRI_YES && !have_password)
	{
		simple_prompt("Password: ", password, sizeof(password), false);
		have_password = true;
	}

	/*
	 * Start the connection.  Loop until we have a password if requested by
	 * backend.
	 */
	do
	{
#define PARAMS_ARRAY_SIZE	   7

		const char *keywords[PARAMS_ARRAY_SIZE];
		const char *values[PARAMS_ARRAY_SIZE];

		keywords[0] = "host";
		values[0] = param->pg_host;
		keywords[1] = "port";
		values[1] = param->pg_port;
		keywords[2] = "user";
		values[2] = param->pg_user;
		keywords[3] = "password";
		values[3] = have_password ? password : NULL;
		keywords[4] = "dbname";
		values[4] = database;
		keywords[5] = "fallback_application_name";
		values[5] = param->progname;
		keywords[6] = NULL;
		values[6] = NULL;

		new_pass = false;

		conn = PQconnectdbParams(keywords, values, true);
		if (!conn)
		{
			fprintf(stderr, "Connection to database \"%s\" failed\n",
					database);
			return -1;
		}

		if (PQstatus(conn) == CONNECTION_BAD &&
			PQconnectionNeedsPassword(conn) &&
			!have_password &&
			param->pg_prompt != TRI_NO)
		{
			PQfinish(conn);
			simple_prompt("Password: ", password, sizeof(password), false);
			have_password = true;
			new_pass = true;
		}
	} while (new_pass);

	/* check to see that the backend connection was successfully made */
	if (PQstatus(conn) == CONNECTION_BAD)
	{
		fprintf(stderr, "Connection to database \"%s\" failed:\n%s",
				database, PQerrorMessage(conn));
		PQfinish(conn);
		return -1;
	}

	if (param->verbose)
	{
		fprintf(stdout, "Connected to database \"%s\"\n", database);

		if (param->fmt == FORMAT_XML)
			fprintf(stdout, "Import XML document\n");
		else if (param->fmt == FORMAT_TEXT)
			fprintf(stdout, "Import TEXT document\n");
		else if (param->fmt == FORMAT_BYTEA)
			fprintf(stdout, "Import BYTEA document\n");
	}

	PQfinish(conn);

	return 0;
}

static void
usage(const char *progname)
{
	printf("%s imports XML, TEXT or BYTEA documents to PostgreSQL.\n\n", progname);
	printf("Usage:\n  %s [OPTION]... DBNAME\n\n", progname);
	printf("Options:\n");
	printf("  -V, --version  output version information, then exit\n");
	printf("  -?, --help     show this help, then exit\n");
	printf("  -v             write a lot of progress messages\n");
	printf("  -c sqlcmd      INSERT command with parameter\n");
	printf("  -t type        type specification [ XML | TEXT | BYTEA ], default is TEXT\n");
	printf("\nConnection options:\n");
	printf("  -h HOSTNAME    database server host or socket directory\n");
	printf("  -p PORT        database server port\n");
	printf("  -U USERNAME    user name to connect as\n");
	printf("  -w             never prompt for password\n");
	printf("  -W             force password prompt\n");
	printf("\n");
	printf("Report bugs to <pavel.stehule@gmail.com>.\n");
}


int
main(int argc, char **argv)
{
	int			rc = 0;
	struct _param param;
	int			c;
	int			port;
	const char *progname;

	progname = get_progname(argv[0]);

	/* Set default parameter values */
	param.pg_user = NULL;
	param.pg_prompt = TRI_DEFAULT;
	param.fmt = FORMAT_TEXT;
	param.pg_host = NULL;
	param.pg_port = NULL;
	param.progname = progname;

	/* Process command-line arguments */
	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0)
		{
			usage(progname);
			exit(0);
		}
		if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)
		{
			puts("pgimportdoc (PostgreSQL) " PG_VERSION);
			exit(0);
		}
	}

	while (1)
	{
		c = getopt(argc, argv, "h:l:U:p:c:t:vwW");
		if (c == -1)
			break;

		switch (c)
		{
			case '?':
				fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
				exit(1);
			case ':':
				exit(1);
			case 'v':
				param.verbose = 1;
				break;
			case 'c':
				param.command = pg_strdup(optarg);
				break;
			case 't':
				if (strcmp(optarg, "XML") == 0)
					param.fmt = FORMAT_XML;
				else if (strcmp(optarg, "TEXT") == 0)
					param.fmt = FORMAT_TEXT;
				else if (strcmp(optarg, "BYTEA") == 0)
					param.fmt = FORMAT_BYTEA;
				else
				{
					fprintf(stderr,
							"%s: only XML, TEXT or BYTEA types are supported\n",
							progname);
					exit(1);
				}
				break;
			case 'U':
				param.pg_user = pg_strdup(optarg);
				break;
			case 'w':
				param.pg_prompt = TRI_NO;
				break;
			case 'W':
				param.pg_prompt = TRI_YES;
				break;
			case 'p':
				port = strtol(optarg, NULL, 10);
				if ((port < 1) || (port > 65535))
				{
					fprintf(stderr, "%s: invalid port number: %s\n", progname, optarg);
					exit(1);
				}
				param.pg_port = pg_strdup(optarg);
				break;
			case 'h':
				param.pg_host = pg_strdup(optarg);
				break;
		}
	}

	/* No database given? Show usage */
	if (optind + 1 !=  argc)
	{
		fprintf(stderr, "pgimportdoc: missing required argument: database name\n");
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
		exit(1);
	}

	rc = pgimportdoc(argv[argc - 1], &param);
	return rc;
}
