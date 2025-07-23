/*
 * This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information.
 */

#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <signal.h>
#include <stdlib.h>

UA_Boolean running = true;
static void stopHandler(int sign) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "received ctrl-c");
    running = false;
}

char * allocate_format_string (const char* format, ...)
{
    char *str = NULL;
    va_list args;
    va_list args_copy;

    va_start (args, format);
    va_copy (args_copy, args);
    size_t size = vsnprintf (NULL, 0, format, args);
    va_end (args);

    str = calloc (1u, size+1);
    vsnprintf (str, size+1, format, args_copy);
    va_end (args_copy);
    return str;
}

static UA_StatusCode
readValueCb(UA_Server *server,
                const UA_NodeId *sessionId, void *sessionContext,
                const UA_NodeId *nodeId, void *nodeContext,
                UA_Boolean sourceTimeStamp, const UA_NumericRange *range,
                UA_DataValue *dataValue) {
    uint64_t *val = nodeContext;
    (*val)++;
    UA_Variant_setScalarCopy(&dataValue->value, val,
                             &UA_TYPES[UA_TYPES_UINT64]);
    dataValue->hasValue = true;
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode setup_nodes(UA_Server *server)
{
    uint32_t NODE_COUNT = 100;
    uint32_t DEV_COUNT = 100;
    uint32_t METRIC_COUNT = 10;

    UA_NodeId container_id = UA_NODEID_STRING(1, "Container");
    UA_ObjectAttributes objectAttr = UA_ObjectAttributes_default;
    UA_StatusCode retval = UA_Server_addObjectNode (
       server,                    // Server instance
       container_id,
       UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),              // Parent NodeId
       UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),           // Reference type
       UA_QUALIFIEDNAME(1, "Container"), // Browse name
       UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), // Type definition
       objectAttr,                // Object attributes
       NULL,                      // Node context (optional)
       NULL                       // Output NodeId (optional)
    );
    if (retval != UA_STATUSCODE_GOOD) return retval;

    for (uint32_t i = 0; i<NODE_COUNT; i++)
    {
        char *node_name = allocate_format_string("node-%u", i);
        UA_NodeId node_node_id = UA_NODEID_STRING(1, node_name);
        UA_StatusCode retval = UA_Server_addObjectNode (
           server,                    // Server instance
           node_node_id,
           container_id,
           UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),           // Reference type
           UA_QUALIFIEDNAME(1, node_name), // Browse name
           UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), // Type definition
           objectAttr,                // Object attributes
           NULL,                      // Node context (optional)
           NULL                       // Output NodeId (optional)
        );
        if (retval != UA_STATUSCODE_GOOD) return retval;

        for (uint32_t j = 0; j<DEV_COUNT; j++)
        {
            char *dev_name= allocate_format_string("node-%u-device-%u", i, j);
            UA_NodeId dev_node_id = UA_NODEID_STRING(1, dev_name);
            retval = UA_Server_addObjectNode (
               server,                    // Server instance
               dev_node_id,
               node_node_id,
               UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),           // Reference type
               UA_QUALIFIEDNAME(1, dev_name), // Browse name
               UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), // Type definition
               objectAttr,                // Object attributes
               NULL,                      // Node context (optional)
               NULL                       // Output NodeId (optional)
            );
            if (retval != UA_STATUSCODE_GOOD) return retval;

            for (uint32_t k = 0; k<METRIC_COUNT; k++) {
                char *metric_name = allocate_format_string("node-%u-device-%u-metric-%u", i, j, k);
                UA_NodeId metric_node_id = UA_NODEID_STRING(1, metric_name);

                uint64_t *value = malloc(sizeof(*value));
                *value = 0;

                UA_VariableAttributes varAttr = UA_VariableAttributes_default;
                UA_Variant_setScalarCopy (&varAttr.value, value, &UA_TYPES[UA_TYPES_UINT64]);
                varAttr.accessLevel = UA_ACCESSLEVELMASK_READ;
                retval = UA_Server_addVariableNode (
                    server,
                    metric_node_id,
                    dev_node_id,              // Parent is our custom object
                    UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                    UA_QUALIFIEDNAME(1, metric_name),
                    UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                    varAttr,
                    value,
                    NULL
                );
                if (retval != UA_STATUSCODE_GOOD) return retval;

                UA_DataSource source;
                source.read = readValueCb;
                retval = UA_Server_setVariableNode_dataSource (
                    server,
                    metric_node_id,
                    source
                );
                if (retval != UA_STATUSCODE_GOOD) return retval;
                free (metric_name);
            }
            free (dev_name);
        }
        free (node_name);
    }


    return UA_STATUSCODE_GOOD;
}

int main(int argc, char** argv) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_Server *server = UA_Server_new();
    UA_ServerConfig_setDefault(UA_Server_getConfig(server));


    if (setup_nodes(server) != UA_STATUSCODE_GOOD) return EXIT_FAILURE;



    UA_StatusCode retval = UA_Server_run_startup(server);
    if(retval != UA_STATUSCODE_GOOD)
        goto cleanup;
    uint64_t count = 0;
    UA_DateTime total = 0;
    while(running) {
        UA_DateTime start = UA_DateTime_nowMonotonic();
        UA_Server_run_iterate (server, true);
        total += UA_DateTime_nowMonotonic() - start;
        count++;

        if (count == 10) {
            fprintf (stderr, "Server average iter time = %lu samples=%lu \n", total/count, count);
            count = 0;
            total = 0;
        }


    }
    retval = UA_Server_run_shutdown(server);

 cleanup:
    UA_Server_delete(server);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}
