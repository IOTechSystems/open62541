/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information. */

#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>

#include <stdio.h>
#include <stdlib.h>

/**
 * Using Alarms and Conditions Server
 * ----------------------------------
 *
 * Besides the usage of monitored items and events to observe the changes in the
 * server, it is also important to make use of the Alarms and Conditions Server
 * Model. Alarms are events which are triggered automatically by the server
 * dependent on internal server logic or user specific logic when the states of
 * server components change. The state of a component is represented through a
 * condition. So the values of all the condition children (Fields) are the
 * actual state of the component.
 *
 * Trigger Alarm events by changing States
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * The following example will be based on the server events tutorial. Please
 * make sure to understand the principle of normal events before proceeding with
 * this example! */

static UA_NodeId conditionSource;

static UA_NodeId conditionInstance_1;

static UA_StatusCode
addConditionSourceObject(UA_Server *server) {
    UA_ObjectAttributes object_attr = UA_ObjectAttributes_default;
    object_attr.eventNotifier = 1;

    object_attr.displayName = UA_LOCALIZEDTEXT("en", "ConditionSourceObject");
    UA_StatusCode retval =  UA_Server_addObjectNode(server, UA_NODEID_NULL,
                                      UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                                      UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                                      UA_QUALIFIEDNAME(0, "ConditionSourceObject"),
                                      UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
                                      object_attr, NULL, &conditionSource);

    if(retval != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                     "Creating Condition Source failed. StatusCode %s",
                     UA_StatusCode_name(retval));
        return retval;
    }

    /* ConditionSource should be EventNotifier of another Object (usually the
     * Server Object). If this Reference is not created by user then the A&C
     * Server will create "HasEventSource" reference to the Server Object
     * automatically when the condition is created*/
    retval = UA_Server_addReference(server, UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
                                     UA_NODEID_NUMERIC(0, UA_NS0ID_HASNOTIFIER),
                                     UA_EXPANDEDNODEID_NUMERIC(conditionSource.namespaceIndex,
                                                               conditionSource.identifier.numeric),
                                     UA_TRUE);

    return retval;
}

/**
 * The Alarm is evaluated whenever its InputNode is written. */

static void
afterInputNodeWrite (UA_Server *server,
                     const UA_NodeId *sessionId, void *sessionContext,
                     const UA_NodeId *nodeId, void *nodeContext,
                     const UA_NumericRange *range, const UA_DataValue *data)
{
    if(!UA_Variant_hasScalarType(&data->value, &UA_TYPES[UA_TYPES_DOUBLE]))
        return;
    UA_Server_exclusiveLimitAlarmEvaluate_default(server, &conditionInstance_1, NULL,
                                                  (UA_Double*) data->value.data);
}

static UA_StatusCode
getInputNodeValue (UA_Server *server, UA_NodeId conditionId, UA_Variant *out)
{
    UA_QualifiedName inputNodeQN = UA_QUALIFIEDNAME(0, "InputNode");
    UA_BrowsePathResult bpr = UA_Server_browseSimplifiedBrowsePath(server, conditionId, 1, &inputNodeQN);
    if (bpr.statusCode != UA_STATUSCODE_GOOD || bpr.targetsSize != 1) {
        UA_StatusCode status = bpr.statusCode != UA_STATUSCODE_GOOD ?
            bpr.statusCode : UA_STATUSCODE_BADNOTFOUND;
        UA_BrowsePathResult_clear(&bpr);
        return status;
    }
    UA_Variant sourceNodeId;
    UA_StatusCode status = UA_Server_readValue(server, bpr.targets[0].targetId.nodeId, &sourceNodeId);
    UA_BrowsePathResult_clear(&bpr);
    if (status != UA_STATUSCODE_GOOD) return status;
    if (!UA_Variant_hasScalarType(&sourceNodeId, &UA_TYPES[UA_TYPES_NODEID])) {
        UA_Variant_clear(&sourceNodeId);
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }
    status = UA_Server_readValue(server, *(UA_NodeId*) sourceNodeId.data, out);
    UA_Variant_clear(&sourceNodeId);
    return status;
}

/**
 * The evaluate functions read the InputNode when the Alarm is enabled. */

static void *
sourceNodeGetInputDouble (UA_Server *server, const UA_NodeId *conditionId, void *conditionCtx)
{
    UA_Variant val;
    UA_Variant_init (&val);
    UA_StatusCode ret = getInputNodeValue(server, *conditionId, &val);
    if (ret != UA_STATUSCODE_GOOD || !UA_Variant_hasScalarType(&val, &UA_TYPES[UA_TYPES_DOUBLE]))
    {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                     "Getting input value from condition InputNode failed. StatusCode %s",
                     UA_StatusCode_name(ret));
        UA_Variant_clear(&val);
        return NULL;
    }
    UA_Double *doubleVal = (UA_Double *) val.data;
    val.data = NULL;
    UA_Variant_clear(&val);
    return doubleVal;
}

static void
sourceNodeInputFreeDouble (void *input, void *conditionCtx)
{
    UA_Double_delete((UA_Double *) input);
}

/**
 * The Alarm needs to be acknowledged whenever it becomes active. */

static UA_StatusCode
onCondition1Active (UA_Server *server, const UA_NodeId *conditionId, void *context)
{
    UA_NodeId ackedId;
    UA_StatusCode retval = UA_Server_getNodeIdWithBrowseName(server, conditionId,
                                                             UA_QUALIFIEDNAME(0, "AckedState"), &ackedId);
    if (retval != UA_STATUSCODE_GOOD) return retval;
    retval = UA_Server_writeTwoStateVariable(server, ackedId, UA_LOCALIZEDTEXT("en", "Unacknowledged"), false);
    UA_NodeId_clear (&ackedId);
    return retval;
}

static const UA_ConditionCallbacks condition1Impl = {
    .onActive = onCondition1Active
};

static UA_StatusCode
addExclusiveLimitAlarmCondition (UA_Server *server) {
    UA_NodeId inputNode;
    UA_Double val = 100;
    UA_VariableAttributes vatt = UA_VariableAttributes_default;
    vatt.dataType = UA_NODEID_NUMERIC(0, UA_NS0ID_DOUBLE);
    UA_Variant_setScalar(&vatt.value, &val, &UA_TYPES[UA_TYPES_DOUBLE]);
    vatt.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

    UA_StatusCode retval = UA_Server_addVariableNode(
        server, UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        UA_QUALIFIEDNAME(0, "InputNode"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        vatt,
        NULL,
        &inputNode
    );
    if (retval != UA_STATUSCODE_GOOD) return retval;

    UA_ValueSourceNotifications notifications = {
        .onWrite = afterInputNodeWrite
    };
    retval = UA_Server_setVariableNode_internalValueSource (server, inputNode, NULL, &notifications);
    if (retval != UA_STATUSCODE_GOOD) return retval;

    UA_CreateConditionProperties properties = {
        .sourceNode = conditionSource,
        .browseName = UA_QUALIFIEDNAME(0, "Exclusive Limit Alarm Condition"),
        .hierarchialReferenceType = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        .parentNodeId = conditionSource
    };

    UA_AlarmConditionProperties baseP = {
        .acknowledgeableConditionProperties = {
            .confirmable = true
        },
        .inputNode = inputNode,
        .isLatching = true,
        .isShelvable = true
    };

    UA_LevelAlarmProperties alarmProperties = {
        .alarmConditionProperties = baseP,
        .hasHighLimit = true,
        .highLimit = 20.0
    };

    retval = UA_Server_createExclusiveLevelAlarm (
        server,
        UA_NODEID_NULL,
        &properties,
        &alarmProperties,
        &conditionInstance_1
    );
    if (retval != UA_STATUSCODE_GOOD) return retval;

    UA_ConditionEvaluateFns fns;
    fns.getInput = sourceNodeGetInputDouble;
    fns.inputFree = sourceNodeInputFreeDouble;
    fns.evaluate = (UA_ConditionEvaluateFn) UA_Server_exclusiveLimitAlarmEvaluate_default;
    retval = UA_Server_Condition_setEvaluateFns(server, conditionInstance_1, fns);
    if (retval != UA_STATUSCODE_GOOD) return retval;

    retval = UA_Server_Condition_setCallbacks(server, conditionInstance_1, &condition1Impl);
    if (retval != UA_STATUSCODE_GOOD) return retval;

    return UA_Server_Condition_enable (server, conditionInstance_1, true, NULL);
}

static UA_StatusCode
setUpEnvironment(UA_Server *server) {
    UA_StatusCode retval = addConditionSourceObject(server);
    if (retval != UA_STATUSCODE_GOOD) return retval;
    retval = addExclusiveLimitAlarmCondition(server);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                     "Creating the Alarm failed. StatusCode %s",
                     UA_StatusCode_name(retval));
    }
    return retval;
}

/**
 * It follows the main server code, making use of the above definitions. */

int main (void) {
    UA_Server *server = UA_Server_new();
    UA_StatusCode retval = setUpEnvironment(server);
    if (retval == UA_STATUSCODE_GOOD)
        retval = UA_Server_runUntilInterrupt(server);
    UA_Server_delete(server);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}
