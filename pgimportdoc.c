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
#include "pqexpbuffer.h"

#include "catalog/pg_type.h"

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
	bool		use_stdin;
	char	   *filename;
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
	FILE	   *input;
	char		buffer[BUFSIZE];
	size_t		size;
	PQExpBufferData data;
	PGresult	*result = NULL;
	Oid			ptypes[10];
	int			pformats[10];
	const char * pvalues[10];
	int			plengths[10];
	ExecStatusType status;

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

	if (param->use_stdin)
	{
		input = stdin;
	}
	else
	{
		struct stat		fst;

		canonicalize_path(param->filename);

		input = fopen(param->filename,"rb");
		if (NULL == input)
		{
			fprintf(stderr, "%s: Unable to open '%s': %s\n",
				param->progname, param->filename, strerror(errno));
			PQfinish(conn);
			return -1;
		}

		if (fstat(fileno(input), &fst) != -1)
		{
			if (S_ISREG(fst.st_mode) && fst.st_size > ((int64) 1024) * 1024 * 1024)
			{
				fprintf(stderr, "%s: '%s' is too big (greather than 1GB)\n",
					param->progname, param->filename);
				PQfinish(conn);
				return -1;
			}
		}
		else
		{
			fprintf(stderr, "%s: %s\n",
				param->progname, strerror(errno));
			PQfinish(conn);
			return -1;
		}
	}

	initPQExpBuffer(&data);

	while ((size = fread(buffer, 1, sizeof(buffer), input)) > 0)
		appendBinaryPQExpBuffer(&data, buffer, size);

	if (ferror(input))
	{
		fprintf(stderr, "%s: Cannot read data '%s': %s\n",
				param->progname, param->filename, strerror(errno));
		PQfinish(conn);
		return -1;
	}
	else if (PQExpBufferBroken(&data))
	{
		fprintf(stderr, "%s: Out of memory\n",
				param->progname);
		PQfinish(conn);
		return -1;
	}

	fclose(input);

	if (param->verbose)
	{
		fprintf(stdout, "Buffered data of size: %ld\n", data.len);
	}

	if (param->fmt == FORMAT_XML || param->fmt == FORMAT_BYTEA)
	{
		ptypes[0] = param->fmt == FORMAT_XML ? XMLOID : BYTEAOID;
		plengths[0] = data.len;
		pformats[0] = 1;
		pvalues[0] = data.data;

		result = PQexecParams(conn,
								param->command,
								1, ptypes, pvalues, plengths, pformats,
								0);
	}
	else if (param->fmt == FORMAT_TEXT)
	{
		pvalues[0] = data.data;

		result = PQexecParams(conn,
								param->command,
								1, NULL, pvalues, NULL, NULL,
								0);
	}

	status = PQresultStatus(result);

	if (param->verbose)
	{
		fprintf(stdout, "Result status: %s\n", PQresStatus(status));
	}

	if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK)
	{
		fprintf(stderr, "%s: Unexpected result status: %s\n",
				param->progname, PQresStatus(status));
		fprintf(stderr, "%s: Error: %s\n",
				param->progname, PQresultErrorMessage(result));
		PQfinish(conn);
		return -1;
	}

	PQclear(result);

	termPQExpBuffer(&data);
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
	printf("  -f file        file name of imported document, default is stdin\n");
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
	param.verbose = 0;
	param.pg_user = NULL;
	param.pg_prompt = TRI_DEFAULT;
	param.fmt = FORMAT_TEXT;
	param.pg_host = NULL;
	param.pg_port = NULL;
	param.progname = progname;
	param.use_stdin = true;
	param.filename = NULL;
	param.command = NULL;

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
		c = getopt(argc, argv, "h:f:U:p:c:t:vwW");
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
			case 'f':
				if (strcmp(optarg, "-") != 0)
				{
					param.filename = pg_strdup(optarg);
					param.use_stdin = false;
				}
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

	if (param.command == NULL)
	{
		fprintf(stderr, "pgimportdoc: missing required argument: -c command\n");
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
		exit(1);
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
