/*
 * This code was created by Peter Harvey (mostly during Christmas 98/99).
 * This code is LGPL. Please ensure that this message remains in future
 * distributions and uses of this code (thats about all I get out of it).
 * - Peter Harvey pharvey@codebydesign.com
 * 
 * This file has been modified for the MonetDB project.  See the file
 * Copyright in this directory for more information.
 */

/**********************************************************************
 * SQLExecute()
 * CLI Compliance: ISO 92
 *
 * Author: Martin van Dinther
 * Date  : 30 Aug 2002
 *
 **********************************************************************/

#include "ODBCGlobal.h"
#include "ODBCStmt.h"

static char *
convert(char *str)
{
	char *res = NULL;
	size_t i, len;

	for (len = 1, i = 0; str[i]; i++, len++) {
		if (str[i] == '\'')
			len++;
	}
	res = malloc(len);
	for (len = 0, i = 0; str[i]; i++) {
		if (str[i] == '\'') {
			res[len++] = '\\';
			len++;
		}
		res[len++] = str[i];
	}
	res[len] = '\0';
	return res;
}

static int
next_result(stream *rs, ODBCStmt *hstmt, int *type)
{
	int status;

	if (!stream_readInt(rs, type) || *type == Q_END) {
		/* 08S01 = Communication link failure */
		addStmtError(hstmt, "08S01", NULL, 0);
		return SQL_ERROR;
	}

	stream_readInt(rs, &status);	/* read result size (is < 0 on error) */
	if (*type < 0 || status < 0) {
		/* output error */
		char buf[BLOCK + 1];
		int last = 0;

		(void) bs_read_next(rs, buf, &last);
		/* read result string (not used) */
		while (!last) {
			(void) bs_read_next(rs, buf, &last);
		}
		/* HY000 = General Error */
		addStmtError(hstmt, "HY000",
			     "No result available (status < 0)", 0);
		return SQL_ERROR;
	}
	return status;
}

struct sql_types {
	char *name;
	int type;
} sql_types[] = {
	{"bit", SQL_C_BIT},
	{"uchr", SQL_C_UTINYINT},
	{"char", SQL_C_CHAR},
	{"sht", SQL_C_SSHORT},
	{"int", SQL_C_SLONG},
	{"lng", SQL_C_SBIGINT},
	{"flt", SQL_C_FLOAT},
	{"dbl", SQL_C_DOUBLE},
	{"date", SQL_C_TYPE_DATE},
	{"time", SQL_C_TYPE_TIME},
	{"timestamp", SQL_C_TYPE_TIMESTAMP},
	{0, 0},			/* sentinel */
};

SQLRETURN
SQLExecute(SQLHSTMT hStmt)
{
	ODBCStmt *hstmt = (ODBCStmt *) hStmt;
	ODBCDbc *dbc = NULL;
	char *query = NULL;

	int nCol = 0;
	int nRow = 0;
	int nCols = 0;
	int nRows = 0;
	int type = 0;
	int status = 0;
	stream *rs;

	if (!isValidStmt(hstmt))
		return SQL_INVALID_HANDLE;

	clearStmtErrors(hstmt);

	/* check statement cursor state, query should be prepared */
	if (hstmt->State != PREPARED) {
		/* 24000 = Invalid cursor state */
		addStmtError(hstmt, "24000", NULL, 0);
		return SQL_ERROR;
	}

	/* internal state correctness checks */
	assert(hstmt->Query != NULL);
	assert(hstmt->ResultCols == NULL);
	assert(hstmt->ResultRows == NULL);

	dbc = hstmt->Dbc;
	assert(dbc);
	assert(dbc->Mrs);
	assert(dbc->Mws);

	query = hstmt->Query;
	/* Send the Query to the server for execution */
	if (hstmt->bindParams.size) {
		char *Query = 0;
		int i = 0, params = 1;
		int queryLen = strlen(hstmt->Query) + 1;
		char *oldquery = strdup(hstmt->Query);
		char **strings = (char **) calloc((size_t) hstmt->bindParams.size,
						  sizeof(char *));

		for (i = 1; i <= hstmt->bindParams.size; i++) {
			if (!hstmt->bindParams.array[i])
				break;

			strings[i] = convert(hstmt->bindParams.array[i]->ParameterValuePtr);
			queryLen += 2 + strlen(strings[i]);
		}
		Query = malloc(queryLen);
		Query[0] = '\0';
		i = 0;
		query = oldquery;
		while (query && *query) {
			/* problem with strings with ?s */
			char *old = query;

			if ((query = strchr(query, '?')) != NULL) {
				*query = '\0';
				if (!hstmt->bindParams.array[params])
					break;
				i += snprintf(Query + i, queryLen - i,
					      "%s'%s'", old, strings[params]);
				query++;
				old = query;
				params++;
			}
			if (old && *old != '\0')
				i += snprintf(Query + i, queryLen - i, "%s",
					      old);
			Query[i] = '\0';
		}
		for (i = 0; i < hstmt->bindParams.size; i++) {
			if (strings[i])
				free(strings[i]);
		}
		free(strings);
		free(oldquery);
		query = Query;
	}

	stream_write(dbc->Mws, query, 1, strlen(query));
	stream_write(dbc->Mws, ";\n", 1, 2);
	stream_flush(dbc->Mws);

	/* now get the result data and store it to our internal data structure */

	/* initialize the Result meta data values */
	hstmt->nrCols = 0;
	hstmt->nrRows = 0;
	hstmt->currentRow = 0;

	rs = dbc->Mrs;
	status = next_result(rs, hstmt, &type);
	if (status == SQL_ERROR)
		return status;

	if (type == Q_RESULT && status > 0) {	/* header info */
		char *sc, *ec;
		bstream *bs = bstream_create(rs, BLOCK);
		int eof = 0;
		int id = 0;
		ColumnHeader *pCol;

		stream_readInt(rs, &id);

		nCols = status;

		hstmt->nrCols = nCols;
		hstmt->ResultCols = NEW_ARRAY(ColumnHeader, (nCols + 1));
		memset(hstmt->ResultCols, 0,
		       (nCols + 1) * sizeof(ColumnHeader));
		pCol = hstmt->ResultCols + 1;

		eof = (bstream_read(bs, bs->size - (bs->len - bs->pos)) == 0);
		sc = bs->buf + bs->pos;
		ec = bs->buf + bs->len;

		while (sc < ec) {
			char *s, *name = NULL, *type = NULL;
			struct sql_types *p;

			s = sc;
			while (sc < ec && *sc != ',')
				sc++;
			if (sc >= ec && !eof) {
				bs->pos = s - bs->buf;
				eof = (bstream_read(bs, bs->size - (bs->len - bs->pos)) == 0);
				sc = bs->buf + bs->pos;
				ec = bs->buf + bs->len;

				continue;
			} else if (eof) {
				/* TODO: set some error message */
				break;
			}

			*sc = 0;
			name = strdup(s);
			sc++;
			s = sc;
			while (sc < ec && *sc != '\n')
				sc++;
			if (sc >= ec && !eof) {
				bs->pos = s - bs->buf;
				eof = (bstream_read(bs, bs->size - (bs->len - bs->pos)) == 0);
				sc = bs->buf + bs->pos;
				ec = bs->buf + bs->len;

				while (sc < ec && *sc != '\n')
					sc++;
				if (sc >= ec) {
					/* TODO: set some error message */
					break;
				}
			} else if (eof) {
				/* TODO: set some error message */
				break;
			}
			*sc = 0;
			type = strdup(s);
			sc++;

			for (p = sql_types; p->name; p++) {
				if (strcmp(p->name, type) == 0) {
					pCol->nSQL_DESC_TYPE = p->type;
					break;
				}
			}
			pCol->pszSQL_DESC_BASE_COLUMN_NAME = name;
			pCol->pszSQL_DESC_BASE_TABLE_NAME = strdup("tablename");
			pCol->pszSQL_DESC_TYPE_NAME = type;
			pCol->pszSQL_DESC_LOCAL_TYPE_NAME = strdup("Mtype");
			pCol->pszSQL_DESC_LABEL = strdup(name);
			pCol->pszSQL_DESC_CATALOG_NAME = strdup("catalog");
			pCol->pszSQL_DESC_LITERAL_PREFIX = strdup("pre");
			pCol->pszSQL_DESC_LITERAL_SUFFIX = strdup("suf");
			pCol->pszSQL_DESC_NAME = strdup(name);
			pCol->pszSQL_DESC_SCHEMA_NAME = strdup("schema");
			pCol->pszSQL_DESC_TABLE_NAME = strdup("table");
			pCol->nSQL_DESC_DISPLAY_SIZE = strlen(name) + 2;
			pCol++;
		}
		bstream_destroy(bs);

		status = next_result(rs, hstmt, &type);
		if (status == SQL_ERROR)
			return status;
	}
	if (type == Q_TABLE && status > 0) {
		char *sc, *ec;
		bstream *bs = bstream_create(rs, BLOCK);
		int eof = 0;

		nRows = status;

		hstmt->nrRows = nRows;
		hstmt->ResultRows = NEW_ARRAY(char *,
					      (nCols + 1) * (nRows + 1));
		memset(hstmt->ResultRows, 0, (nCols + 1) * (nRows + 1));
		assert(hstmt->ResultRows != NULL);

		/* Next copy data from all columns for all rows */
		eof = (bstream_read(bs, bs->size - (bs->len - bs->pos)) == 0);
		sc = bs->buf + bs->pos;
		ec = bs->buf + bs->len;

		for (nRow = 1; nRow <= nRows && !eof; nRow++) {
			for (nCol = 1; nCol <= nCols && !eof; nCol++) {
				char *s = sc;

				while (sc < ec && *sc != '\t' && *sc != '\n')
					sc++;
				if (sc >= ec && !eof) {
					bs->pos = s - bs->buf;
					eof = (bstream_read(bs, bs->size - (bs->len - bs->pos)) == 0);
					sc = bs->buf + bs->pos;
					ec = bs->buf + bs->len;

					while (sc < ec && *sc != '\t' &&
					       *sc != '\n')
						sc++;
					if (sc >= ec) {
						bstream_destroy(bs);

						return SQL_ERROR;
					}
				}
				*sc = '\0';
				if (*s == '\"' && *(sc - 1) == '\"') {
					s++;
					*(sc - 1) = '\0';
				}
				if (*s == '\'' && *(sc - 1) == '\'') {
					s++;
					*(sc - 1) = '\0';
				}
				hstmt->ResultRows[nRow * nCols + nCol] = strdup(s);
				sc++;
			}
		}

		bstream_destroy(bs);
	} else if (Q_UPDATE) {
		hstmt->nrRows = nRows;
		hstmt->ResultRows = NULL;
	}

	hstmt->State = EXECUTED;
	return SQL_SUCCESS;
}
