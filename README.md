# pgimportdoc
command line utility for importing XML, JSON, BYTEA document to PostgreSQL

This PostgreSQL command line utility (extension) is used for importing XML, any text or
binary documents to PostgreSQL.

```
pgimportdoc -c 'insert into xmltab(x) values($1)' -t XML -f myxmldoc
```

for help run
```
pgimportdoc --help
```

XML documents are read in binary format - if XML doc has a header with encoding, then Postgres
ensures encoding from XML encoding to PostgreSQL encoding.

Text documents are read in text format - there are translation from client encoding to
PostgreSQL server encoding.

When format is BYTEA, then passing data are in bytea escaped text format.

Attention: The imported documents are completly loaded to client's memory. So you need enough free
memory on client, when you would to use this tool. Maximal teoretical size of imported document
is 1GB. More practical real maximal size is about 100MB.

ToDo:

* More input files support - options -f1 xxx -f2 xxx ... insert into .. values( $1, $2 )
* using LO API for passing binary data
* show returned result when RETURNING clause is used
* encoding option - possibility to specify encoding
* regress tests

Examples:

```
[pavel@localhost pgimportdoc]$ ./pgimportdoc postgres -f ~/Stažené/enprimeur.xml -c 'insert into xmldata values($1)' -t XML
[pavel@localhost pgimportdoc]$ cat ~/Stažené/enprimeur.xml | ./pgimportdoc postgres -c 'insert into xmldata values($1)' -t XML
```
