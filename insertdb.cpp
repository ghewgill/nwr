#include <time.h>

#include <libpq-fe.h>

#include "eas_decode.h"

const char *Station = NULL;
PGconn *conn;

string strfv(const char format[], va_list args)
{
    char _buf[4096];
    char *buf = _buf;
    int size = sizeof(_buf);
    while (true) {
        int n = vsnprintf(buf, size, format, args);
        if (n < 0) { // handle broken old glibc behaviour
            size *= 2;
        } else if (n < size) {
            break;
        } else {
            size = n+1;
        }
        char *newbuf;
        if (buf == _buf) {
            newbuf = static_cast<char *>(malloc(size));
        } else {
            newbuf = static_cast<char *>(realloc(buf, size));
        }
        if (newbuf == NULL) {
            if (buf != _buf) {
                free(buf);
            }
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        buf = newbuf;
    }
    string r = buf;
    if (buf != _buf) {
        free(buf);
    }
    return r;
}

string timestr(time_t t, bool seconds = false)
{
    if (t == 0) {
        return "none";
    }
    struct tm *tm = gmtime(&t);
    char buf[40];
    if (seconds) {
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", 1900+tm->tm_year, 1+tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", 1900+tm->tm_year, 1+tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min);
    }
    return buf;
}

PGresult *query(const char query[], ...)
{
    va_list args;
    va_start(args, query);
    string sql = strfv(query, args);
    va_end(args);

    PGresult *r = PQexec(conn, sql.c_str());
    if (r == NULL || (PQresultStatus(r) != PGRES_COMMAND_OK && PQresultStatus(r) != PGRES_TUPLES_OK)) {
        printf("insertdb: sql error (%s) %s\n", PQresStatus(PQresultStatus(r)), PQresultErrorMessage(r));
        PQclear(r);
        exit(1);
    }
    return r;
}

int main(int argc, char *argv[])
{
    int a = 1;
    while (a < argc && argv[a][0] == '-' && argv[a][1] != 0) {
        switch (argv[a][1]) {
        case 's':
            if (argv[a][2]) {
                Station = argv[a]+2;
            } else {
                a++;
                Station = argv[a];
            }
            break;
        default:
            fprintf(stderr, "%s: unknown option %c\n", argv[0], argv[a][1]);
            exit(1);
        }
        a++;
    }
    if (argc-a < 2) {
        fprintf(stderr, "usage: insertdb code filename\n");
        exit(1);
    }
    eas::Message message;
    if (!eas::Decode(argv[a], message)) {
        fprintf(stderr, "insertdb: bad code\n");
        exit(1);
    }
    conn = PQconnectdb("dbname=nwr");
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "insertdb: connection failed (%d)\n", PQstatus(conn));
        exit(1);
    }
    PGresult *r = query("begin");
    PQclear(r);
    int id = time(0);
    r = query("insert into message (id, station, raw, originator, event, issued, received, purge, sender, filename) values (%ld, upper('%s'), '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s')",
        id,
        Station,
        message.raw.c_str(),
        message.originator.c_str(),
        message.event.c_str(),
        timestr(message.issued).c_str(),
        timestr(message.received).c_str(),
        timestr(message.purge).c_str(),
        message.sender.c_str(),
        argv[a+1]);
    PQclear(r);
    for (vector<eas::Message::Area>::iterator a = message.areas.begin(); a != message.areas.end(); a++) {
        r = query("insert into message_area (message_id, code, part, state, county) values (%d, '%s', %d, %d, %d)",
            id,
            a->code.c_str(),
            a->part,
            a->state,
            a->county);
        PQclear(r);
    }
    r = query("commit");
    PQclear(r);
    PQfinish(conn);
    return 0;
}
