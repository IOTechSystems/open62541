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

#include "test_helpers.h"

UA_Server *acserver;
static uint32_t eventCount = 0;

static void startServer(UA_Boolean supportsFilteredRetain) {
    eventCount = 0;
    acserver = UA_Server_newForUnitTest();
    ck_assert(acserver != NULL);
    UA_Server_getConfig(acserver)->supportsFilteredRetain = supportsFilteredRetain;
    ck_assert_uint_eq(UA_Server_run_startup(acserver), UA_STATUSCODE_GOOD);
}

static void setup(void) {
    startServer(false);
}

static void setupSupportsFilteredRetain(void) {
    startServer(true);
}

static void teardown(void) {
    UA_Server_run_shutdown(acserver);
    UA_Server_delete(acserver);
}

/* Local event notifications are delivered asynchronously from the EventLoop.
 * Iterate once so that the callbacks for all pending events have run. */
static void deliverEvents(void) {
    UA_Server_run_iterate(acserver, false);
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
isConditionSuppressed (UA_Server *server, UA_NodeId condition)
{
    return isConditionTwoStateVariableInTrueState(
        server,
        condition,
        UA_QUALIFIEDNAME(0, "SuppressedState")
    );
}

static inline UA_Boolean
isConditionOutOfService (UA_Server *server, UA_NodeId condition)
{
    return isConditionTwoStateVariableInTrueState(
        server,
        condition,
        UA_QUALIFIEDNAME(0, "OutOfServiceState")
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

static inline UA_Boolean
conditionSuppressedOrShelved (UA_Server *server, UA_NodeId condition)
{
    UA_Variant val;
    UA_StatusCode ret = UA_Server_readObjectProperty(
        server,
        condition,
        UA_QUALIFIEDNAME(0, "SuppressedOrShelved"),
        &val
    );
    assert (ret == UA_STATUSCODE_GOOD && val.type == &UA_TYPES[UA_TYPES_BOOLEAN]);
    UA_Boolean suppressedOrShelved = *(UA_Boolean *)val.data;
    UA_Variant_clear(&val);
    return suppressedOrShelved;
}

START_TEST(createMultiple) {
    UA_StatusCode retval;

    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Condition create multiple")
    };

    for(UA_UInt16 i = 0; i < 10; ++i)
    {
        UA_NodeId conditionInstance = UA_NODEID_NULL;
        retval = __UA_Server_createCondition(
            acserver,
            UA_NODEID_NULL,
            UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE),
            &conditionProperties,
            NULL,
            NULL,
            NULL,
            &conditionInstance
        );
        UA_Server_Condition_enable(acserver, conditionInstance, true, NULL);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
        ck_assert_msg(!UA_NodeId_isNull(&conditionInstance), "ConditionId is null");
    }
} END_TEST

START_TEST(createDelete) {
    UA_StatusCode retval;

    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Condition createDelete")
    };

    // Loop to increase the chance of capturing dead pointers
    for(UA_UInt16 i = 0; i < 3; ++i)
    {
        UA_NodeId conditionInstance = UA_NODEID_NULL;
        retval = __UA_Server_createCondition(
            acserver,
            UA_NODEID_NULL,
            UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE),
            &conditionProperties,
            NULL,
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

typedef struct
{
    UA_Boolean acked;
    UA_Boolean retain;
    UA_Boolean confirmed;
    UA_Boolean active;
}ConditionState;

static UA_StatusCode onAcked(UA_Server *server, const UA_NodeId *id, void *ctx)
{
    UA_Boolean *autoConfirm = (UA_Boolean *)ctx;
    if(!*autoConfirm)
    {
        UA_NodeId confirmedId;
        UA_Server_getNodeIdWithBrowseName(server, id, UA_QUALIFIEDNAME(0, "ConfirmedState"), &confirmedId);
        UA_Server_writeTwoStateVariable(server, confirmedId, UA_LOCALIZEDTEXT("en", "Unconfirmed"), false);
        UA_NodeId_clear (&confirmedId);

    }
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode onConfirmed(UA_Server *server, const UA_NodeId *id, void *ctx)
{
    UA_Boolean *autoConfirm = (UA_Boolean *)ctx;
    *autoConfirm = true;
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode onActive(UA_Server *server, const UA_NodeId *id, void *ctx)
{
    UA_Boolean *autoConfirm = (UA_Boolean *)ctx;
    *autoConfirm = false;
    UA_NodeId ackedId;
    UA_Server_getNodeIdWithBrowseName(server, id, UA_QUALIFIEDNAME(0, "AckedState"), &ackedId);
    UA_Server_writeTwoStateVariable(server, ackedId, UA_LOCALIZEDTEXT("en", "Unacknowledged"), false);
    UA_NodeId_clear (&ackedId);
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode onInactive(UA_Server *server, const UA_NodeId *id, void *ctx)
{
    return UA_STATUSCODE_GOOD;
}

const UA_ConditionCallbacks callbacks = {
    .onAcked = onAcked,
    .onConfirmed = onConfirmed,
    .onActive = onActive,
    .onInactive = onInactive
};

static void conditionSequence1CB (UA_Server *server, UA_UInt32 monId, void *monContext,
                                  const UA_KeyValueMap eventFields)
{
    eventCount++;
}


/* Based on https://reference.opcfoundation.org/Core/Part9/v105/docs/B.1.2 */
START_TEST(conditionSequence1) {

    UA_StatusCode retval;
    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test Condition"),
        .canBranch = false
    };

    UA_AlarmConditionProperties alarmProperties;
    memset (&alarmProperties, 0, sizeof(alarmProperties));
    alarmProperties.acknowledgeableConditionProperties.confirmable = true;

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    retval = __UA_Server_createCondition(
        acserver,
        UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE),
        &conditionProperties,
        (UA_ConditionTypeSetupFn) UA_Server_setupAlarmConditionNodes,
        (UA_ConditionTypeSetupFn) UA_Server_initAlarmConditionNodes,
        &alarmProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    retval = UA_Server_Condition_setCallbacks(acserver, conditionInstance, &callbacks);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    /* Create monitored event */
    UA_MonitoredItemCreateRequest req;
    UA_MonitoredItemCreateRequest_init(&req);
    req.itemToMonitor.nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    req.monitoringMode = UA_MONITORINGMODE_REPORTING;
    req.itemToMonitor.attributeId = UA_ATTRIBUTEID_EVENTNOTIFIER;
    req.requestedParameters.samplingInterval = 250;
    req.requestedParameters.discardOldest = true;
    req.requestedParameters.queueSize = 10;

    UA_SimpleAttributeOperand select[1];

    size_t i =0;
    UA_SimpleAttributeOperand_init(&select[i]);
    select[i].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    select[i].attributeId = UA_ATTRIBUTEID_NODEID;
    i++;

    UA_EventFilter filter;
    UA_EventFilter_init(&filter);
    filter.selectClausesSize = i;
    filter.selectClauses = select;

    req.requestedParameters.filter.content.decoded.type = &UA_TYPES[UA_TYPES_EVENTFILTER];
    req.requestedParameters.filter.content.decoded.data = &filter;
    req.requestedParameters.filter.encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;

    UA_MonitoredItemCreateResult res = UA_Server_createEventMonitoredItemEx(
        acserver,
        req,
        NULL,
        conditionSequence1CB
    );
    ck_assert_uint_eq(res.statusCode, UA_STATUSCODE_GOOD);

    UA_Boolean autoConfirm = false;
    retval = UA_Server_Condition_setContext(acserver, conditionInstance, &autoConfirm);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    uint32_t expectedEventCount = 0;
    ck_assert_uint_eq (expectedEventCount, eventCount);

    retval = UA_Server_Condition_enable (acserver, conditionInstance, UA_TRUE, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    /* Initial State of Condition */
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);

    /* 1. Alarm goes Active */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionAcked(acserver, conditionInstance) == false);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 2. Condition Acknowledged Confirm required */
    retval = UA_Server_Condition_acknowledge(acserver, conditionInstance, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 3. Alarm goes inactive */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 4. Condition confirmed */
    retval = UA_Server_Condition_confirm (acserver, conditionInstance, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);

    /* 5. Alarm goes active */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionAcked(acserver, conditionInstance) == false);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 6. Alarm goes inactive */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == false);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 7. Condition Acknowledged Confirm required */
    retval = UA_Server_Condition_acknowledge(acserver, conditionInstance, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 8. Condition confirmed */
    retval = UA_Server_Condition_confirm (acserver, conditionInstance, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);

    retval = UA_Server_deleteCondition(
        acserver,
        conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
} END_TEST

typedef struct {
    UA_NodeId branch1;
    UA_NodeId branch2;

    ConditionState mainBranchState;
    ConditionState branch1State;
    ConditionState branch2State;
}EventCBCtx;

static void conditionSequence2CB (UA_Server *server, UA_UInt32 monId, void *monContext,
                                  const UA_KeyValueMap eventFields)
{
    eventCount++;
    EventCBCtx *ctx= (EventCBCtx *) monContext;

    UA_NodeId conditionId = *(UA_NodeId *) eventFields.map[0].value.data;
    UA_NodeId branchId = *(UA_NodeId *) eventFields.map[1].value.data;
    UA_Boolean retain = *(UA_Boolean*) eventFields.map[2].value.data;
    UA_Boolean acked = *(UA_Boolean*) eventFields.map[3].value.data;
    UA_Boolean confirmed = *(UA_Boolean*) eventFields.map[4].value.data;
    UA_Boolean active = *(UA_Boolean*) eventFields.map[5].value.data;

    /*Update branchIds*/
    if (!UA_NodeId_equal(&branchId, &UA_NODEID_NULL))
    {
        if (UA_NodeId_equal(&ctx->branch1, &UA_NODEID_NULL)) UA_NodeId_copy(&branchId, &ctx->branch1);
        else if (UA_NodeId_equal(&ctx->branch2, &UA_NODEID_NULL) && !UA_NodeId_equal(&branchId, &ctx->branch1))
            UA_NodeId_copy(&branchId, &ctx->branch2);
    }

    ConditionState state;
    state.retain = retain;
    state.acked = acked;
    state.confirmed = confirmed;
    state.active = active;

    if (UA_NodeId_equal (&branchId, &UA_NODEID_NULL))
    {
        ctx->mainBranchState = state;
    }
    else if (UA_NodeId_equal(&branchId,&ctx->branch1))
    {
        ctx->branch1State = state;
    }
    else if (UA_NodeId_equal(&branchId,&ctx->branch2))
    {
        ctx->branch2State = state;
    }
}

static UA_StatusCode
getNonMainBranch (UA_Server *server, const UA_NodeId *conditionBranchId,
                  void *conditionContext, void *iterCtx)
{
    UA_NodeId *conditionId = ((UA_NodeId **) iterCtx)[0];
    UA_NodeId *branchOut = ((UA_NodeId **) iterCtx)[1];
    if (!UA_NodeId_equal(conditionBranchId, conditionId))
        UA_NodeId_copy(conditionBranchId, branchOut);
    return UA_STATUSCODE_GOOD;
}

/* Re-enabling a Condition re-evaluates Retain of all ConditionBranches, so
 * an active Condition and its unacknowledged branch are retained again */
START_TEST(enableRestoresBranchRetain) {
    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test Condition"),
        .canBranch = true
    };

    UA_AlarmConditionProperties alarmProperties;
    memset (&alarmProperties, 0, sizeof(alarmProperties));

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    UA_StatusCode retval = __UA_Server_createCondition(
        acserver,
        UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE),
        &conditionProperties,
        (UA_ConditionTypeSetupFn)UA_Server_setupAlarmConditionNodes,
        (UA_ConditionTypeSetupFn)UA_Server_initAlarmConditionNodes,
        &alarmProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    retval = UA_Server_Condition_enable(acserver, conditionInstance, true, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    /* onActive marks the Condition unacknowledged */
    retval = UA_Server_Condition_setCallbacks(acserver, conditionInstance, &callbacks);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    UA_Boolean autoConfirm = false;
    retval = UA_Server_Condition_setContext(acserver, conditionInstance, &autoConfirm);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    /* Going inactive while unacknowledged creates a branch. The Condition is
     * then active again. */
    ck_assert_uint_eq(UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true), UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false), UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true), UA_STATUSCODE_GOOD);

    UA_NodeId branch = UA_NODEID_NULL;
    UA_NodeId *iterCtx[2] = {&conditionInstance, &branch};
    UA_Server_Condition_iterBranches(acserver, conditionInstance, getNonMainBranch, iterCtx);
    ck_assert(!UA_NodeId_isNull(&branch));
    ck_assert(isConditionAcked(acserver, branch) == false);
    ck_assert(conditionRetain(acserver, branch) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    retval = UA_Server_Condition_enable(acserver, conditionInstance, false, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert(conditionRetain(acserver, branch) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);

    retval = UA_Server_Condition_enable(acserver, conditionInstance, true, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert(conditionRetain(acserver, branch) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    UA_NodeId_clear(&branch);
} END_TEST

/* Based on https://reference.opcfoundation.org/Core/Part9/v105/docs/B.1.3 */
START_TEST(conditionSequence2) {

    EventCBCtx ctx;
    memset(&ctx, 0, sizeof(ctx));

    UA_StatusCode retval;
    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test Condition"),
        .canBranch = true
    };

    UA_AlarmConditionProperties alarmProperties;
    memset (&alarmProperties, 0, sizeof(alarmProperties));
    alarmProperties.acknowledgeableConditionProperties.confirmable = true;

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    retval = __UA_Server_createCondition(
        acserver,
        UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE),
        &conditionProperties,
        (UA_ConditionTypeSetupFn)UA_Server_setupAlarmConditionNodes,
        (UA_ConditionTypeSetupFn)UA_Server_initAlarmConditionNodes,
        &alarmProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    retval = UA_Server_Condition_enable(acserver, conditionInstance, true, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    retval = UA_Server_Condition_setCallbacks(acserver, conditionInstance, &callbacks);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_Boolean autoConfirm = false;
    retval = UA_Server_Condition_setContext(acserver, conditionInstance, &autoConfirm);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    /* Create monitored event */
    UA_MonitoredItemCreateRequest req;
    UA_MonitoredItemCreateRequest_init(&req);
    req.itemToMonitor.nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    req.monitoringMode = UA_MONITORINGMODE_REPORTING;
    req.itemToMonitor.attributeId = UA_ATTRIBUTEID_EVENTNOTIFIER;
    req.requestedParameters.samplingInterval = 250;
    req.requestedParameters.discardOldest = true;
    req.requestedParameters.queueSize = 10;

    UA_SimpleAttributeOperand select[6];

    size_t i =0;
    UA_SimpleAttributeOperand_init(&select[i]);
    select[i].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    select[i].attributeId = UA_ATTRIBUTEID_NODEID;

    UA_QualifiedName branchIdQN = UA_QUALIFIEDNAME(0, "BranchId");
    i++;
    UA_SimpleAttributeOperand_init(&select[i]);
    select[i].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    select[i].attributeId = UA_ATTRIBUTEID_VALUE;
    select[i].browsePathSize = 1;
    select[i].browsePath = &branchIdQN;

    UA_QualifiedName retainQN = UA_QUALIFIEDNAME(0, "Retain");
    i++;
    UA_SimpleAttributeOperand_init(&select[i]);
    select[i].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    select[i].attributeId = UA_ATTRIBUTEID_VALUE;
    select[i].browsePathSize = 1;
    select[i].browsePath = &retainQN;

    UA_QualifiedName ackedId[2] = {UA_QUALIFIEDNAME(0, "AckedState"), UA_QUALIFIEDNAME(0, "Id") };
    i++;
    UA_SimpleAttributeOperand_init(&select[i]);
    select[i].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    select[i].attributeId = UA_ATTRIBUTEID_VALUE;
    select[i].browsePathSize = 2;
    select[i].browsePath = ackedId;

    UA_QualifiedName confirmedId[2] = {UA_QUALIFIEDNAME(0, "ConfirmedState"), UA_QUALIFIEDNAME(0, "Id") };
    i++;
    UA_SimpleAttributeOperand_init(&select[i]);
    select[i].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    select[i].attributeId = UA_ATTRIBUTEID_VALUE;
    select[i].browsePathSize = 2;
    select[i].browsePath = confirmedId;

    UA_QualifiedName activeId[2] = {UA_QUALIFIEDNAME(0, "ActiveState"), UA_QUALIFIEDNAME(0, "Id") };
    i++;
    UA_SimpleAttributeOperand_init(&select[i]);
    select[i].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    select[i].attributeId = UA_ATTRIBUTEID_VALUE;
    select[i].browsePathSize = 2;
    select[i].browsePath = activeId;


    UA_EventFilter filter;
    UA_EventFilter_init(&filter);
    filter.selectClausesSize = 6;
    filter.selectClauses = select;

    req.requestedParameters.filter.content.decoded.type = &UA_TYPES[UA_TYPES_EVENTFILTER];
    req.requestedParameters.filter.content.decoded.data = &filter;
    req.requestedParameters.filter.encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;

    UA_MonitoredItemCreateResult res = UA_Server_createEventMonitoredItemEx(
        acserver,
        req,
        &ctx,
        conditionSequence2CB
    );
    ck_assert_uint_eq(res.statusCode, UA_STATUSCODE_GOOD);

    uint32_t expectedEventCount = 0;
    ck_assert_uint_eq (expectedEventCount, eventCount);

    /* Initial State of Condition */
    ck_assert(isConditionEnabled(acserver, conditionInstance) == true);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);

    /* 1. Alarm goes Active */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.mainBranchState.active == true);
    ck_assert(ctx.mainBranchState.acked == false);
    ck_assert(ctx.mainBranchState.confirmed == true);
    ck_assert(ctx.mainBranchState.retain == true);

    /* 2. Alarm acked */
    retval = UA_Server_Condition_acknowledge(acserver, conditionInstance, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.mainBranchState.active == true);
    ck_assert(ctx.mainBranchState.acked == true);
    ck_assert(ctx.mainBranchState.confirmed == false);
    ck_assert(ctx.mainBranchState.retain == true);

    /* 3. Alarm goes Inactive */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.mainBranchState.active == false);
    ck_assert(ctx.mainBranchState.acked == true);
    ck_assert(ctx.mainBranchState.confirmed == false);
    ck_assert(ctx.mainBranchState.retain == true);

    /* 4. Alarm Confirmed */
    retval = UA_Server_Condition_confirm (acserver, conditionInstance, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.mainBranchState.active == false);
    ck_assert(ctx.mainBranchState.acked == true);
    ck_assert(ctx.mainBranchState.confirmed == true);
    ck_assert(ctx.mainBranchState.retain == false);

    /* 5. Alarm goes active */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.mainBranchState.active == true);
    ck_assert(ctx.mainBranchState.acked == false);
    ck_assert(ctx.mainBranchState.confirmed == true);
    ck_assert(ctx.mainBranchState.retain == true);

    /* 6. Alarm goes inactive */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++; //event for alarm going active
    expectedEventCount++; //event for branch created
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.mainBranchState.active == false);
    ck_assert(ctx.mainBranchState.acked == true);
    ck_assert(ctx.mainBranchState.confirmed == true);
    ck_assert(ctx.mainBranchState.retain == true);

    /* 7. Branch #1 created */
    ck_assert(!UA_NodeId_equal(&ctx.branch1, &UA_NODEID_NULL));
    ck_assert(ctx.branch1State.active == true);
    ck_assert(ctx.branch1State.acked == false);
    ck_assert(ctx.branch1State.confirmed == true);
    ck_assert(ctx.branch1State.retain == true);

    /* 8. Alarm goes active again */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.mainBranchState.active == true);
    ck_assert(ctx.mainBranchState.acked == false);
    ck_assert(ctx.mainBranchState.confirmed == true);
    ck_assert(ctx.mainBranchState.retain == true);

    /* 9. Branch acked */
    retval = UA_Server_Condition_acknowledge(acserver, ctx.branch1, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.branch1State.active == true);
    ck_assert(ctx.branch1State.acked == true);
    ck_assert(ctx.branch1State.confirmed == false);
    ck_assert(ctx.branch1State.retain == true);

    /* 10. Alarm goes inactive again */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    expectedEventCount++; /*New branch notification */
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.mainBranchState.active == false);
    ck_assert(ctx.mainBranchState.acked == true);
    ck_assert(ctx.mainBranchState.confirmed == true);
    ck_assert(ctx.mainBranchState.retain == true);

    /* 11. Branch #2 created */
    ck_assert(!UA_NodeId_equal(&ctx.branch2, &UA_NODEID_NULL));
    ck_assert(ctx.branch2State.active == true);
    ck_assert(ctx.branch2State.acked == false);
    ck_assert(ctx.branch2State.confirmed == true);
    ck_assert(ctx.branch2State.retain == true);

    /* 12. Branch #1 confirmed  */
    retval = UA_Server_Condition_confirm(acserver, ctx.branch1, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.branch1State.active == true);
    ck_assert(ctx.branch1State.acked == true);
    ck_assert(ctx.branch1State.confirmed == true);
    ck_assert(ctx.branch1State.retain == false);

    /* 13. Branch #2 Acked */
    retval = UA_Server_Condition_acknowledge(acserver, ctx.branch2, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    expectedEventCount++; //notification for main branch retain going to false
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.branch2State.active == true);
    ck_assert(ctx.branch2State.acked == true);
    ck_assert(ctx.branch2State.confirmed == true);
    ck_assert(ctx.branch2State.retain == false);

    /* 14. No longer of interest */
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(ctx.mainBranchState.active == false);
    ck_assert(ctx.mainBranchState.acked == true);
    ck_assert(ctx.mainBranchState.confirmed == true);
    ck_assert(ctx.mainBranchState.retain == false);

    retval = UA_Server_deleteCondition(
            acserver,
            conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
} END_TEST

static void enableDisableCB (UA_Server *server, UA_UInt32 monId, void *monContext,
                                  const UA_KeyValueMap eventFields)
{
    eventCount++;
}

START_TEST(enableDisable) {
    UA_StatusCode status;
    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test Condition"),
        .canBranch = false
    };

    UA_AlarmConditionProperties alarmProperties;
    memset (&alarmProperties, 0, sizeof(alarmProperties));
    alarmProperties.acknowledgeableConditionProperties.confirmable = true;

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    status = __UA_Server_createCondition(
        acserver,
        UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE),
        &conditionProperties,
        (UA_ConditionTypeSetupFn) UA_Server_setupAlarmConditionNodes,
        (UA_ConditionTypeSetupFn) UA_Server_initAlarmConditionNodes,
        &alarmProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);

    /* Create monitored event */
    UA_MonitoredItemCreateRequest req;
    UA_MonitoredItemCreateRequest_init(&req);
    req.itemToMonitor.nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    req.monitoringMode = UA_MONITORINGMODE_REPORTING;
    req.itemToMonitor.attributeId = UA_ATTRIBUTEID_EVENTNOTIFIER;
    req.requestedParameters.samplingInterval = 250;
    req.requestedParameters.discardOldest = true;
    req.requestedParameters.queueSize = 10;

    UA_SimpleAttributeOperand select[1];

    size_t i =0;
    UA_SimpleAttributeOperand_init(&select[i]);
    select[i].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    select[i].attributeId = UA_ATTRIBUTEID_NODEID;
    i++;

    UA_EventFilter filter;
    UA_EventFilter_init(&filter);
    filter.selectClausesSize = i;
    filter.selectClauses = select;

    req.requestedParameters.filter.content.decoded.type = &UA_TYPES[UA_TYPES_EVENTFILTER];
    req.requestedParameters.filter.content.decoded.data = &filter;
    req.requestedParameters.filter.encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;

    UA_MonitoredItemCreateResult res = UA_Server_createEventMonitoredItemEx(
        acserver,
        req,
        NULL,
        enableDisableCB
    );
    ck_assert_uint_eq(res.statusCode, UA_STATUSCODE_GOOD);

    uint32_t expectedEventCount = 0;

    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert (isConditionEnabled(acserver, conditionInstance) == UA_FALSE);

    status = UA_Server_Condition_enable (acserver, conditionInstance, UA_TRUE, NULL);
    deliverEvents();
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    ck_assert (isConditionEnabled(acserver, conditionInstance) == UA_TRUE);
    /* Retain is false so no event*/
    ck_assert_uint_eq (expectedEventCount, eventCount);

    status = UA_Server_Condition_enable (acserver, conditionInstance, UA_TRUE, NULL);
    deliverEvents();
    ck_assert_uint_eq(status, UA_STATUSCODE_BADCONDITIONALREADYENABLED);
    ck_assert_uint_eq (expectedEventCount, eventCount);

    status = UA_Server_Condition_enable (acserver, conditionInstance, UA_FALSE, NULL);
    deliverEvents();
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    ck_assert (isConditionEnabled(acserver, conditionInstance) == UA_FALSE);
    /* Retain is false so no event*/
    ck_assert_uint_eq (expectedEventCount, eventCount);

    status = UA_Server_Condition_enable (acserver, conditionInstance, UA_FALSE, NULL);
    deliverEvents();
    ck_assert_uint_eq(status, UA_STATUSCODE_BADCONDITIONALREADYDISABLED);
    ck_assert_uint_eq (expectedEventCount, eventCount);

    status = UA_Server_Condition_updateActive (acserver, conditionInstance, NULL, UA_TRUE);
    deliverEvents();
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);

    status = UA_Server_Condition_enable (acserver, conditionInstance, UA_TRUE, NULL);
    deliverEvents();
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    ck_assert (isConditionEnabled(acserver, conditionInstance) == UA_TRUE);
    /* Condition active so we now expect an event*/
    ck_assert_uint_eq (++expectedEventCount, eventCount);

}

static void disableRetainCB (UA_Server *server, UA_UInt32 monId, void *monContext,
                             const UA_KeyValueMap eventFields)
{
    eventCount++;
    *(UA_Boolean *)monContext = *(UA_Boolean*) eventFields.map[0].value.data;
}

/* Disabling a retained Condition notifies Clients with Retain set to False.
 * No further events are generated while the Condition is disabled.
 * https://reference.opcfoundation.org/Core/Part9/v105/docs/5.5 */
START_TEST(disableNotifiesRetainFalse) {
    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test Condition"),
        .canBranch = false
    };

    UA_AlarmConditionProperties alarmProperties;
    memset (&alarmProperties, 0, sizeof(alarmProperties));

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    UA_StatusCode status = __UA_Server_createCondition(
        acserver,
        UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE),
        &conditionProperties,
        (UA_ConditionTypeSetupFn) UA_Server_setupAlarmConditionNodes,
        (UA_ConditionTypeSetupFn) UA_Server_initAlarmConditionNodes,
        &alarmProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    status = UA_Server_Condition_enable (acserver, conditionInstance, UA_TRUE, NULL);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);

    UA_MonitoredItemCreateRequest req;
    UA_MonitoredItemCreateRequest_init(&req);
    req.itemToMonitor.nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    req.monitoringMode = UA_MONITORINGMODE_REPORTING;
    req.itemToMonitor.attributeId = UA_ATTRIBUTEID_EVENTNOTIFIER;
    req.requestedParameters.samplingInterval = 250;
    req.requestedParameters.discardOldest = true;
    req.requestedParameters.queueSize = 10;

    UA_SimpleAttributeOperand select[1];
    UA_SimpleAttributeOperand_init(&select[0]);
    select[0].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    select[0].browsePathSize = 1;
    select[0].browsePath = (UA_QualifiedName[]){UA_QUALIFIEDNAME(0, "Retain")};
    select[0].attributeId = UA_ATTRIBUTEID_VALUE;

    UA_EventFilter filter;
    UA_EventFilter_init(&filter);
    filter.selectClausesSize = 1;
    filter.selectClauses = select;

    req.requestedParameters.filter.content.decoded.type = &UA_TYPES[UA_TYPES_EVENTFILTER];
    req.requestedParameters.filter.content.decoded.data = &filter;
    req.requestedParameters.filter.encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;

    UA_Boolean lastRetain = false;
    UA_MonitoredItemCreateResult res = UA_Server_createEventMonitoredItemEx(
        acserver, req, &lastRetain, disableRetainCB);
    ck_assert_uint_eq(res.statusCode, UA_STATUSCODE_GOOD);

    status = UA_Server_Condition_updateActive (acserver, conditionInstance, NULL, UA_TRUE);
    deliverEvents();
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(eventCount, 1);
    ck_assert(lastRetain == true);

    status = UA_Server_Condition_enable (acserver, conditionInstance, UA_FALSE, NULL);
    deliverEvents();
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(eventCount, 2);
    ck_assert(lastRetain == false);

    status = UA_Server_Condition_updateActive (acserver, conditionInstance, NULL, UA_FALSE);
    deliverEvents();
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    status = UA_Server_Condition_updateActive (acserver, conditionInstance, NULL, UA_TRUE);
    deliverEvents();
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(eventCount, 2);
} END_TEST

static void conditionSequence3CB (UA_Server *server, UA_UInt32 monId, void *monContext,
                                  const UA_KeyValueMap eventFields)
{
    eventCount++;
    UA_NodeId conditionId = *(UA_NodeId *) eventFields.map[0].value.data;
    UA_Boolean retain = *(UA_Boolean*) eventFields.map[1].value.data;
    *(UA_Boolean *)monContext = retain;
}

static void
setupTwoOperandsFilter(UA_ContentFilterElement *element, UA_UInt32 firstOperandIndex, UA_UInt32 secondOperandIndex){
    element->filterOperands[0].content.decoded.type = &UA_TYPES[UA_TYPES_ELEMENTOPERAND];
    element->filterOperands[0].encoding = UA_EXTENSIONOBJECT_DECODED;
    element->filterOperands[1].content.decoded.type = &UA_TYPES[UA_TYPES_ELEMENTOPERAND];
    element->filterOperands[1].encoding = UA_EXTENSIONOBJECT_DECODED;
    UA_ElementOperand *firstElementOperand = UA_ElementOperand_new();
    UA_ElementOperand_init(firstElementOperand);
    firstElementOperand->index = firstOperandIndex;
    UA_ElementOperand *secondElementOperand = UA_ElementOperand_new();
    UA_ElementOperand_init(secondElementOperand);
    secondElementOperand->index = secondOperandIndex;
    element->filterOperands[0].content.decoded.data = firstElementOperand;
    element->filterOperands[1].content.decoded.data = secondElementOperand;
}

/* Based on https://reference.opcfoundation.org/Core/Part9/v105/docs/B.1.4
 * For this test supportsFilteredRetain is true.
 * */

START_TEST(conditionSequence3) {

    UA_StatusCode retval;
    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test Condition"),
        .canBranch = false
    };

    UA_AlarmConditionProperties alarmProperties;
    memset(&alarmProperties, 0, sizeof(alarmProperties));
    alarmProperties.isSuppressible = true;
    alarmProperties.isServiceable = true;

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    retval = __UA_Server_createCondition(
        acserver,
        UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE),
        &conditionProperties,
        (UA_ConditionTypeSetupFn)UA_Server_setupAlarmConditionNodes,
        (UA_ConditionTypeSetupFn)UA_Server_initAlarmConditionNodes,
        &alarmProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    retval = UA_Server_Condition_enable(acserver, conditionInstance, true, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    /* Create monitored event */
    UA_MonitoredItemCreateRequest req;
    UA_MonitoredItemCreateRequest_init(&req);
    req.itemToMonitor.nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    req.monitoringMode = UA_MONITORINGMODE_REPORTING;
    req.itemToMonitor.attributeId = UA_ATTRIBUTEID_EVENTNOTIFIER;
    req.requestedParameters.samplingInterval = 250;
    req.requestedParameters.discardOldest = true;
    req.requestedParameters.queueSize = 10;

    UA_SimpleAttributeOperand select[2];

    size_t i = 0;
    UA_SimpleAttributeOperand_init(&select[i]);
    select[i].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    select[i].attributeId = UA_ATTRIBUTEID_NODEID;
    i++;

    UA_QualifiedName retainQN = UA_QUALIFIEDNAME(0, "Retain");
    UA_SimpleAttributeOperand_init(&select[i]);
    select[i].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    select[i].attributeId = UA_ATTRIBUTEID_VALUE;
    select[i].browsePathSize = 1;
    select[i].browsePath = &retainQN;
    i++;

    //(only report when Suppressed state == false and OutOfServiceState == false)
    UA_ContentFilterElement whereClauseElements[3];

    UA_Boolean val = false;
    UA_LiteralOperand literalOperand;
    UA_Variant_setScalar(&literalOperand.value, &val, &UA_TYPES[UA_TYPES_BOOLEAN]);
    UA_ExtensionObject literalOperandEO;
    literalOperandEO.content.decoded.data = &literalOperand;
    literalOperandEO.content.decoded.type = &UA_TYPES[UA_TYPES_LITERALOPERAND];
    literalOperandEO.encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;

    //or
    UA_ExtensionObject filterOperands0[2];
    UA_ExtensionObject_init(&filterOperands0[0]);
    filterOperands0[0].encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;
    filterOperands0[0].content.decoded.type = &UA_TYPES[UA_TYPES_ELEMENTOPERAND];
    UA_ElementOperand idx1ElementOperand;
    idx1ElementOperand.index = 1;
    filterOperands0[0].content.decoded.data = &idx1ElementOperand;

    UA_ExtensionObject_init(&filterOperands0[1]);
    filterOperands0[1].encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;
    filterOperands0[1].content.decoded.type = &UA_TYPES[UA_TYPES_ELEMENTOPERAND];
    UA_ElementOperand idx2ElementOperand;
    idx2ElementOperand.index = 2;
    filterOperands0[1].content.decoded.data = &idx2ElementOperand;

    whereClauseElements[0].filterOperator = UA_FILTEROPERATOR_AND;
    whereClauseElements[0].filterOperandsSize = 2;
    whereClauseElements[0].filterOperands = filterOperands0;

    //SuppressedState == False
    UA_ExtensionObject filterOperands1[2];
    UA_ExtensionObject_init(&filterOperands1[0]);
    filterOperands1[0].encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;
    filterOperands1[0].content.decoded.type = &UA_TYPES[UA_TYPES_SIMPLEATTRIBUTEOPERAND];
    UA_SimpleAttributeOperand suppressedStateOperand;
    UA_SimpleAttributeOperand_init(&suppressedStateOperand);
    suppressedStateOperand.attributeId = UA_ATTRIBUTEID_VALUE;
    suppressedStateOperand.typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE);
    UA_QualifiedName suppressedStateIdPath[2];
    suppressedStateIdPath[0] = UA_QUALIFIEDNAME(0, "SuppressedState");
    suppressedStateIdPath[1] = UA_QUALIFIEDNAME(0, "Id");
    suppressedStateOperand.browsePath = suppressedStateIdPath;
    suppressedStateOperand.browsePathSize = 2;
    filterOperands1[0].content.decoded.data = &suppressedStateOperand;
    filterOperands1[1] = literalOperandEO;
    whereClauseElements[1].filterOperator = UA_FILTEROPERATOR_EQUALS;
    whereClauseElements[1].filterOperandsSize = 2;
    whereClauseElements[1].filterOperands = filterOperands1;

    //OutOfServiceState == False
    UA_ExtensionObject filterOperands2[2];
    UA_ExtensionObject_init(&filterOperands2[0]);
    filterOperands2[0].encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;
    filterOperands2[0].content.decoded.type = &UA_TYPES[UA_TYPES_SIMPLEATTRIBUTEOPERAND];
    UA_SimpleAttributeOperand outOfServiceStateOperand;
    UA_SimpleAttributeOperand_init(&outOfServiceStateOperand);
    outOfServiceStateOperand.attributeId = UA_ATTRIBUTEID_VALUE;
    outOfServiceStateOperand.typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE);
    UA_QualifiedName outOfServicePath[2];
    outOfServicePath[0] = UA_QUALIFIEDNAME(0, "OutOfServiceState");
    outOfServicePath[1] = UA_QUALIFIEDNAME(0, "Id");
    outOfServiceStateOperand.browsePath = outOfServicePath;
    outOfServiceStateOperand.browsePathSize = 2;
    filterOperands2[0].content.decoded.data = &outOfServiceStateOperand;

    filterOperands2[1] = literalOperandEO;
    whereClauseElements[2].filterOperator = UA_FILTEROPERATOR_EQUALS;
    whereClauseElements[2].filterOperandsSize = 2;
    whereClauseElements[2].filterOperands = filterOperands2;

    UA_ContentFilter whereClause;
    whereClause.elements = whereClauseElements;
    whereClause.elementsSize = 3;

    UA_EventFilter filter;
    UA_EventFilter_init(&filter);
    filter.selectClausesSize = i;
    filter.selectClauses = select;
    filter.whereClause = whereClause;

    req.requestedParameters.filter.content.decoded.type = &UA_TYPES[UA_TYPES_EVENTFILTER];
    req.requestedParameters.filter.content.decoded.data = &filter;
    req.requestedParameters.filter.encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;

    UA_Boolean retainSent = false;
    UA_MonitoredItemCreateResult res = UA_Server_createEventMonitoredItemEx(
        acserver,
        req,
        &retainSent,
        conditionSequence3CB
    );
    ck_assert_uint_eq(res.statusCode, UA_STATUSCODE_GOOD);

    uint32_t expectedEventCount = 0;
    ck_assert_uint_eq (expectedEventCount, eventCount);

    /* Initial State of Condition */
    ck_assert(isConditionEnabled(acserver, conditionInstance) == true);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(retainSent == false);

    /* 1. Alarm Goes Active */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);
    ck_assert(retainSent == true);

    /* 2. Placed OutOfService */
    retval = UA_Server_Condition_removeFromService(acserver, conditionInstance, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);
    ck_assert(retainSent == false);

    /* 3. Alarm Suppressed; No event since OutOfService */
    retval = UA_Server_Condition_suppress(acserver, conditionInstance, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == true);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);
    ck_assert(retainSent == false);

    /* 4. Alarm goes inactive; No event since OutOfService */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == true);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(retainSent == false);

    /* 5. Alarm Not Suppressed; No event since out of service*/
    retval = UA_Server_Condition_unsuppress(acserver, conditionInstance, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(retainSent == false);

    /* 6. Alarm goes active; No event since out of service*/
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);
    ck_assert(retainSent == false);

    /* 7. Alarm no longer OutOfService; Event generated */
    retval = UA_Server_Condition_placeInService(acserver, conditionInstance, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);
    ck_assert(retainSent == true);

    /* 8. Alarm goes inactive */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(retainSent == false);

    /* 9. Alarm Suppressed, No event since not active */
    retval = UA_Server_Condition_suppress(acserver, conditionInstance, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == true);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(retainSent == false);

    /* 10. Alarm goes active, No event since suppressed */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == true);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);
    ck_assert(retainSent == false);

    /* 11. Alarm goes inactive, No event since suppressed */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == true);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(retainSent == false);

    /* 12. Alarm no longer suppressed */
    retval = UA_Server_Condition_unsuppress (acserver, conditionInstance, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(retainSent == false);

    /* 13. Alarm placed out of service */
    retval = UA_Server_Condition_removeFromService (acserver, conditionInstance, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(retainSent == false);

    /* 14. Alarm goes active; No event since out of service */
    retval = UA_Server_Condition_updateActive (acserver, conditionInstance, NULL, true);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);
    ck_assert(retainSent == false);

    /* 15. Alarm goes inactive; No event since out of service */
    retval = UA_Server_Condition_updateActive (acserver, conditionInstance, NULL, false);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(retainSent == false);

    /* 16. Alarm no longer out of service */
    retval = UA_Server_Condition_placeInService(acserver, conditionInstance, NULL, NULL);
    deliverEvents();
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(retainSent == false);

    retval = UA_Server_deleteCondition(
        acserver,
        conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
} END_TEST


START_TEST(exclusiveLimitEvaluateFn) {
    UA_StatusCode retval;

    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test")
    };

    UA_LevelAlarmProperties levelProperties = {
        .hasHighLimit = true,
        .highLimit = 10.0f,
    };

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    retval = UA_Server_createNonExclusiveLevelAlarm (
        acserver,
        UA_NODEID_NULL,
        &conditionProperties,
        &levelProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_LimitState currentState = 0;
    UA_Boolean stateChanged = false;
    retval = UA_Server_NonExclusiveLimitAlarmEvaluateLimitState (acserver, &conditionInstance, 20.0f, &currentState, &stateChanged);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_LimitState testState = 0;
    UA_LIMITSTATE_SET (testState, UA_LIMITSTATE_HIGHSTATEBIT);
    ck_assert_uint_eq(testState, currentState);
    ck_assert (stateChanged == true);

    currentState = 0;
    stateChanged = false;
    retval = UA_Server_NonExclusiveLimitAlarmEvaluateLimitState (acserver, &conditionInstance, 9.0f, &currentState, &stateChanged);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    testState = 0;
    ck_assert_uint_eq(testState, currentState);
    ck_assert (stateChanged == true);

} END_TEST

START_TEST(nonExclusiveLimitEvaluateFn) {
    UA_StatusCode retval;

    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test")
    };

    UA_LevelAlarmProperties levelProperties = {
        .hasHighLimit = true,
        .highLimit = 10.0f,
    };

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    retval = UA_Server_createNonExclusiveLevelAlarm (
        acserver,
        UA_NODEID_NULL,
        &conditionProperties,
        &levelProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_LimitState currentState = 0;
    UA_Boolean stateChanged = false;
    retval = UA_Server_NonExclusiveLimitAlarmEvaluateLimitState (acserver, &conditionInstance, 20.0f, &currentState, &stateChanged);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_LimitState testState = 0;
    UA_LIMITSTATE_SET (testState, UA_LIMITSTATE_HIGHSTATEBIT);
    ck_assert_uint_eq(testState, currentState);
    ck_assert (stateChanged == true);

    currentState = 0;
    stateChanged = false;
    retval = UA_Server_NonExclusiveLimitAlarmEvaluateLimitState (acserver, &conditionInstance, 9.0f, &currentState, &stateChanged);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    testState = 0;
    ck_assert_uint_eq(testState, currentState);
    ck_assert (stateChanged == true);

} END_TEST

static UA_Double evaluateInput = 20.0;

static void *
getEvaluateInput (UA_Server *server, const UA_NodeId *conditionId, void *conditionCtx)
{
    return &evaluateInput;
}

/* Enabling a Condition evaluates it with the user functions. They are called
 * with the server lock held and can use the public API. */
START_TEST(enableEvaluatesCondition) {
    UA_StatusCode retval;

    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test")
    };

    UA_LevelAlarmProperties levelProperties = {
        .hasHighLimit = true,
        .highLimit = 10.0,
    };

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    retval = UA_Server_createExclusiveLevelAlarm (
        acserver,
        UA_NODEID_NULL,
        &conditionProperties,
        &levelProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_ConditionEvaluateFns fns = {
        .getInput = getEvaluateInput,
        .evaluate = (UA_ConditionEvaluateFn) UA_Server_exclusiveLimitAlarmEvaluate_default
    };
    retval = UA_Server_Condition_setEvaluateFns(acserver, conditionInstance, fns);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    if (isConditionEnabled(acserver, conditionInstance))
    {
        retval = UA_Server_Condition_enable(acserver, conditionInstance, false, NULL);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    }
    retval = UA_Server_Condition_enable(acserver, conditionInstance, true, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
} END_TEST

/* The HighHighState of a NonExclusiveLimitAlarm without a HighLimit is read
 * back, so it only changes once and is cleared again */
START_TEST(nonExclusiveHighHighWithoutHigh) {
    UA_StatusCode retval;

    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test")
    };

    UA_LevelAlarmProperties levelProperties = {
        .hasLowLimit = true,
        .lowLimit = 10.0,
        .hasHighHighLimit = true,
        .highHighLimit = 90.0,
    };

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    retval = UA_Server_createNonExclusiveLevelAlarm (
        acserver,
        UA_NODEID_NULL,
        &conditionProperties,
        &levelProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    UA_QualifiedName highHighStateQN = UA_QUALIFIEDNAME(0, "HighHighState");

    UA_LimitState highHighState = 0;
    UA_LIMITSTATE_SET (highHighState, UA_LIMITSTATE_HIGHHIGHSTATEBIT);

    UA_LimitState currentState = 0;
    UA_Boolean stateChanged = false;
    retval = UA_Server_NonExclusiveLimitAlarmEvaluateLimitState (acserver, &conditionInstance, 95.0, &currentState, &stateChanged);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(highHighState, currentState);
    ck_assert (stateChanged == true);
    ck_assert(isConditionTwoStateVariableInTrueState(acserver, conditionInstance, highHighStateQN));

    /* Still above the limit: no change */
    currentState = 0;
    stateChanged = true;
    retval = UA_Server_NonExclusiveLimitAlarmEvaluateLimitState (acserver, &conditionInstance, 95.0, &currentState, &stateChanged);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(highHighState, currentState);
    ck_assert (stateChanged == false);

    /* Back to normal: HighHighState is cleared */
    currentState = 0;
    stateChanged = false;
    retval = UA_Server_NonExclusiveLimitAlarmEvaluateLimitState (acserver, &conditionInstance, 50.0, &currentState, &stateChanged);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(0, currentState);
    ck_assert (stateChanged == true);
    ck_assert(!isConditionTwoStateVariableInTrueState(acserver, conditionInstance, highHighStateQN));
} END_TEST

/* A NonExclusiveLimitAlarm needs at least one limit, which can be any of the
 * four limits */
START_TEST(nonExclusiveSingleLimit) {
    UA_StatusCode retval;

    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test")
    };

    UA_LevelAlarmProperties noLimits;
    memset (&noLimits, 0, sizeof(noLimits));
    UA_NodeId conditionInstance = UA_NODEID_NULL;
    retval = UA_Server_createNonExclusiveLevelAlarm (
        acserver,
        UA_NODEID_NULL,
        &conditionProperties,
        &noLimits,
        &conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_BADCONFIGURATIONERROR);

    UA_LevelAlarmProperties highHighOnly = {
        .hasHighHighLimit = true,
        .highHighLimit = 90.0,
    };
    conditionInstance = UA_NODEID_NULL;
    retval = UA_Server_createNonExclusiveLevelAlarm (
        acserver,
        UA_NODEID_NULL,
        &conditionProperties,
        &highHighOnly,
        &conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_LimitState highHighState = 0;
    UA_LIMITSTATE_SET (highHighState, UA_LIMITSTATE_HIGHHIGHSTATEBIT);
    UA_LimitState currentState = 0;
    UA_Boolean stateChanged = false;
    retval = UA_Server_NonExclusiveLimitAlarmEvaluateLimitState (acserver, &conditionInstance, 95.0, &currentState, &stateChanged);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(highHighState, currentState);
    ck_assert (stateChanged == true);
    ck_assert(isConditionTwoStateVariableInTrueState(acserver, conditionInstance,
                                                     UA_QUALIFIEDNAME(0, "HighHighState")));
} END_TEST

/* OPC UA Part 9, 5.8.17: the ShelvedStateMachine Methods of the ShelvingState
 * child can be called with the ConditionId as the ObjectId */
START_TEST(shelveMethodCalledOnCondition) {
    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test Condition"),
        .canBranch = false
    };

    UA_AlarmConditionProperties alarmProperties;
    memset (&alarmProperties, 0, sizeof(alarmProperties));
    alarmProperties.isShelvable = true;

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    UA_StatusCode status = __UA_Server_createCondition(
        acserver,
        UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE),
        &conditionProperties,
        (UA_ConditionTypeSetupFn) UA_Server_setupAlarmConditionNodes,
        (UA_ConditionTypeSetupFn) UA_Server_initAlarmConditionNodes,
        &alarmProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);

    UA_NodeId shelvingState;
    status = UA_Server_getNodeIdWithBrowseName(acserver, &conditionInstance, UA_QUALIFIEDNAME(0, "ShelvingState"), &shelvingState);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    UA_NodeId timedShelve;
    status = UA_Server_getNodeIdWithBrowseName(acserver, &shelvingState, UA_QUALIFIEDNAME(0, "TimedShelve"), &timedShelve);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);

    UA_Duration shelvingTime = 10000.0;
    UA_Variant input;
    UA_Variant_setScalar(&input, &shelvingTime, &UA_TYPES[UA_TYPES_DURATION]);

    UA_CallMethodRequest request;
    UA_CallMethodRequest_init(&request);
    request.objectId = conditionInstance;
    request.methodId = timedShelve;
    request.inputArgumentsSize = 1;
    request.inputArguments = &input;

    UA_CallMethodResult result = UA_Server_call(acserver, &request);
    ck_assert_uint_eq(result.statusCode, UA_STATUSCODE_GOOD);
    UA_CallMethodResult_clear(&result);

    /* The Condition is now TimedShelved */
    UA_NodeId currentState;
    status = UA_Server_getNodeIdWithBrowseName(acserver, &shelvingState, UA_QUALIFIEDNAME(0, "CurrentState"), &currentState);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    UA_NodeId currentStateId;
    status = UA_Server_getNodeIdWithBrowseName(acserver, &currentState, UA_QUALIFIEDNAME(0, "Id"), &currentStateId);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    UA_Variant value;
    status = UA_Server_readValue(acserver, currentStateId, &value);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    ck_assert(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_NODEID]));
    UA_NodeId timedShelved = UA_NODEID_NUMERIC(0, UA_NS0ID_SHELVEDSTATEMACHINETYPE_TIMEDSHELVED);
    ck_assert(UA_NodeId_equal((UA_NodeId *) value.data, &timedShelved));
    UA_Variant_clear(&value);

    /* An object that is not a Condition still cannot call the Method */
    request.objectId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    result = UA_Server_call(acserver, &request);
    ck_assert_uint_eq(result.statusCode, UA_STATUSCODE_BADMETHODINVALID);
    UA_CallMethodResult_clear(&result);

    UA_NodeId_clear(&currentStateId);
    UA_NodeId_clear(&currentState);
    UA_NodeId_clear(&timedShelve);
    UA_NodeId_clear(&shelvingState);
} END_TEST

static UA_StatusCode
callShelvingMethod(const UA_NodeId *shelvingState, char *method,
                   size_t inputSize, UA_Variant *input) {
    UA_NodeId methodId;
    UA_StatusCode status = UA_Server_getNodeIdWithBrowseName(acserver, shelvingState, UA_QUALIFIEDNAME(0, method), &methodId);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);

    UA_CallMethodRequest request;
    UA_CallMethodRequest_init(&request);
    request.objectId = *shelvingState;
    request.methodId = methodId;
    request.inputArgumentsSize = inputSize;
    request.inputArguments = input;

    UA_CallMethodResult result = UA_Server_call(acserver, &request);
    status = result.statusCode;
    UA_CallMethodResult_clear(&result);
    UA_NodeId_clear(&methodId);
    return status;
}

static void
assertShelvingState(const UA_NodeId *shelvingState, UA_UInt32 expectedState) {
    UA_NodeId currentState;
    UA_StatusCode status = UA_Server_getNodeIdWithBrowseName(acserver, shelvingState, UA_QUALIFIEDNAME(0, "CurrentState"), &currentState);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    UA_NodeId currentStateId;
    status = UA_Server_getNodeIdWithBrowseName(acserver, &currentState, UA_QUALIFIEDNAME(0, "Id"), &currentStateId);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    UA_Variant value;
    status = UA_Server_readValue(acserver, currentStateId, &value);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    ck_assert(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_NODEID]));
    UA_NodeId expected = UA_NODEID_NUMERIC(0, expectedState);
    ck_assert(UA_NodeId_equal((UA_NodeId *) value.data, &expected));
    UA_Variant_clear(&value);
    UA_NodeId_clear(&currentStateId);
    UA_NodeId_clear(&currentState);
}

/* The ShelvedStateMachine Methods are called with the ShelvingState as the
 * ObjectId, including the Methods with a Comment argument */
START_TEST(shelveMethodCalledOnShelvingState) {
    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test Condition"),
        .canBranch = false
    };

    UA_AlarmConditionProperties alarmProperties;
    memset (&alarmProperties, 0, sizeof(alarmProperties));
    alarmProperties.isShelvable = true;

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    UA_StatusCode status = __UA_Server_createCondition(
        acserver,
        UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE),
        &conditionProperties,
        (UA_ConditionTypeSetupFn) UA_Server_setupAlarmConditionNodes,
        (UA_ConditionTypeSetupFn) UA_Server_initAlarmConditionNodes,
        &alarmProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);

    UA_NodeId shelvingState;
    status = UA_Server_getNodeIdWithBrowseName(acserver, &conditionInstance, UA_QUALIFIEDNAME(0, "ShelvingState"), &shelvingState);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);

    UA_LocalizedText comment = UA_LOCALIZEDTEXT("en", "test comment");
    UA_Duration shelvingTime = 10000.0;

    UA_Variant timedShelveInput;
    UA_Variant_setScalar(&timedShelveInput, &shelvingTime, &UA_TYPES[UA_TYPES_DURATION]);
    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "TimedShelve", 1, &timedShelveInput), UA_STATUSCODE_GOOD);
    assertShelvingState(&shelvingState, UA_NS0ID_SHELVEDSTATEMACHINETYPE_TIMEDSHELVED);

    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "Unshelve", 0, NULL), UA_STATUSCODE_GOOD);
    assertShelvingState(&shelvingState, UA_NS0ID_SHELVEDSTATEMACHINETYPE_UNSHELVED);

    UA_Variant timedShelve2Input[2];
    UA_Variant_setScalar(&timedShelve2Input[0], &shelvingTime, &UA_TYPES[UA_TYPES_DURATION]);
    UA_Variant_setScalar(&timedShelve2Input[1], &comment, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "TimedShelve2", 2, timedShelve2Input), UA_STATUSCODE_GOOD);
    assertShelvingState(&shelvingState, UA_NS0ID_SHELVEDSTATEMACHINETYPE_TIMEDSHELVED);

    UA_Variant commentInput;
    UA_Variant_setScalar(&commentInput, &comment, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "Unshelve2", 1, &commentInput), UA_STATUSCODE_GOOD);
    assertShelvingState(&shelvingState, UA_NS0ID_SHELVEDSTATEMACHINETYPE_UNSHELVED);

    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "OneShotShelve2", 1, &commentInput), UA_STATUSCODE_GOOD);
    assertShelvingState(&shelvingState, UA_NS0ID_SHELVEDSTATEMACHINETYPE_ONESHOTSHELVED);

    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "Unshelve", 0, NULL), UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "OneShotShelve", 0, NULL), UA_STATUSCODE_GOOD);
    assertShelvingState(&shelvingState, UA_NS0ID_SHELVEDSTATEMACHINETYPE_ONESHOTSHELVED);

    UA_NodeId_clear(&shelvingState);
} END_TEST

/* Unshelving cancels the TimedShelve timer, so it cannot unshelve a later shelve */
START_TEST(unshelveCancelsTimedShelve) {
    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test Condition"),
        .canBranch = false
    };

    UA_AlarmConditionProperties alarmProperties;
    memset (&alarmProperties, 0, sizeof(alarmProperties));
    alarmProperties.isShelvable = true;

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    UA_StatusCode status = __UA_Server_createCondition(
        acserver,
        UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE),
        &conditionProperties,
        (UA_ConditionTypeSetupFn) UA_Server_setupAlarmConditionNodes,
        (UA_ConditionTypeSetupFn) UA_Server_initAlarmConditionNodes,
        &alarmProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);

    UA_NodeId shelvingState;
    status = UA_Server_getNodeIdWithBrowseName(acserver, &conditionInstance, UA_QUALIFIEDNAME(0, "ShelvingState"), &shelvingState);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);

    UA_Duration shelvingTime = 1000.0;
    UA_Variant timedShelveInput;
    UA_Variant_setScalar(&timedShelveInput, &shelvingTime, &UA_TYPES[UA_TYPES_DURATION]);
    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "TimedShelve", 1, &timedShelveInput), UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "Unshelve", 0, NULL), UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "OneShotShelve", 0, NULL), UA_STATUSCODE_GOOD);

    UA_fakeSleep(1500);
    UA_Server_run_iterate(acserver, false);
    assertShelvingState(&shelvingState, UA_NS0ID_SHELVEDSTATEMACHINETYPE_ONESHOTSHELVED);

    UA_NodeId_clear(&shelvingState);
} END_TEST

/* A TimedShelve rejected for exceeding MaxTimeShelved keeps the timer of the
 * current TimedShelve, so the Condition is still unshelved when it expires */
START_TEST(rejectedTimedShelveKeepsTimer) {
    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test Condition"),
        .canBranch = false
    };

    UA_AlarmConditionProperties alarmProperties;
    memset (&alarmProperties, 0, sizeof(alarmProperties));
    alarmProperties.isShelvable = true;
    alarmProperties.hasMaxTimeShelved = true;
    alarmProperties.maxTimeShelved = 5000.0;

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    UA_StatusCode status = __UA_Server_createCondition(
        acserver,
        UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE),
        &conditionProperties,
        (UA_ConditionTypeSetupFn) UA_Server_setupAlarmConditionNodes,
        (UA_ConditionTypeSetupFn) UA_Server_initAlarmConditionNodes,
        &alarmProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);

    UA_NodeId shelvingState;
    status = UA_Server_getNodeIdWithBrowseName(acserver, &conditionInstance, UA_QUALIFIEDNAME(0, "ShelvingState"), &shelvingState);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);

    UA_Duration shelvingTime = 1000.0;
    UA_Variant timedShelveInput;
    UA_Variant_setScalar(&timedShelveInput, &shelvingTime, &UA_TYPES[UA_TYPES_DURATION]);
    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "TimedShelve", 1, &timedShelveInput), UA_STATUSCODE_GOOD);

    UA_Duration tooLong = 10000.0;
    UA_Variant tooLongInput;
    UA_Variant_setScalar(&tooLongInput, &tooLong, &UA_TYPES[UA_TYPES_DURATION]);
    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "TimedShelve", 1, &tooLongInput),
                      UA_STATUSCODE_BADSHELVINGTIMEOUTOFRANGE);
    assertShelvingState(&shelvingState, UA_NS0ID_SHELVEDSTATEMACHINETYPE_TIMEDSHELVED);

    UA_fakeSleep(1500);
    UA_Server_run_iterate(acserver, false);
    assertShelvingState(&shelvingState, UA_NS0ID_SHELVEDSTATEMACHINETYPE_UNSHELVED);

    UA_NodeId_clear(&shelvingState);
} END_TEST

/* Shelving fails cleanly when the CurrentState of the ShelvingState is
 * missing */
START_TEST(shelveWithoutCurrentState) {
    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test Condition"),
        .canBranch = false
    };

    UA_AlarmConditionProperties alarmProperties;
    memset (&alarmProperties, 0, sizeof(alarmProperties));
    alarmProperties.isShelvable = true;

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    UA_StatusCode status = __UA_Server_createCondition(
        acserver,
        UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE),
        &conditionProperties,
        (UA_ConditionTypeSetupFn) UA_Server_setupAlarmConditionNodes,
        (UA_ConditionTypeSetupFn) UA_Server_initAlarmConditionNodes,
        &alarmProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);

    UA_NodeId shelvingState;
    status = UA_Server_getNodeIdWithBrowseName(acserver, &conditionInstance, UA_QUALIFIEDNAME(0, "ShelvingState"), &shelvingState);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    UA_NodeId currentState;
    status = UA_Server_getNodeIdWithBrowseName(acserver, &shelvingState, UA_QUALIFIEDNAME(0, "CurrentState"), &currentState);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    status = UA_Server_deleteNode(acserver, currentState, true);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);

    status = UA_Server_Condition_timedShelve(acserver, conditionInstance, 1000.0, NULL, NULL);
    ck_assert_uint_ne(status, UA_STATUSCODE_GOOD);

    UA_NodeId_clear(&currentState);
    UA_NodeId_clear(&shelvingState);
} END_TEST

/* SuppressedOrShelved follows the ShelvingState, including the expiry of a
 * TimedShelve, and stays set while the Condition is also suppressed */
START_TEST(shelvingSetsSuppressedOrShelved) {
    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test Condition"),
        .canBranch = false
    };

    UA_AlarmConditionProperties alarmProperties;
    memset (&alarmProperties, 0, sizeof(alarmProperties));
    alarmProperties.isShelvable = true;
    alarmProperties.isSuppressible = true;

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    UA_StatusCode status = __UA_Server_createCondition(
        acserver,
        UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE),
        &conditionProperties,
        (UA_ConditionTypeSetupFn) UA_Server_setupAlarmConditionNodes,
        (UA_ConditionTypeSetupFn) UA_Server_initAlarmConditionNodes,
        &alarmProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    ck_assert(!conditionSuppressedOrShelved(acserver, conditionInstance));

    UA_NodeId shelvingState;
    status = UA_Server_getNodeIdWithBrowseName(acserver, &conditionInstance, UA_QUALIFIEDNAME(0, "ShelvingState"), &shelvingState);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);

    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "OneShotShelve", 0, NULL), UA_STATUSCODE_GOOD);
    ck_assert(conditionSuppressedOrShelved(acserver, conditionInstance));
    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "Unshelve", 0, NULL), UA_STATUSCODE_GOOD);
    ck_assert(!conditionSuppressedOrShelved(acserver, conditionInstance));

    UA_Duration shelvingTime = 1000.0;
    UA_Variant timedShelveInput;
    UA_Variant_setScalar(&timedShelveInput, &shelvingTime, &UA_TYPES[UA_TYPES_DURATION]);
    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "TimedShelve", 1, &timedShelveInput), UA_STATUSCODE_GOOD);
    ck_assert(conditionSuppressedOrShelved(acserver, conditionInstance));

    UA_fakeSleep(1500);
    UA_Server_run_iterate(acserver, false);
    assertShelvingState(&shelvingState, UA_NS0ID_SHELVEDSTATEMACHINETYPE_UNSHELVED);
    ck_assert(!conditionSuppressedOrShelved(acserver, conditionInstance));

    /* Unshelving keeps SuppressedOrShelved while the Condition is suppressed */
    status = UA_Server_Condition_suppress(acserver, conditionInstance, NULL, NULL);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "OneShotShelve", 0, NULL), UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(callShelvingMethod(&shelvingState, "Unshelve", 0, NULL), UA_STATUSCODE_GOOD);
    ck_assert(conditionSuppressedOrShelved(acserver, conditionInstance));

    UA_NodeId_clear(&shelvingState);
} END_TEST

/* The Alarm only becomes inactive once the OffDelay has expired and can be
 * activated again afterwards */
START_TEST(offDelay) {
    UA_CreateConditionProperties conditionProperties = {
        .sourceNode = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER),
        .browseName = UA_QUALIFIEDNAME(0, "Test Condition"),
        .canBranch = false
    };

    UA_AlarmConditionProperties alarmProperties;
    memset (&alarmProperties, 0, sizeof(alarmProperties));
    alarmProperties.hasOffDelay = true;
    alarmProperties.offDelay = 1000.0;

    UA_NodeId conditionInstance = UA_NODEID_NULL;
    UA_StatusCode status = __UA_Server_createCondition(
        acserver,
        UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE),
        &conditionProperties,
        (UA_ConditionTypeSetupFn) UA_Server_setupAlarmConditionNodes,
        (UA_ConditionTypeSetupFn) UA_Server_initAlarmConditionNodes,
        &alarmProperties,
        &conditionInstance
    );
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    UA_QualifiedName activeStateQN = UA_QUALIFIEDNAME(0, "ActiveState");

    status = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    ck_assert(isConditionTwoStateVariableInTrueState(acserver, conditionInstance, activeStateQN));

    /* Stays active until the OffDelay expires */
    status = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    ck_assert(isConditionTwoStateVariableInTrueState(acserver, conditionInstance, activeStateQN));

    UA_fakeSleep(1500);
    UA_Server_run_iterate(acserver, false);
    ck_assert(!isConditionTwoStateVariableInTrueState(acserver, conditionInstance, activeStateQN));

    status = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    ck_assert_uint_eq(status, UA_STATUSCODE_GOOD);
    ck_assert(isConditionTwoStateVariableInTrueState(acserver, conditionInstance, activeStateQN));
} END_TEST

#endif

int main(void) {
    Suite *s = suite_create("server_alarmcondition");

#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
    TCase *tc = tcase_create("Alarms and Conditions");
    tcase_add_test(tc, createDelete);
    tcase_add_test(tc, createMultiple);
    tcase_add_test(tc, conditionSequence1);
    tcase_add_test(tc, conditionSequence2);
    tcase_add_test(tc, enableDisable);
    tcase_add_test(tc, disableNotifiesRetainFalse);
    tcase_add_test(tc, enableRestoresBranchRetain);
    tcase_add_test(tc, exclusiveLimitEvaluateFn);
    tcase_add_test(tc, nonExclusiveLimitEvaluateFn);
    tcase_add_test(tc, nonExclusiveHighHighWithoutHigh);
    tcase_add_test(tc, nonExclusiveSingleLimit);
    tcase_add_test(tc, enableEvaluatesCondition);
    tcase_add_test(tc, shelveMethodCalledOnCondition);
    tcase_add_test(tc, shelveMethodCalledOnShelvingState);
    tcase_add_test(tc, offDelay);
    tcase_add_test(tc, unshelveCancelsTimedShelve);
    tcase_add_test(tc, rejectedTimedShelveKeepsTimer);
    tcase_add_test(tc, shelvingSetsSuppressedOrShelved);
    tcase_add_test(tc, shelveWithoutCurrentState);
    tcase_add_checked_fixture(tc, setup, teardown);
    suite_add_tcase(s, tc);

    TCase *tc1 = tcase_create("Alarms and Conditions Supports Filtered Retain True");
    tcase_add_test(tc1, conditionSequence3);
    tcase_add_checked_fixture(tc1, setupSupportsFilteredRetain, teardown);
    suite_add_tcase(s, tc1);



#endif

    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
