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
#include "open62541_queue.h"

UA_Server *acserver;
static uint32_t eventCount = 0;


static void setup(void) {
    eventCount = 0;
    acserver = UA_Server_new();
    UA_ServerConfig_setDefault(UA_Server_getConfig(acserver));
}

static void setupSupportsFilteredRetain(void) {
    setup();
    UA_ServerConfig *config = UA_Server_getConfig(acserver);
    config->supportsFilteredRetain = true;
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
    if(!*autoConfirm)  UA_Server_Condition_setConfirmRequired(server, *id);
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
    UA_Server_Condition_setAcknowledgeRequired(server, *id);
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode onInactive(UA_Server *server, const UA_NodeId *id, void *ctx)
{
    return UA_STATUSCODE_GOOD;
}

const UA_ConditionImplCallbacks callbacks = {
    .onAcked = onAcked,
    .onConfirmed = onConfirmed,
    .onActive = onActive,
    .onInactive = onInactive
};

static void conditionSequence1CB (UA_Server *server, UA_UInt32 monId, void *monContext,
                                  size_t nEventFields, const UA_Variant *eventFields)
{
    eventCount++;
}


/* Based on https://reference.opcfoundation.org/Core/Part9/v105/docs/B.1.2 */
START_TEST(conditionSequence1) {

    UA_StatusCode retval;
    UA_ConditionProperties conditionProperties;
    conditionProperties.name = UA_QUALIFIEDNAME(0, "Test Condition");
    conditionProperties.hierarchialReferenceType = UA_NODEID_NULL;
    conditionProperties.source = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    conditionProperties.canBranch = false;

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

    retval = UA_Server_Condition_setImplCallbacks(acserver, conditionInstance, &callbacks);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    /* Create monitored event */
    UA_MonitoredItemCreateRequest req;
    UA_MonitoredItemCreateRequest_init(&req);
    req.itemToMonitor.nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    req.monitoringMode = UA_MONITORINGMODE_REPORTING;
    req.itemToMonitor.attributeId = UA_ATTRIBUTEID_EVENTNOTIFIER;
    req.requestedParameters.samplingInterval = 250;
    req.requestedParameters.discardOldest = true;
    req.requestedParameters.queueSize = 1;

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

    UA_MonitoredItemCreateResult res = UA_Server_createEventMonitoredItem(
        acserver,
        UA_TIMESTAMPSTORETURN_NEITHER,
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

    /* Initial State of Condition */
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);

    /* 1. Alarm goes Active */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionAcked(acserver, conditionInstance) == false);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 2. Condition Acknowledged Confirm required */
    retval = UA_Server_Condition_acknowledge(acserver, conditionInstance, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 3. Alarm goes inactive */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 4. Alarm goes inactive */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 5. Condition confirmed */
    retval = UA_Server_Condition_confirm (acserver, conditionInstance, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);

    /* 6. Alarm goes active */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionAcked(acserver, conditionInstance) == false);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 7. Alarm goes inactive */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == false);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 8. Condition Acknowledged Confirm required */
    retval = UA_Server_Condition_acknowledge(acserver, conditionInstance, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionAcked(acserver, conditionInstance) == true);
    ck_assert(isConditionConfirmed(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);

    /* 10. Condition confirmed */
    retval = UA_Server_Condition_confirm (acserver, conditionInstance, NULL);
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
                                  size_t nEventFields, const UA_Variant *eventFields)
{
    eventCount++;
    EventCBCtx *ctx= (EventCBCtx *) monContext;

    UA_NodeId conditionId = *(UA_NodeId *) eventFields[0].data;
    UA_NodeId branchId = *(UA_NodeId *) eventFields[1].data;
    UA_Boolean retain = *(UA_Boolean*) eventFields[2].data;
    UA_Boolean acked = *(UA_Boolean*) eventFields[3].data;
    UA_Boolean confirmed = *(UA_Boolean*) eventFields[4].data;
    UA_Boolean active = *(UA_Boolean*) eventFields[5].data;

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

/* Based on https://reference.opcfoundation.org/Core/Part9/v105/docs/B.1.3 */
START_TEST(conditionSequence2) {

    EventCBCtx ctx;
    memset(&ctx, 0, sizeof(ctx));

    UA_StatusCode retval;
    UA_ConditionProperties conditionProperties;
    conditionProperties.name = UA_QUALIFIEDNAME(0, "Test Condition");
    conditionProperties.hierarchialReferenceType = UA_NODEID_NULL;
    conditionProperties.source = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    conditionProperties.canBranch = true;

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

    retval = UA_Server_Condition_setImplCallbacks(acserver, conditionInstance, &callbacks);
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
    req.requestedParameters.queueSize = 1;

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

    UA_MonitoredItemCreateResult res = UA_Server_createEventMonitoredItem(
        acserver,
        UA_TIMESTAMPSTORETURN_NEITHER,
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
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.mainBranchState.active == true);
    ck_assert(ctx.mainBranchState.acked == false);
    ck_assert(ctx.mainBranchState.confirmed == true);
    ck_assert(ctx.mainBranchState.retain == true);

    /* 2. Alarm acked */
    retval = UA_Server_Condition_acknowledge(acserver, conditionInstance, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.mainBranchState.active == true);
    ck_assert(ctx.mainBranchState.acked == true);
    ck_assert(ctx.mainBranchState.confirmed == false);
    ck_assert(ctx.mainBranchState.retain == true);

    /* 3. Alarm goes Inactive */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.mainBranchState.active == false);
    ck_assert(ctx.mainBranchState.acked == true);
    ck_assert(ctx.mainBranchState.confirmed == false);
    ck_assert(ctx.mainBranchState.retain == true);

    /* 4. Alarm Confirmed */
    retval = UA_Server_Condition_confirm (acserver, conditionInstance, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.mainBranchState.active == false);
    ck_assert(ctx.mainBranchState.acked == true);
    ck_assert(ctx.mainBranchState.confirmed == true);
    ck_assert(ctx.mainBranchState.retain == false);

    /* 5. Alarm goes active */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.mainBranchState.active == true);
    ck_assert(ctx.mainBranchState.acked == false);
    ck_assert(ctx.mainBranchState.confirmed == true);
    ck_assert(ctx.mainBranchState.retain == true);

    /* 6. Alarm goes inactive */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
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
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.mainBranchState.active == true);
    ck_assert(ctx.mainBranchState.acked == false);
    ck_assert(ctx.mainBranchState.confirmed == true);
    ck_assert(ctx.mainBranchState.retain == true);

    /* 9. Branch acked */
    retval = UA_Server_Condition_acknowledge(acserver, ctx.branch1, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.branch1State.active == true);
    ck_assert(ctx.branch1State.acked == true);
    ck_assert(ctx.branch1State.confirmed == false);
    ck_assert(ctx.branch1State.retain == true);

    /* 10. Alarm goes inactive again */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
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
    retval = UA_Server_Condition_confirm(acserver, ctx.branch1, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(ctx.branch1State.active == true);
    ck_assert(ctx.branch1State.acked == true);
    ck_assert(ctx.branch1State.confirmed == true);
    ck_assert(ctx.branch1State.retain == false);

    /* 13. Branch #2 Acked */
    retval = UA_Server_Condition_acknowledge(acserver, ctx.branch2, NULL);
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

static void conditionSequence3CB (UA_Server *server, UA_UInt32 monId, void *monContext,
                                  size_t nEventFields, const UA_Variant *eventFields)
{
    eventCount++;
    UA_NodeId conditionId = *(UA_NodeId *) eventFields[0].data;
    UA_Boolean retain = *(UA_Boolean*) eventFields[1].data;
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
    UA_ConditionProperties conditionProperties;
    conditionProperties.name = UA_QUALIFIEDNAME(0, "Test Condition");
    conditionProperties.hierarchialReferenceType = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);//UA_NODEID_NULL;
    conditionProperties.source = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    conditionProperties.canBranch = false;

    UA_AlarmConditionProperties alarmProperties;
    memset(&alarmProperties, 0, sizeof(alarmProperties));
    alarmProperties.isSuppressible = true;
    alarmProperties.isServiceable = true;

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

    /* Create monitored event */
    UA_MonitoredItemCreateRequest req;
    UA_MonitoredItemCreateRequest_init(&req);
    req.itemToMonitor.nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    req.monitoringMode = UA_MONITORINGMODE_REPORTING;
    req.itemToMonitor.attributeId = UA_ATTRIBUTEID_EVENTNOTIFIER;
    req.requestedParameters.samplingInterval = 250;
    req.requestedParameters.discardOldest = true;
    req.requestedParameters.queueSize = 1;

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
    UA_MonitoredItemCreateResult res = UA_Server_createEventMonitoredItem(
        acserver,
        UA_TIMESTAMPSTORETURN_NEITHER,
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
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);
    ck_assert(retainSent == true);

    /* 2. Placed OutOfService */
    retval = UA_Server_Condition_removeFromService(acserver, conditionInstance, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);
    ck_assert(retainSent == false);

    /* 3. Alarm Suppressed; No event since OutOfService */
    retval = UA_Server_Condition_suppress(acserver, conditionInstance, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == true);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);
    ck_assert(retainSent == false);

    /* 4. Alarm goes inactive; No event since OutOfService */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, false);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == true);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(retainSent == false);

    /* 5. Alarm Not Suppressed; No event since out of service*/
    retval = UA_Server_Condition_unsuppress(acserver, conditionInstance, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(retainSent == false);

    /* 6. Alarm goes active; No event since out of service*/
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == true);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);
    ck_assert(retainSent == false);

    /* 7. Alarm no longer OutOfService; Event generated */
    retval = UA_Server_Condition_placeInService(acserver, conditionInstance, NULL);
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
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    expectedEventCount++;
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == false);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(retainSent == false);

    /* 9. Alarm Suppressed, No event since not active */
    retval = UA_Server_Condition_suppress(acserver, conditionInstance, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == false);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == true);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == false);
    ck_assert(retainSent == false);

    /* 10. Alarm goes active, No event since suppressed */
    retval = UA_Server_Condition_updateActive(acserver, conditionInstance, NULL, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq (expectedEventCount, eventCount);
    ck_assert(isConditionActive(acserver, conditionInstance) == true);
    ck_assert(isConditionSuppressed(acserver, conditionInstance) == true);
    ck_assert(isConditionOutOfService(acserver, conditionInstance) == false);
    ck_assert(conditionRetain(acserver, conditionInstance) == true);
    ck_assert(retainSent == false);

    //UA_Server_run_startup(acserver);
    //while (1){ UA_Server_run_iterate(acserver, true);}

    retval = UA_Server_deleteCondition(
        acserver,
        conditionInstance
    );
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
} END_TEST

#endif

int main(void) {
    Suite *s = suite_create("server_alarmcondition");

#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
    TCase *tc_call = tcase_create("Alarms and Conditions");
    tcase_add_test(tc_call, createDelete);
    tcase_add_test(tc_call, conditionSequence1);
    tcase_add_test(tc_call, conditionSequence2);
    tcase_add_checked_fixture(tc_call, setup, teardown);
    suite_add_tcase(s, tc_call);

    TCase *tc_call1 = tcase_create("Alarms and Conditions Supports Filtered Retain True");
    tcase_add_test(tc_call1, conditionSequence3);
    tcase_add_checked_fixture(tc_call1, setupSupportsFilteredRetain, teardown);
    suite_add_tcase(s, tc_call1);
#endif

    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
