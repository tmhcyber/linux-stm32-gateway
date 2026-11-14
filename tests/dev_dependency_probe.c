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
    /* Compile/link the exact synchronous-loop API used by stage 4; no connection. */
    if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS) return 1;
    struct mosquitto *client = mosquitto_new("gateway-build-probe", true, NULL);
    if (!client) { mosquitto_lib_cleanup(); return 1; }
    int result = mosquitto_threaded_set(client, true);
    if (!result) result = mosquitto_max_inflight_messages_set(client, 3);
    mosquitto_destroy(client);
    mosquitto_lib_cleanup();
    if (result) return 1;
    return 0;
}
