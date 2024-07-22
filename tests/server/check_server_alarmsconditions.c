/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 *    Copyright 2020 (c) Christian von Arnim, ISW University of Stuttgart (for VDW and umati)
 */

#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <check.h>
#include <stdlib.h>
#include <stdio.h>

UA_Server *acserver;

static void setup(void) {
    acserver = UA_Server_new();
    UA_ServerConfig_setDefault(UA_Server_getConfig(acserver));
}

static void teardown(void) {
    UA_Server_delete(acserver);
}

#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

static UA_Boolean
isConditionTwoStateVariableInTrueState (UA_Server *server, UA_NodeId condition, UA_QualifiedName twoStateVariableName)
{
    UA_Boolean state = false;
    UA_NodeId stateNodeId;
    UA_StatusCode status = UA_Server_getNodeIdWithBrowseName(server, &condition, twoStateVariableName, &stateNodeId);
    assert (status == UA_STATUSCODE_GOOD);

    UA_NodeId stateIdNodeId;
    status = UA_Server_getNodeIdWithBrowseName(server, &stateNodeId, UA_QUALIFIEDNAME(0, "Id"), &stateIdNodeId);
    UA_NodeId_clear(&stateNodeId);
    assert (status == UA_STATUSCODE_GOOD);

    UA_Variant val;
    status = UA_Server_readValue(server, stateIdNodeId, &val);
    UA_NodeId_clear(&stateIdNodeId);
    assert (status == UA_STATUSCODE_GOOD);
    assert (val.data != NULL && val.type == &UA_TYPES[UA_TYPES_BOOLEAN]);
    state = *(UA_Boolean*)val.data;
    UA_Variant_clear(&val);
    return state;
}

static inline UA_Boolean
isConditionEnabled (UA_Server *server, UA_NodeId condition)
{
    return isConditionTwoStateVariableInTrueState(
        server,
        condition,
        UA_QUALIFIEDNAME(0, "EnabledState")
    );
}

static inline UA_Boolean
isConditionActive (UA_Server *server, UA_NodeId condition)
{
    return isConditionTwoStateVariableInTrueState(
        server,
        condition,
        UA_QUALIFIEDNAME(0, "ActiveState")
    );
}

static inline UA_Boolean
isConditionAcked(UA_Server *server, UA_NodeId condition)
{
    return isConditionTwoStateVariableInTrueState(
        server,
        condition,
        UA_QUALIFIEDNAME(0, "AckedState")
    );
}

static inline UA_Boolean
isConditionConfirmed (UA_Server *server, UA_NodeId condition)
{
    return isConditionTwoStateVariableInTrueState(
        server,
        condition,
        UA_QUALIFIEDNAME(0, "ConfirmedState")
    );
}

static inline UA_Boolean
conditionRetain (UA_Server *server, UA_NodeId condition)
{
    UA_Variant val;
    UA_StatusCode ret = UA_Server_readObjectProperty(
        server,
        condition,
        UA_QUALIFIEDNAME(0, "Retain"),
        &val
    );
    assert (ret == UA_STATUSCODE_GOOD && val.type == &UA_TYPES[UA_TYPES_BOOLEAN]);
    UA_Boolean retain = *(UA_Boolean *)val.data;
    UA_Variant_clear(&val);
    return retain;
}

START_TEST(createDelete) {
    UA_StatusCode retval;

    UA_ConditionProperties conditionProperties;
    conditionProperties.name = UA_QUALIFIEDNAME(0, "Condition createDelete");
    conditionProperties.hierarchialReferenceType = UA_NODEID_NULL;
    conditionProperties.source = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);

    UA_ConditionInputFns inputs = {0};
    // Loop to increase the chance of capturing dead pointers
    for(UA_UInt16 i = 0; i < 3; ++i)
    {
        UA_NodeId conditionInstance = UA_NODEID_NULL;
        retval = __UA_Server_createCondition(
            acserver,
            UA_NODEID_NULL,
            UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE),
            &conditionProperties,
            inputs,
            NULL,
            NULL,
            &conditionInstance
        );
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
        ck_assert_msg(!UA_NodeId_isNull(&conditionInstance), "ConditionId is null");

        retval = UA_Server_deleteCondition(
            acserver,
            conditionInstance
        );
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    }
} END_TEST

static void onAcked(UA_Server *server, const UA_NodeId *id, void *ctx)
{
    UA_Server_Condition_setConfirmRequired(server, *id);
}

static void eventCB (UA_Server *server, UA_UInt32 monId, void *monContext,
     size_t nEventFields, const UA_Variant *eventFields)
{

    UA_String idString;
    UA_String branchIdString;
    UA_String_init(&idString);
    UA_String_init(&branchIdString);
    UA_NodeId conditionId = *(UA_NodeId *) eventFields[0].data;
    UA_NodeId branchId = *(UA_NodeId *) eventFields[1].data;
    UA_NodeId_print(&conditionId, &idString);
    UA_NodeId_print(&branchId, &branchIdString);
    fprintf (stderr, "EVENT: conditionId '%.*s' branchId: '%.*s'\n", UA_PRINTF_STRING_DATA(idString), UA_PRINTF_STRING_DATA(branchIdString));

}

/* Based on https://reference.opcfoundation.org/Core/Part9/v105/docs/B.1.3 */
START_TEST(eventSequence) {
    UA_StatusCode retval;
    UA_ConditionProperties conditionProperties;
    conditionProperties.name = UA_QUALIFIEDNAME(0, "Test Condition");
    conditionProperties.hierarchialReferenceType = UA_NODEID_NULL;
    conditionProperties.source = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);

    UA_AlarmConditionProperties alarmProperties;
    memset (&alarmProperties, 0, sizeof(alarmProperties));
    alarmProperties.acknowledgeableConditionProperties.confirmable = true;

    UA_ConditionInputFns inputs = {0};
    UA_NodeId conditionInstance = UA_NODEID_NULL;
    retval = __UA_Server_createCondition(
            acserver,
            UA_NODEID_NULL,
            UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE),
            &conditionProperties,
            inputs,
            NULL,
            &alarmProperties,
            &conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_MonitoredItemCreateRequest req;
    UA_MonitoredItemCreateRequest_init(&req);
    req.itemToMonitor.nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    req.monitoringMode = UA_MONITORINGMODE_REPORTING;
    req.itemToMonitor.attributeId = UA_ATTRIBUTEID_EVENTNOTIFIER;
    req.requestedParameters.samplingInterval = 250;
    req.requestedParameters.discardOldest = true;
    req.requestedParameters.queueSize = 1;

    UA_SimpleAttributeOperand select[2];
    UA_SimpleAttributeOperand_init(&select[0]);
    select[0].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    select[0].attributeId = UA_ATTRIBUTEID_NODEID;


    UA_QualifiedName branchIdQN = UA_QUALIFIEDNAME(0, "BranchId");
    UA_SimpleAttributeOperand_init(&select[1]);
    select[1].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    select[1].attributeId = UA_ATTRIBUTEID_VALUE;
    select[1].browsePathSize = 1;
    select[1].browsePath = &branchIdQN;

    UA_EventFilter filter;
    UA_EventFilter_init(&filter);
    filter.selectClausesSize = 2;
    filter.selectClauses = select;

    req.requestedParameters.filter.content.decoded.type = &UA_TYPES[UA_TYPES_EVENTFILTER];
    req.requestedParameters.filter.content.decoded.data = &filter;
    req.requestedParameters.filter.encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;

    UA_MonitoredItemCreateResult res = UA_Server_createEventNotificationMonitoredItem(acserver,
            UA_TIMESTAMPSTORETURN_NEITHER,
            req,
            NULL,
            eventCB
    );
    assert (res.statusCode == UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(res.statusCode, UA_STATUSCODE_GOOD);


    retval = UA_Server_Condition_setOnAckedCallback(acserver, conditionInstance, onAcked);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    /* Initial State of Condition */
    ck_assert(isConditionEnabled(acserver, conditionInstance) == true);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);

    /* 1. Alarm goes Active */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionAcked(acserver, conditionInstance) == false);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 2. Alarm acked */
    retval = UA_Server_Condition_acknowledge(acserver, conditionInstance, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 3. Alarm goes Inactive */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 4. Alarm Confirmed */
    retval = UA_Server_Condition_confirm (acserver, conditionInstance, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);

    /* 5. Alarm goes active */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionAcked(acserver, conditionInstance) == false);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 6. Alarm goes inactive */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    UA_Server_run_startup(acserver);
    while (1) UA_Server_run_iterate(acserver,true);

    /* 8. Alarm goes active again */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionAcked(acserver, conditionInstance) == false);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

//    /* 10. Alarm goes inactive again */
//    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
//    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
//    ck_assert(isConditionActive(acserver, conditionInstance) == true);
//    ck_assert(isConditionAcked(acserver, conditionInstance) == false);
//    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
//    ck_assert(conditionRetain(acserver, conditionInstance) == true);
//
//    /* 14. No longer of interest */
//    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
//    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
//    ck_assert(isConditionActive(acserver, conditionInstance) == false);
//    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
//    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
//    ck_assert(conditionRetain(acserver, conditionInstance) == false);

    retval = UA_Server_deleteCondition(
            acserver,
            conditionInstance
    );

    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
} END_TEST

#endif

int main(void) {
    Suite *s = suite_create("server_alarmcondition");

    TCase *tc_call = tcase_create("Alarms and Conditions");
#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
    tcase_add_test(tc_call, createDelete);
    tcase_add_test(tc_call, eventSequence);
#endif
    tcase_add_checked_fixture(tc_call, setup, teardown);

    suite_add_tcase(s, tc_call);

    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
