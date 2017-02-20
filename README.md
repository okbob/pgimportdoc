# pgimportdoc
command line utility for importing XML, JSON, BYTEA document to PostgreSQL

This PostgreSQL command line utility (extension) is used for importing XML, any text or
binary documents to PostgreSQL.

```
pgimportdoc -c "insert into xmltab(x) VALUES($1)" -t XML -f myxmldoc
```

XML documents are read in binary format - if XML doc has a header with encoding, then Postgres
ensures encoding from XML encoding to PostgreSQL encoding.

Text documents are read in text format - there are translation from client encoding to
PostgreSQL server encoding.

When format is BYTEA, then passing is in binary format.

Attention: The imported documents are completly loaded to client's memory. So you need enough free
memory on client, when you would to use this tool. Maximal teoretical size of imported document
is 1GB. More practical real maximal size is about 100MB.
