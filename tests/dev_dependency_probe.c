/* Build-only readiness probe; no gateway, storage writes, or MQTT traffic. */
#include <stdio.h>
#include <sqlite3.h>
#include <mosquitto.h>

int main(void)
{
    int major, minor, revision;
    mosquitto_lib_version(&major, &minor, &revision);
    printf("SQLite %s; libmosquitto %d.%d.%d\n",
           sqlite3_libversion(), major, minor, revision);
    return 0;
}
