/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 *    Copyright 2018 (c) Hilscher Gesellschaft für Systemautomation mbH (Author: Sameer AL-Qadasi)
 *    Copyright 2020-2022 (c) Christian von Arnim, ISW University of Stuttgart (for VDW and umati)
 *    Copyright 2024 (c) IOTechSystems (Author: Joe Riemersma)
 */

#include "ua_server_internal.h"

#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

typedef enum {
    UA_INACTIVE = 0,
    UA_ACTIVE,
} UA_ActiveState;

/* In Alarms and Conditions first implementation, conditionBranchId is always
 * equal to NULL NodeId (UA_NODEID_NULL). That ConditionBranch represents the
 * current state Condition. The current state is determined by the last Event
 * triggered (lastEventId). See Part 9, 5.5.2, BranchId. */
typedef struct UA_ConditionBranch {
    LIST_ENTRY(UA_ConditionBranch) listEntry;
    UA_NodeId conditionBranchId;
    UA_ByteString lastEventId;
    UA_Boolean isCallerAC;
} UA_ConditionBranch;

/* In Alarms and Conditions first implementation, A Condition
 * have only one ConditionBranch entry. */
typedef struct UA_Condition {
    LIST_ENTRY(UA_Condition) listEntry;
    LIST_HEAD(, UA_ConditionBranch) conditionBranches;
    UA_NodeId conditionId;
    UA_UInt16 lastSeverity;
    UA_DateTime lastSeveritySourceTimeStamp;
    UA_ActiveState lastActiveState;
    UA_ActiveState currentActiveState;

    /* These callbacks are defined by the user and must not be called with a
     * locked server mutex */
    struct {
        UA_TwoStateVariableChangeCallback enableStateCallback;
        UA_TwoStateVariableChangeCallback ackStateCallback;
        UA_Boolean ackedRemoveBranch;
        UA_TwoStateVariableChangeCallback confirmStateCallback;
        UA_Boolean confirmedRemoveBranch;
        UA_TwoStateVariableChangeCallback activeStateCallback;
    } callbacks;

} UA_Condition;

/* A ConditionSource can have multiple Conditions. */
struct UA_ConditionSource {
    LIST_ENTRY(UA_ConditionSource) listEntry;
    LIST_HEAD(, UA_Condition) conditions;
    UA_NodeId conditionSourceId;
};

#define CONDITION_SEVERITYCHANGECALLBACK_ENABLE

/* Condition Field Names */
#define CONDITION_FIELD_EVENTID                                "EventId"
#define CONDITION_FIELD_EVENTTYPE                              "EventType"
#define CONDITION_FIELD_SOURCENODE                             "SourceNode"
#define CONDITION_FIELD_SOURCENAME                             "SourceName"
#define CONDITION_FIELD_TIME                                   "Time"
#define CONDITION_FIELD_RECEIVETIME                            "ReceiveTime"
#define CONDITION_FIELD_MESSAGE                                "Message"
#define CONDITION_FIELD_SEVERITY                               "Severity"
#define CONDITION_FIELD_CONDITIONNAME                          "ConditionName"
#define CONDITION_FIELD_BRANCHID                               "BranchId"
#define CONDITION_FIELD_RETAIN                                 "Retain"
#define CONDITION_FIELD_ENABLEDSTATE                           "EnabledState"
#define CONDITION_FIELD_TWOSTATEVARIABLE_ID                    "Id"
#define CONDITION_FIELD_QUALITY                                "Quality"
#define CONDITION_FIELD_LASTSEVERITY                           "LastSeverity"
#define CONDITION_FIELD_COMMENT                                "Comment"
#define CONDITION_FIELD_CLIENTUSERID                           "ClientUserId"
#define CONDITION_FIELD_CONDITIONVARIABLE_SOURCETIMESTAMP      "SourceTimestamp"
#define CONDITION_FIELD_DISABLE                                "Disable"
#define CONDITION_FIELD_ENABLE                                 "Enable"
#define CONDITION_FIELD_ADDCOMMENT                             "AddComment"
#define CONDITION_FIELD_CONDITIONREFRESH                       "ConditionRefresh"
#define CONDITION_FIELD_ACKEDSTATE                             "AckedState"
#define CONDITION_FIELD_CONFIRMEDSTATE                         "ConfirmedState"
#define CONDITION_FIELD_ACKNOWLEDGE                            "Acknowledge"
#define CONDITION_FIELD_CONFIRM                                "Confirm"
#define CONDITION_FIELD_ACTIVESTATE                            "ActiveState"
#define CONDITION_FIELD_INPUTNODE                              "InputNode"
#define CONDITION_FIELD_LATCHEDSTATE                           "LatchedState"
#define CONDITION_FIELD_SUPPRESSEDSTATE                        "SuppressedState"
#define CONDITION_FIELD_OUTOFSERVICESTATE                      "OutOfServiceState"
#define CONDITION_FIELD_MAXTIMESHELVED                         "MaxTimeShelved"
#define CONDITION_FIELD_SUPPRESSEDORSHELVED                    "SuppressedOrShelved"
#define CONDITION_FIELD_NORMALSTATE                            "NormalState"
#define CONDITION_FIELD_ONDELAY                                "OnDelay"
#define CONDITION_FIELD_OFFDELAY                               "OffDelay"
#define CONDITION_FIELD_REALARMTIME                            "ReAlarmTime"
#define CONDITION_FIELD_REALARMREPEATCOUNT                     "ReAlarmRepeatCount"
#define CONDITION_FIELD_HIGHHIGHLIMIT                          "HighHighLimit"
#define CONDITION_FIELD_HIGHLIMIT                              "HighLimit"
#define CONDITION_FIELD_LOWLIMIT                               "LowLimit"
#define CONDITION_FIELD_LOWLOWLIMIT                            "LowLowLimit"
#define CONDITION_FIELD_PROPERTY_EFFECTIVEDISPLAYNAME          "EffectiveDisplayName"
#define CONDITION_FIELD_LIMITSTATE                             "LimitState"
#define CONDITION_FIELD_CURRENTSTATE                           "CurrentState"
#define CONDITION_FIELD_HIGHHIGHSTATE                          "HighHighState"
#define CONDITION_FIELD_HIGHSTATE                              "HighState"
#define CONDITION_FIELD_LOWSTATE                               "LowState"
#define CONDITION_FIELD_LOWLOWSTATE                            "LowLowState"
#define CONDITION_FIELD_DIALOGSTATE                            "DialogState"
#define CONDITION_FIELD_PROMPT                                 "Prompt"
#define CONDITION_FIELD_RESPONSEOPTIONSET                      "ResponseOptionSet"
#define CONDITION_FIELD_DEFAULTRESPONSE                        "DefaultResponse"
#define CONDITION_FIELD_LASTRESPONSE                           "LastResponse"
#define CONDITION_FIELD_OKRESPONSE                             "OkResponse"
#define CONDITION_FIELD_CANCELRESPONSE                         "CancelResponse"
#define CONDITION_FIELD_RESPOND                                "Respond"
#define CONDITION_FIELD_ENGINEERINGUNITS                       "EngineeringUnits"
#define CONDITION_FIELD_EXPIRATION_DATE                        "ExpirationDate"
#define REFRESHEVENT_START_IDX                                 0
#define REFRESHEVENT_END_IDX                                   1
#define REFRESHEVENT_SEVERITY_DEFAULT                          100
#define EXPIRATION_LIMIT_DEFAULT_VALUE                         15

#ifdef UA_ENABLE_ENCRYPTION
#define CONDITION_FIELD_EXPIRATION_LIMIT                       "ExpirationLimit"
#endif

#define LOCALE                                                 "en"
#define LOCALE_NULL                                             ""
#define TEXT_NULL                                               ""
#define ENABLED_TEXT                                           "Enabled"
#define DISABLED_TEXT                                          "Disabled"
#define ENABLED_MESSAGE                                        "The alarm was enabled"
#define DISABLED_MESSAGE                                       "The alarm was disabled"
#define COMMENT_MESSAGE                                        "A comment was added"
#define SEVERITY_INCREASED_MESSAGE                             "The alarm severity has increased"
#define SEVERITY_DECREASED_MESSAGE                             "The alarm severity has decreased"
#define ACKED_TEXT                                             "Acknowledged"
#define UNACKED_TEXT                                           "Unacknowledged"
#define CONFIRMED_TEXT                                         "Confirmed"
#define UNCONFIRMED_TEXT                                       "Unconfirmed"
#define LATCHED_TEXT                                           "Latched"
#define NOT_LATCHED_TEXT                                       "Not Latched"
#define SUPPRESSED_TEXT                                        "Suppressed"
#define NOT_SUPPRESSED_TEXT                                    "Not Suppresssed"
#define IN_SERVICE_TEXT                                        "In Service"
#define OUT_OF_SERVICE_TEXT                                    "Out Of Service"
#define ACKED_MESSAGE                                          "The alarm was acknowledged"
#define CONFIRMED_MESSAGE                                      "The alarm was confirmed"
#define ACTIVE_TEXT                                            "Active"
#define ACTIVE_HIGHHIGH_TEXT                                   "HighHigh active"
#define ACTIVE_HIGH_TEXT                                       "High active"
#define ACTIVE_LOW_TEXT                                        "Low active"
#define ACTIVE_LOWLOW_TEXT                                     "LowLow active"
#define INACTIVE_TEXT                                          "Inactive"

#define STATIC_QN(name) {0, UA_STRING_STATIC(name)}
static const UA_QualifiedName fieldEnabledStateQN = STATIC_QN(CONDITION_FIELD_ENABLEDSTATE);
static const UA_QualifiedName fieldRetainQN = STATIC_QN(CONDITION_FIELD_RETAIN);
static const UA_QualifiedName twoStateVariableIdQN = STATIC_QN(CONDITION_FIELD_TWOSTATEVARIABLE_ID);
static const UA_QualifiedName fieldMessageQN = STATIC_QN(CONDITION_FIELD_MESSAGE);
static const UA_QualifiedName fieldAckedStateQN = STATIC_QN(CONDITION_FIELD_ACKEDSTATE);
static const UA_QualifiedName fieldConfirmedStateQN = STATIC_QN(CONDITION_FIELD_CONFIRMEDSTATE);
static const UA_QualifiedName fieldActiveStateQN = STATIC_QN(CONDITION_FIELD_ACTIVESTATE);
static const UA_QualifiedName fieldLatchedStateQN = STATIC_QN(CONDITION_FIELD_LATCHEDSTATE);
static const UA_QualifiedName fieldSuppressedStateQN = STATIC_QN(CONDITION_FIELD_SUPPRESSEDSTATE);
static const UA_QualifiedName fieldOutOfServiceStateQN = STATIC_QN(CONDITION_FIELD_OUTOFSERVICESTATE);
static const UA_QualifiedName fieldMaxTimeShelvedQN = STATIC_QN(CONDITION_FIELD_MAXTIMESHELVED);
static const UA_QualifiedName fieldOnDelayQN = STATIC_QN(CONDITION_FIELD_ONDELAY);
static const UA_QualifiedName fieldOffDelayQN = STATIC_QN(CONDITION_FIELD_OFFDELAY);
static const UA_QualifiedName fieldReAlarmTimeQN = STATIC_QN(CONDITION_FIELD_REALARMTIME);
static const UA_QualifiedName fieldReAlarmRepeatCountQN = STATIC_QN(CONDITION_FIELD_REALARMREPEATCOUNT);
static const UA_QualifiedName fieldTimeQN = STATIC_QN(CONDITION_FIELD_TIME);
static const UA_QualifiedName fieldCommentQN = STATIC_QN(CONDITION_FIELD_COMMENT);
static const UA_QualifiedName fieldEventIdQN = STATIC_QN(CONDITION_FIELD_EVENTID);
static const UA_QualifiedName fieldBranchIdQN = STATIC_QN(CONDITION_FIELD_BRANCHID);
static const UA_QualifiedName fieldSourceQN = STATIC_QN(CONDITION_FIELD_SOURCENODE);
static const UA_QualifiedName fieldLimitStateQN = STATIC_QN(CONDITION_FIELD_LIMITSTATE);
static const UA_QualifiedName fieldLowLimitQN = STATIC_QN(CONDITION_FIELD_LOWLIMIT);
static const UA_QualifiedName fieldLowLowLimitQN = STATIC_QN(CONDITION_FIELD_LOWLOWLIMIT);
static const UA_QualifiedName fieldHighLimitQN = STATIC_QN(CONDITION_FIELD_HIGHLIMIT);
static const UA_QualifiedName fieldHighHighLimitQN = STATIC_QN(CONDITION_FIELD_HIGHHIGHLIMIT);
static const UA_QualifiedName fieldEngineeringUnitsQN = STATIC_QN(CONDITION_FIELD_ENGINEERINGUNITS);
static const UA_QualifiedName fieldExpirationDateQN = STATIC_QN(CONDITION_FIELD_EXPIRATION_DATE);

#ifdef UA_ENABLE_ENCRYPTION
static const UA_QualifiedName fieldExpirationLimitQN = STATIC_QN(CONDITION_FIELD_EXPIRATION_LIMIT);
#endif

#define CONDITION_ASSERT_RETURN_RETVAL(retval, logMessage, deleteFunction)                \
    {                                                                                     \
        if(retval != UA_STATUSCODE_GOOD) {                                                \
            UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,                 \
                         logMessage". StatusCode %s", UA_StatusCode_name(retval));        \
            deleteFunction                                                                \
            return retval;                                                                \
        }                                                                                 \
    }

#define CONDITION_ASSERT_GOTOLABEL(retval, logMessage, label)                \
    {                                                                                     \
        if(retval != UA_STATUSCODE_GOOD) {                                                \
            UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,                 \
                         logMessage". StatusCode %s", UA_StatusCode_name(retval));        \
            goto label;                                                                    \
        }                                                                                 \
    }

#define CONDITION_ASSERT_RETURN_VOID(retval, logMessage, deleteFunction)                  \
    {                                                                                     \
        if(retval != UA_STATUSCODE_GOOD) {                                                \
            UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,                 \
                         logMessage". StatusCode %s", UA_StatusCode_name(retval));        \
            deleteFunction                                                                \
            return;                                                                       \
        }                                                                                 \
    }

/* Get the node id of the condition state */
static UA_StatusCode
UA_Server_getConditionBranchNodeId(
    UA_Server *server,
    const UA_NodeId *conditionId,
    const UA_ByteString *eventId,
    UA_NodeId *outConditionBranchNodeId
);


static UA_StatusCode
setupConditionNodes (UA_Server *server, const UA_NodeId *condition,
                     const UA_NodeId *conditionType,
                     const UA_ConditionProperties *properties);

static UA_StatusCode
setConditionField(UA_Server *server, const UA_NodeId condition,
                  const UA_Variant* value, const UA_QualifiedName fieldName);

static UA_StatusCode
setConditionVariableFieldProperty(UA_Server *server, const UA_NodeId condition,
                                  const UA_Variant* value,
                                  const UA_QualifiedName variableFieldName,
                                  const UA_QualifiedName variablePropertyName);

static UA_StatusCode
triggerConditionEvent(UA_Server *server, const UA_NodeId condition,
                      const UA_NodeId conditionSource, UA_ByteString *outEventId);

static UA_StatusCode
addConditionOptionalField(UA_Server *server, const UA_NodeId condition,
                          const UA_NodeId conditionType, const UA_QualifiedName fieldName,
                          UA_NodeId *outOptionalNode);

static UA_StatusCode
getConditionFieldNodeId(UA_Server *server, const UA_NodeId *conditionNodeId,
                        const UA_QualifiedName* fieldName, UA_NodeId *outFieldNodeId);

static UA_StatusCode
getConditionFieldPropertyNodeId(UA_Server *server, const UA_NodeId *originCondition,
                                const UA_QualifiedName* variableFieldName,
                                const UA_QualifiedName* variablePropertyName,
                                UA_NodeId *outFieldPropertyNodeId);

static UA_StatusCode
getNodeIdValueOfConditionField(UA_Server *server, const UA_NodeId *condition,
                               UA_QualifiedName fieldName, UA_NodeId *outNodeId);


static UA_ConditionSource *
getConditionSource(UA_Server *server, const UA_NodeId *sourceId) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);
    UA_ConditionSource *cs;
    LIST_FOREACH(cs, &server->conditionSources, listEntry) {
        if(UA_NodeId_equal(&cs->conditionSourceId, sourceId))
            return cs;
    }
    return NULL;
}

static UA_Condition *
getCondition(UA_Server *server, const UA_NodeId *sourceId, const UA_NodeId *conditionId)
{
    UA_LOCK_ASSERT(&server->serviceMutex, 1);
    UA_ConditionSource *cs = getConditionSource(server, sourceId);
    if(!cs)
        return NULL;

    UA_Condition *c;
    LIST_FOREACH(c, &cs->conditions, listEntry) {
        if(UA_NodeId_equal(&c->conditionId, conditionId))
            return c;
    }
    return NULL;
}

static UA_NodeId getTypeDefinitionId(UA_Server *server, const UA_NodeId *targetId)
{
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bd.includeSubtypes = false;
    bd.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASTYPEDEFINITION);
    bd.nodeId = *targetId;
    bd.resultMask = UA_BROWSERESULTMASK_TYPEDEFINITION;
    UA_UInt32 maxRefs = 1;
    UA_BrowseResult br;
    UA_BrowseResult_init (&br);
    Operation_Browse(server, &server->adminSession, &maxRefs, &bd, &br);
    if (br.statusCode != UA_STATUSCODE_GOOD || br.referencesSize != 1)
    {
        return UA_NODEID_NULL;
    }
    UA_NodeId id = br.references->nodeId.nodeId;
    br.references->nodeId.nodeId = UA_NODEID_NULL;
    UA_BrowseResult_clear(&br);
    return id;
}

static UA_Boolean
isBranchIdMainCondition (UA_Server *server, const UA_NodeId *conditionBranchId)
{
    UA_NodeId branchIdFieldValue;
    UA_LOCK_ASSERT(&server->serviceMutex, 1);
    UA_StatusCode retval = getNodeIdValueOfConditionField(server, conditionBranchId, fieldBranchIdQN, &branchIdFieldValue);
    if (retval != UA_STATUSCODE_GOOD) return false;
    UA_Boolean isMainCondition = UA_NodeId_isNull(&branchIdFieldValue);
    UA_NodeId_clear(&branchIdFieldValue);
    return isMainCondition;

}

static UA_Boolean
isTwoStateVariableInTrueState(UA_Server *server, const UA_NodeId *condition,
                              const UA_QualifiedName *twoStateVariable) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    /* Get TwoStateVariableId NodeId */
    UA_NodeId twoStateVariableIdNodeId;
    UA_StatusCode retval = getConditionFieldPropertyNodeId(server, condition, twoStateVariable,
                                                           &twoStateVariableIdQN,
                                                           &twoStateVariableIdNodeId);
    if(retval != UA_STATUSCODE_GOOD) {
        UA_LOG_WARNING(server->config.logging, UA_LOGCATEGORY_USERLAND,
                       "TwoStateVariable/Id not found. StatusCode %s", UA_StatusCode_name(retval));
        return false; //TODO maybe a better error handling?
    }

    /* Read Id value */
    UA_Variant tOutVariant;
    retval = readWithReadValue(server, &twoStateVariableIdNodeId, UA_ATTRIBUTEID_VALUE, &tOutVariant);
    if(retval != UA_STATUSCODE_GOOD ||
       !UA_Variant_hasScalarType(&tOutVariant, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_NodeId_clear(&twoStateVariableIdNodeId);
        return false;
    }

    UA_NodeId_clear(&twoStateVariableIdNodeId);

    if(*(UA_Boolean *)tOutVariant.data == true) {
        UA_Variant_clear(&tOutVariant);
        return true;
    }

    UA_Variant_clear(&tOutVariant);
    return false;
}

static UA_Boolean
UA_Server_isTwoStateVariableInTrueState(UA_Server *server, const UA_NodeId *condition,
                                        const UA_QualifiedName *twoStateVariable) {
    UA_LOCK(&server->serviceMutex);
    UA_Boolean res = isTwoStateVariableInTrueState(server, condition, twoStateVariable);
    UA_UNLOCK(&server->serviceMutex);
    return res;
}

static inline UA_Boolean
conditionConfirmed (UA_Server *server, const UA_NodeId *conditionId)
{
    return isTwoStateVariableInTrueState (server, conditionId, &fieldConfirmedStateQN);
}

static inline UA_Boolean
conditionAcked (UA_Server *server, const UA_NodeId *conditionId)
{
    return isTwoStateVariableInTrueState (server, conditionId, &fieldAckedStateQN);
}

static inline UA_Boolean
conditionEnabled (UA_Server *server, const UA_NodeId *conditionId)
{
    return isTwoStateVariableInTrueState (server, conditionId, &fieldEnabledStateQN);
}

static UA_Boolean
UA_Server_conditionEnabled (UA_Server *server, const UA_NodeId *conditionId)
{
    UA_LOCK(&server->serviceMutex);
    UA_Boolean enabled = isTwoStateVariableInTrueState (server, conditionId, &fieldEnabledStateQN);
    UA_UNLOCK(&server->serviceMutex);
    return enabled;
}


static inline UA_Boolean
isBranch (UA_Server *server, const UA_NodeId *condition)
{
    return !isBranchIdMainCondition(server, condition);
}

static UA_Boolean
fieldExists (UA_Server *server, const UA_NodeId *condition, const UA_QualifiedName *field)
{
    UA_NodeId tmp;
    UA_NodeId_init (&tmp);
    UA_StatusCode status = getConditionFieldNodeId(server, condition, field, &tmp);
    UA_Boolean exists = status == UA_STATUSCODE_GOOD && !UA_NodeId_isNull(&tmp);
    UA_NodeId_clear (&tmp);
    return exists;
}

static inline UA_Boolean
isConfirmable (UA_Server *server, const UA_NodeId *condition)
{
   return fieldExists (server, condition, &fieldConfirmedStateQN);
}

static inline UA_StatusCode
setRetain (UA_Server *server, const UA_NodeId *condition, UA_Boolean retain)
{
    UA_Variant value;
    UA_Variant_setScalar(&value, &retain, &UA_TYPES[UA_TYPES_BOOLEAN]);
    UA_StatusCode retval = setConditionField (server, *condition, &value, fieldRetainQN);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "set Condition Retain failed",);
    return retval;
}

static UA_StatusCode
setTwoStateVariable (UA_Server *server, const UA_NodeId *condition,
                          UA_Boolean idValue, const char *state)
{
    /* Update Enabled State */
    UA_Variant value;
    UA_Variant_setScalar(&value, &idValue, &UA_TYPES[UA_TYPES_BOOLEAN]);
    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    retval = setConditionVariableFieldProperty(server, *condition, &value,
                                               fieldEnabledStateQN, twoStateVariableIdQN);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Setting State Id failed",);

    UA_LocalizedText stateText = UA_LOCALIZEDTEXT(LOCALE, (char *) (uintptr_t) state);
    UA_Variant_setScalar(&value, &stateText, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
    retval = setConditionField (server, *condition, &value, fieldEnabledStateQN);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "set State text failed",);
    return retval;
}

static inline UA_StatusCode
setComment (UA_Server *server, const UA_NodeId *condition, const UA_LocalizedText *comment)
{
    if (UA_String_equal(&comment->text, &UA_STRING_NULL)
        && UA_String_equal(&comment->locale, &UA_STRING_NULL)) return UA_STATUSCODE_GOOD;
    UA_Variant value;
    UA_Variant_setScalar(&value, (void *) (uintptr_t) comment, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    retval = setConditionField (server, *condition, &value, fieldCommentQN);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Setting Condition Comment failed",);
    return retval;
}

static inline UA_StatusCode
UA_Server_setComment (UA_Server *server, const UA_NodeId *condition, const UA_LocalizedText *comment)
{
    UA_LOCK (&server->serviceMutex);
    UA_StatusCode ret = setComment(server, condition, comment);
    UA_UNLOCK (&server->serviceMutex);
    return ret;
}

static inline UA_StatusCode
setMessage (UA_Server *server, const UA_NodeId *condition, const UA_LocalizedText *message)
{
    UA_Variant value;
    UA_Variant_setScalar(&value, (void*)(uintptr_t) message, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    retval = setConditionField (server, *condition, &value, fieldMessageQN);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Setting Condition Message failed",);
    return retval;
}

static inline UA_StatusCode
UA_Server_setMessage (UA_Server *server, const UA_NodeId *condition, const UA_LocalizedText *message)
{
    UA_LOCK (&server->serviceMutex);
    UA_StatusCode ret = setComment(server, condition, message);
    UA_UNLOCK (&server->serviceMutex);
    return ret;
}

static inline UA_StatusCode
setEnabledState (UA_Server *server, const UA_NodeId *condition, UA_Boolean enabled)
{
    return setTwoStateVariable (
        server, condition, enabled, enabled ? ENABLED_TEXT : DISABLED_TEXT
    );
}

static inline UA_StatusCode
setAckedState (UA_Server *server, const UA_NodeId *condition, UA_Boolean acked)
{
    return setTwoStateVariable (
        server, condition, acked, acked ? ACKED_TEXT : UNACKED_TEXT
    );
}


static inline UA_StatusCode
setConfirmedState (UA_Server *server, const UA_NodeId *condition, UA_Boolean confirmed)
{
    return setTwoStateVariable (
        server, condition, confirmed, confirmed? CONFIRMED_TEXT : UNCONFIRMED_TEXT
    );
}

static UA_Condition *
getConditionFromConditionBranchId (UA_Server *server, const UA_NodeId *sourceId, const UA_NodeId *conditionBranchId)
{
    UA_LOCK_ASSERT(&server->serviceMutex, 1);
    if (isBranchIdMainCondition(server, conditionBranchId))
    {
        return getCondition(server, sourceId, conditionBranchId);
    }

    /* search through all branches */
    UA_ConditionSource *source = getConditionSource(server, sourceId);
    if (!source) return NULL;

    UA_Condition *cond = NULL;
    UA_Condition *foundCond = NULL;
    UA_ConditionBranch *branch = NULL;
    LIST_FOREACH (cond, &source->conditions, listEntry)
    {
        LIST_FOREACH (branch, &cond->conditionBranches, listEntry)
        {
            if (UA_NodeId_equal (conditionBranchId, &branch->conditionBranchId))
            {
                foundCond = cond;
                break;
            }
        }
    }
    return foundCond;
}

/* Function used to set a user specific callback to TwoStateVariable Fields of a
 * condition. The callbacks will be called before triggering the events when
 * transition to true State of EnabledState/Id, AckedState/Id, ConfirmedState/Id
 * and ActiveState/Id occurs.
 * @param removeBranch is not used for the first implementation */
UA_StatusCode
UA_Server_setConditionTwoStateVariableCallback(UA_Server *server, const UA_NodeId condition,
                                               const UA_NodeId conditionSource, UA_Boolean removeBranch,
                                               UA_TwoStateVariableChangeCallback callback,
                                               UA_TwoStateVariableCallbackType callbackType) {
    UA_LOCK(&server->serviceMutex);

    /* Get Condition */
    UA_Condition *c = getCondition(server, &conditionSource, &condition);
    if(!c) {
        UA_UNLOCK(&server->serviceMutex);
        return UA_STATUSCODE_BADNOTFOUND;
    }

    /* Set the callback */
    switch(callbackType) {
    case UA_ENTERING_ENABLEDSTATE:
        c->callbacks.enableStateCallback = callback;
        break;
    case UA_ENTERING_ACKEDSTATE:
        c->callbacks.ackStateCallback = callback;
        c->callbacks.ackedRemoveBranch = removeBranch;
        break;
    case UA_ENTERING_CONFIRMEDSTATE:
        c->callbacks.confirmStateCallback = callback;
        c->callbacks.confirmedRemoveBranch = removeBranch;
        break;
    case UA_ENTERING_ACTIVESTATE:
        c->callbacks.activeStateCallback = callback;
        break;
    default:
        UA_UNLOCK(&server->serviceMutex);
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    UA_UNLOCK(&server->serviceMutex);
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
getConditionTwoStateVariableCallback(UA_Server *server, const UA_NodeId *branch,
                                     UA_Condition *condition, UA_Boolean *removeBranch,
                                     UA_TwoStateVariableCallbackType callbackType) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);
    UA_StatusCode res = UA_STATUSCODE_GOOD;

    /* That callbacks are defined in the userland. Release the server lock before. */
    UA_UNLOCK(&server->serviceMutex);

    /* TODO log warning when the callback wasn't set */
    switch(callbackType) {
    case UA_ENTERING_ENABLEDSTATE:
        if(condition->callbacks.enableStateCallback)
            res = condition->callbacks.enableStateCallback(server, branch);
        break;

    case UA_ENTERING_ACKEDSTATE:
        if(condition->callbacks.ackStateCallback) {
            *removeBranch = condition->callbacks.ackedRemoveBranch;
            res = condition->callbacks.ackStateCallback(server, branch);
        }
        break;

    case UA_ENTERING_CONFIRMEDSTATE:
        if(condition->callbacks.confirmStateCallback) {
            *removeBranch = condition->callbacks.confirmedRemoveBranch;
            res = condition->callbacks.confirmStateCallback(server, branch);
        }
        break;

    case UA_ENTERING_ACTIVESTATE:
        if(condition->callbacks.activeStateCallback)
            res = condition->callbacks.activeStateCallback(server, branch);
        break;

    default:
        res = UA_STATUSCODE_BADNOTFOUND;
        break;
    }
    UA_LOCK(&server->serviceMutex);

    return res;
}

static UA_StatusCode
callConditionTwoStateVariableCallback(UA_Server *server, const UA_NodeId *condition,
                                      const UA_NodeId *conditionSource, UA_Boolean *removeBranch,
                                      UA_TwoStateVariableCallbackType callbackType) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    UA_ConditionSource *source = getConditionSource(server, conditionSource);
    if(!source)
        return UA_STATUSCODE_BADNOTFOUND;

    UA_Condition *cond;
    LIST_FOREACH(cond, &source->conditions, listEntry) {
        if(UA_NodeId_equal(&cond->conditionId, condition)) {
            return getConditionTwoStateVariableCallback(server, condition, cond,
                                                        removeBranch, callbackType);
        }
        UA_ConditionBranch *branch;
        LIST_FOREACH(branch, &cond->conditionBranches, listEntry) {
            if(!UA_NodeId_equal(&branch->conditionBranchId, condition))
                continue;
            return getConditionTwoStateVariableCallback(server, &branch->conditionBranchId,
                                                        cond, removeBranch, callbackType);
        }
    }
    return UA_STATUSCODE_BADNOTFOUND;
}

static UA_StatusCode
UA_Server_callConditionTwoStateVariableCallback(UA_Server *server, const UA_NodeId *condition,
                                                const UA_NodeId *conditionSource,
                                                UA_Boolean *removeBranch,
                                                UA_TwoStateVariableCallbackType callbackType) {
    UA_LOCK(&server->serviceMutex);
    UA_StatusCode res = callConditionTwoStateVariableCallback(server, condition, conditionSource,
                                                              removeBranch, callbackType);
    UA_UNLOCK(&server->serviceMutex);
    return res;
}

static void *
copyFieldParent(void *context, UA_ReferenceTarget *t) {
    UA_NodeId *parent = (UA_NodeId*)context;
    if(!UA_NodePointer_isLocal(t->targetId))
        return NULL;
    UA_NodeId tmpNodeId = UA_NodePointer_toNodeId(t->targetId);
    UA_StatusCode res = UA_NodeId_copy(&tmpNodeId, parent);
    return (res == UA_STATUSCODE_GOOD) ? (void*)0x1 : NULL;
}

/* Gets the parent NodeId of a Field (e.g. Severity) or Field Property (e.g.
 * EnabledState/Id) */
static UA_StatusCode
getFieldParentNodeId(UA_Server *server, const UA_NodeId *field, UA_NodeId *parent) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    *parent = UA_NODEID_NULL;
    const UA_Node *fieldNode = UA_NODESTORE_GET(server, field);
    if(!fieldNode)
        return UA_STATUSCODE_BADNOTFOUND;
    for(size_t i = 0; i < fieldNode->head.referencesSize; i++) {
        UA_NodeReferenceKind *rk = &fieldNode->head.references[i];
        if(rk->referenceTypeIndex != UA_REFERENCETYPEINDEX_HASPROPERTY &&
           rk->referenceTypeIndex != UA_REFERENCETYPEINDEX_HASCOMPONENT)
            continue;
        if(!rk->isInverse)
            continue;
        /* Take the first hierarchical inverse reference */
        void *success = UA_NodeReferenceKind_iterate(rk, copyFieldParent, parent);
        if(success) {
            UA_NODESTORE_RELEASE(server, (const UA_Node *)fieldNode);
            return UA_STATUSCODE_GOOD;
        }
    }
    UA_NODESTORE_RELEASE(server, (const UA_Node *)fieldNode);
    return UA_STATUSCODE_BADNOTFOUND;
}

static UA_StatusCode
UA_Server_getFieldParentNodeId(UA_Server *server, const UA_NodeId *field, UA_NodeId *parent) {
    UA_LOCK(&server->serviceMutex);
    UA_StatusCode res = getFieldParentNodeId(server, field, parent);
    UA_UNLOCK(&server->serviceMutex);
    return res;
}

/* Gets the NodeId of a Field (e.g. Severity) */
static UA_StatusCode
getConditionFieldNodeId(UA_Server *server, const UA_NodeId *conditionNodeId,
                        const UA_QualifiedName* fieldName, UA_NodeId *outFieldNodeId) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    UA_BrowsePathResult bpr =
        browseSimplifiedBrowsePath(server, *conditionNodeId, 1, fieldName);
    if(bpr.statusCode != UA_STATUSCODE_GOOD)
        return bpr.statusCode;
    UA_StatusCode retval = UA_NodeId_copy(&bpr.targets[0].targetId.nodeId, outFieldNodeId);
    UA_BrowsePathResult_clear(&bpr);
    return retval;
}

/* Gets the NodeId of a Field Property (e.g. EnabledState/Id) */
static UA_StatusCode
getConditionFieldPropertyNodeId(UA_Server *server, const UA_NodeId *originCondition,
                                const UA_QualifiedName* variableFieldName,
                                const UA_QualifiedName* variablePropertyName,
                                UA_NodeId *outFieldPropertyNodeId) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    /* 1) Find Variable Field of the Condition */
    UA_BrowsePathResult bprConditionVariableField =
        browseSimplifiedBrowsePath(server, *originCondition, 1, variableFieldName);
    if(bprConditionVariableField.statusCode != UA_STATUSCODE_GOOD)
        return bprConditionVariableField.statusCode;

    /* 2) Find Property of the Variable Field of the Condition */
    UA_BrowsePathResult bprVariableFieldProperty =
        browseSimplifiedBrowsePath(server, bprConditionVariableField.targets->targetId.nodeId,
                                   1, variablePropertyName);
    if(bprVariableFieldProperty.statusCode != UA_STATUSCODE_GOOD) {
        UA_BrowsePathResult_clear(&bprConditionVariableField);
        return bprVariableFieldProperty.statusCode;
    }

    *outFieldPropertyNodeId = bprVariableFieldProperty.targets[0].targetId.nodeId;
    UA_NodeId_init(&bprVariableFieldProperty.targets[0].targetId.nodeId);
    UA_BrowsePathResult_clear(&bprConditionVariableField);
    UA_BrowsePathResult_clear(&bprVariableFieldProperty);
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
getValueOfConditionField (UA_Server *server, const UA_NodeId *condition,
                         UA_QualifiedName fieldName, UA_Variant *outValue)
{
    UA_LOCK_ASSERT(&server->serviceMutex, 1);
    UA_Variant_init (outValue);
    UA_NodeId fieldId;
    UA_StatusCode retval = getConditionFieldNodeId(server, condition, &fieldName, &fieldId);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Field not found",);
    retval = readWithReadValue(server, &fieldId, UA_ATTRIBUTEID_VALUE, outValue);
    UA_NodeId_clear(&fieldId);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Field not found",);
    return retval;
}

static UA_StatusCode
getNodeIdValueOfConditionField(UA_Server *server, const UA_NodeId *condition,
                               UA_QualifiedName fieldName, UA_NodeId *outNodeId) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);
    /* Read the Value of SourceNode Property Node (the Value is a NodeId) */
    UA_Variant value;
    UA_StatusCode retval = getValueOfConditionField (server, condition, fieldName, &value);
    if(retval != UA_STATUSCODE_GOOD) return retval;
    if (!UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_NODEID]))
    {
        UA_Variant_clear(&value);
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }
    *outNodeId = *(UA_NodeId*)value.data;
    UA_NodeId_init((UA_NodeId*)value.data);
    UA_Variant_clear(&value);
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
getByteStringValueOfConditionField(UA_Server *server, const UA_NodeId *condition,
                                UA_QualifiedName fieldName, UA_ByteString*outValue) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);
    /* Read the Value of SourceNode Property Node (the Value is a NodeId) */
    UA_Variant value;
    UA_StatusCode retval = getValueOfConditionField(server, condition, fieldName, &value);
    if(retval != UA_STATUSCODE_GOOD) return retval;
    if(!UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BYTESTRING])) {
        UA_Variant_clear(&value);
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }
    *outValue = *(UA_ByteString*)value.data;
    UA_ByteString_init((UA_ByteString*)value.data);
    UA_Variant_clear(&value);
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
UA_Server_getNodeIdValueOfConditionField(UA_Server *server, const UA_NodeId *condition,
                                         UA_QualifiedName fieldName, UA_NodeId *outNodeId) {
    UA_LOCK(&server->serviceMutex);
    UA_StatusCode res = getNodeIdValueOfConditionField(server, condition, fieldName, outNodeId);
    UA_UNLOCK(&server->serviceMutex);
    return res;
}

static UA_StatusCode
getBooleanValueOfConditionField(UA_Server *server, const UA_NodeId *condition,
                               UA_QualifiedName fieldName, UA_Boolean*outValue) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);
    /* Read the Value of SourceNode Property Node (the Value is a NodeId) */
    UA_Variant value;
    UA_StatusCode retval = getValueOfConditionField(server, condition, fieldName, &value);
    if(retval != UA_STATUSCODE_GOOD) return retval;
    if(!UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_Variant_clear(&value);
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }
    *outValue = *(UA_Boolean*)value.data;
    UA_Boolean_init((UA_Boolean*)value.data);
    UA_Variant_clear(&value);
    return UA_STATUSCODE_GOOD;
}



static UA_Boolean
isRetained(UA_Server *server, const UA_NodeId *condition) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);
    UA_Boolean value = false;
    UA_StatusCode retval = getBooleanValueOfConditionField(server, condition, fieldRetainQN, &value);
    if(retval != UA_STATUSCODE_GOOD) {
        UA_LOG_WARNING(server->config.logging, UA_LOGCATEGORY_USERLAND,
                       "Retain not found. StatusCode %s", UA_StatusCode_name(retval));
        return false; //TODO maybe a better error handling?
    }
    return value;
}

static UA_Boolean
UA_Server_isRetained(UA_Server *server, const UA_NodeId *condition) {
    UA_LOCK(&server->serviceMutex);
    UA_Boolean res = isRetained(server, condition);
    UA_UNLOCK(&server->serviceMutex);
    return res;
}

/* Gets the NodeId of a condition branch. In case of main branch (BranchId ==
 * UA_NODEID_NULL), ConditionId will be returned. */
static UA_StatusCode
UA_Server_getConditionBranchNodeId(
    UA_Server *server,
    const UA_NodeId *conditionId,
    const UA_ByteString *eventId,
    UA_NodeId *outConditionBranchNodeId
) {
    UA_LOCK(&server->serviceMutex);

    *outConditionBranchNodeId = UA_NODEID_NULL;

    UA_StatusCode res = UA_STATUSCODE_BADEVENTIDUNKNOWN;
    UA_ConditionSource *source;
    UA_Condition *cond = NULL;
    UA_Condition *foundCond = NULL;

    /* Find condition */
    LIST_FOREACH(source, &server->conditionSources, listEntry) {
        LIST_FOREACH(cond, &source->conditions, listEntry) {
            if (UA_NodeId_equal (conditionId, &cond->conditionId))
            {
                foundCond = cond;
                break;
            }
        }
    }
    if (!foundCond)
    {
        res = UA_STATUSCODE_BADNODEIDINVALID;
        goto out;
    }
    /* find ConditionBranch */
    UA_ConditionBranch *branch = NULL;
    LIST_FOREACH (branch, &foundCond->conditionBranches, listEntry)
    {
        if(!UA_ByteString_equal(&branch->lastEventId, eventId)) continue;
        if(UA_NodeId_isNull(&branch->conditionBranchId)) {
            res = UA_NodeId_copy(&foundCond->conditionId, outConditionBranchNodeId);
            UA_UNLOCK(&server->serviceMutex);
            return res;
        } else {
            res = UA_NodeId_copy(&branch->conditionBranchId, outConditionBranchNodeId);
            UA_UNLOCK(&server->serviceMutex);
            return res;
        }
        goto out;
    }

 out:
    UA_UNLOCK(&server->serviceMutex);
    return res;
}


static UA_StatusCode
updateConditionLastEventId(UA_Server *server, const UA_NodeId *conditionId,
                           const UA_NodeId *conditionSource,
                           const UA_ByteString *lastEventId) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    UA_Condition *cond = getCondition(server, conditionSource, conditionId);
    if(!cond) {
        UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,
                     "Entry not found in list!");
        return UA_STATUSCODE_BADNOTFOUND;
    }

    UA_ConditionBranch *branch;
    LIST_FOREACH(branch, &cond->conditionBranches, listEntry) {
        if(UA_NodeId_isNull(&branch->conditionBranchId)) {
            /* update main condition branch */
            UA_ByteString_clear(&branch->lastEventId);
            return UA_ByteString_copy(lastEventId, &branch->lastEventId);
        }
    }
    return UA_STATUSCODE_BADNOTFOUND;
}

static void
setIsCallerAC(UA_Server *server, const UA_NodeId *condition,
              const UA_NodeId *conditionSource, UA_Boolean isCallerAC) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    UA_Condition *cond = getCondition(server, conditionSource, condition);
    if(!cond) {
        UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,
                     "Entry not found in list!");
        return;
    }

    UA_ConditionBranch *branch;
    LIST_FOREACH(branch, &cond->conditionBranches, listEntry) {
        if(UA_NodeId_isNull(&branch->conditionBranchId)) {
            branch->isCallerAC = isCallerAC;
            return;
        }
    }
    UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,
                 "Condition Branch not implemented");
}

UA_Boolean
isConditionOrBranch(UA_Server *server, const UA_NodeId *condition,
                    const UA_NodeId *conditionSource, UA_Boolean *isCallerAC) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    UA_Condition *cond = getCondition(server, conditionSource, condition);
    if(!cond) {
        UA_LOG_DEBUG(server->config.logging, UA_LOGCATEGORY_USERLAND,
                     "Entry not found in list!");
        return false;
    }

    UA_ConditionBranch *branch;
    LIST_FOREACH(branch, &cond->conditionBranches, listEntry) {
        if(UA_NodeId_isNull(&branch->conditionBranchId)) {
            *isCallerAC = branch->isCallerAC;
            return true;
        }
    }
    UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,
                 "Condition Branch not implemented");
    return false;
}



static UA_StatusCode
setRefreshMethodEventFields(UA_Server *server, const UA_NodeId *refreshEventNodId) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    UA_QualifiedName fieldSeverity = UA_QUALIFIEDNAME(0, CONDITION_FIELD_SEVERITY);
    UA_QualifiedName fieldSourceName = UA_QUALIFIEDNAME(0, CONDITION_FIELD_SOURCENAME);
    UA_QualifiedName fieldReceiveTime = UA_QUALIFIEDNAME(0, CONDITION_FIELD_RECEIVETIME);
    UA_String sourceNameString = UA_STRING("Server"); //server is the source of Refresh Events
    UA_UInt16 severityValue = REFRESHEVENT_SEVERITY_DEFAULT;
    UA_ByteString eventId  = UA_BYTESTRING_NULL;
    UA_Variant value;

    /* Set Severity */
    UA_Variant_setScalar(&value, &severityValue, &UA_TYPES[UA_TYPES_UINT16]);
    UA_StatusCode retval = setConditionField(server, *refreshEventNodId,
                                             &value, fieldSeverity);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set RefreshEvent Severity failed",);

    /* Set SourceName */
    UA_Variant_setScalar(&value, &sourceNameString, &UA_TYPES[UA_TYPES_STRING]);
    retval = setConditionField(server, *refreshEventNodId, &value, fieldSourceName);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set RefreshEvent Source failed",);

    /* Set ReceiveTime */
    UA_DateTime fieldReceiveTimeValue = UA_DateTime_now();
    UA_Variant_setScalar(&value, &fieldReceiveTimeValue, &UA_TYPES[UA_TYPES_DATETIME]);
    retval = setConditionField(server, *refreshEventNodId, &value, fieldReceiveTime);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set RefreshEvent ReceiveTime failed",);

    /* Set EventId */
    retval = generateEventId(&eventId);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Generating EventId failed",);

    UA_Variant_setScalar(&value, &eventId, &UA_TYPES[UA_TYPES_BYTESTRING]);
    retval = setConditionField(server, *refreshEventNodId, &value, fieldEventIdQN);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set RefreshEvent EventId failed",);

    UA_ByteString_clear(&eventId);

    return retval;
}

static UA_StatusCode
setRefreshMethodEvents(UA_Server *server, const UA_NodeId *refreshStartNodId,
                       const UA_NodeId *refreshEndNodId) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    /* Set Standard Fields for RefreshStart */
    UA_StatusCode retval = setRefreshMethodEventFields(server, refreshStartNodId);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set standard Fields of RefreshStartEvent failed",);

    /* Set Standard Fields for RefreshEnd*/
    retval = setRefreshMethodEventFields(server, refreshEndNodId);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set standard Fields of RefreshEndEvent failed",);
    return retval;
}

static UA_Boolean
isConditionSourceInMonitoredItem(UA_Server *server, const UA_MonitoredItem *monitoredItem,
                                 const UA_NodeId *conditionSource){
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    /* TODO: check also other hierarchical references */
    UA_ReferenceTypeSet refs = UA_REFTYPESET(UA_REFERENCETYPEINDEX_ORGANIZES);
    refs = UA_ReferenceTypeSet_union(refs, UA_REFTYPESET(UA_REFERENCETYPEINDEX_HASCOMPONENT));
    refs = UA_ReferenceTypeSet_union(refs, UA_REFTYPESET(UA_REFERENCETYPEINDEX_HASEVENTSOURCE));
    refs = UA_ReferenceTypeSet_union(refs, UA_REFTYPESET(UA_REFERENCETYPEINDEX_HASNOTIFIER));
    return isNodeInTree(server, conditionSource, &monitoredItem->itemToMonitor.nodeId, &refs);
}


/*****************************************************************************/
/* Functions                                                                 */
/*****************************************************************************/

static UA_StatusCode
setConditionBranchInConditionList (UA_Server *server, const UA_NodeId *sourceId, const UA_NodeId *originConditionBranch,
                                   const UA_NodeId *branchId, const UA_ByteString *branchlastEventId)
{
    UA_Condition *condition = getConditionFromConditionBranchId (server, sourceId, originConditionBranch);
    if (!condition) return UA_STATUSCODE_BADNOTFOUND;

    UA_ConditionBranch *conditionBranch = (UA_ConditionBranch*) UA_malloc(sizeof(*conditionBranch));
    if(!conditionBranch) {
        return UA_STATUSCODE_BADOUTOFMEMORY;
    }
    memset(conditionBranch, 0, sizeof(*conditionBranch));

    UA_StatusCode retval = UA_NodeId_copy(branchId, &conditionBranch->conditionBranchId);
    if(retval != UA_STATUSCODE_GOOD) goto fail;
    retval = UA_ByteString_copy (branchlastEventId, &conditionBranch->lastEventId);
    if(retval != UA_STATUSCODE_GOOD) goto fail;
    LIST_INSERT_HEAD(&condition->conditionBranches, conditionBranch, listEntry);
    return UA_STATUSCODE_GOOD;
fail:
    UA_NodeId_clear(&conditionBranch->conditionBranchId);
    UA_ByteString_clear(&conditionBranch->lastEventId);
    UA_free(conditionBranch);
    return retval;
}

static UA_StatusCode
setConditionInConditionList(UA_Server *server, const UA_NodeId *conditionNodeId,
                            UA_ConditionSource *conditionSourceEntry) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    UA_Condition *conditionListEntry = (UA_Condition*)UA_malloc(sizeof(UA_Condition));
    if(!conditionListEntry)
        return UA_STATUSCODE_BADOUTOFMEMORY;
    memset(conditionListEntry, 0, sizeof(UA_Condition));

    /* Set ConditionId with given ConditionNodeId */
    UA_StatusCode retval = UA_NodeId_copy(conditionNodeId, &conditionListEntry->conditionId);
    if(retval != UA_STATUSCODE_GOOD) {
        UA_free(conditionListEntry);
        return retval;
    }

    UA_ConditionBranch *conditionBranchListEntry;
    conditionBranchListEntry = (UA_ConditionBranch*)UA_malloc(sizeof(UA_ConditionBranch));
    if(!conditionBranchListEntry) {
        UA_free(conditionListEntry);
        return UA_STATUSCODE_BADOUTOFMEMORY;
    }
    memset(conditionBranchListEntry, 0, sizeof(UA_ConditionBranch));

    LIST_INSERT_HEAD(&conditionSourceEntry->conditions, conditionListEntry, listEntry);
    LIST_INSERT_HEAD(&conditionListEntry->conditionBranches, conditionBranchListEntry, listEntry);
    return UA_STATUSCODE_GOOD;
}



static UA_StatusCode
appendConditionEntry(UA_Server *server, const UA_NodeId *conditionNodeId,
                     const UA_NodeId *conditionSourceNodeId) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    /* See if the ConditionSource Entry already exists*/
    UA_ConditionSource *source = getConditionSource (server, conditionSourceNodeId);
    if(source)
        return setConditionInConditionList(server, conditionNodeId, source);

    /* ConditionSource not found in list, so we create a new ConditionSource Entry */
    UA_ConditionSource *conditionSourceListEntry;
    conditionSourceListEntry = (UA_ConditionSource*)UA_malloc(sizeof(UA_ConditionSource));
    if(!conditionSourceListEntry)
        return UA_STATUSCODE_BADOUTOFMEMORY;
    memset(conditionSourceListEntry, 0, sizeof(UA_ConditionSource));

    /* Set ConditionSourceId with given ConditionSourceNodeId */
    UA_StatusCode retval = UA_NodeId_copy(conditionSourceNodeId,
                                          &conditionSourceListEntry->conditionSourceId);
    if(retval != UA_STATUSCODE_GOOD) {
        UA_free(conditionSourceListEntry);
        return retval;
    }

    LIST_INSERT_HEAD(&server->conditionSources, conditionSourceListEntry, listEntry);
    return setConditionInConditionList(server, conditionNodeId, conditionSourceListEntry);
}


static void
deleteAllBranchesFromCondition(UA_Condition *cond) {
    UA_ConditionBranch *branch, *tmp_branch;
    LIST_FOREACH_SAFE(branch, &cond->conditionBranches, listEntry, tmp_branch) {
        UA_NodeId_clear(&branch->conditionBranchId);
        UA_ByteString_clear(&branch->lastEventId);
        LIST_REMOVE(branch, listEntry);
        UA_free(branch);
    }
}

static void
deleteCondition(UA_Condition *cond) {
    deleteAllBranchesFromCondition(cond);
    UA_NodeId_clear(&cond->conditionId);
    LIST_REMOVE(cond, listEntry);
    UA_free(cond);
}

void
UA_ConditionList_delete(UA_Server *server) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    UA_ConditionSource *source, *tmp_source;
    LIST_FOREACH_SAFE(source, &server->conditionSources, listEntry, tmp_source) {
        UA_Condition *cond, *tmp_cond;
        LIST_FOREACH_SAFE(cond, &source->conditions, listEntry, tmp_cond) {
            deleteCondition(cond);
        }
        UA_NodeId_clear(&source->conditionSourceId);
        LIST_REMOVE(source, listEntry);
        UA_free(source);
    }
    /* Free memory allocated for RefreshEvents NodeIds */
    UA_NodeId_clear(&server->refreshEvents[REFRESHEVENT_START_IDX]);
    UA_NodeId_clear(&server->refreshEvents[REFRESHEVENT_END_IDX]);
}

/* Get the ConditionId based on the EventId (all branches of one condition
 * should have the same ConditionId) */
UA_StatusCode
UA_getConditionId(UA_Server *server, const UA_NodeId *conditionNodeId,
                  UA_NodeId *outConditionId) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    /* Get ConditionSource Entry */
    UA_ConditionSource *source;
    LIST_FOREACH(source, &server->conditionSources, listEntry) {
        /* Get Condition Entry */
        UA_Condition *cond;
        LIST_FOREACH(cond, &source->conditions, listEntry) {
            if(UA_NodeId_equal(&cond->conditionId, conditionNodeId)) {
                *outConditionId = cond->conditionId;
                return UA_STATUSCODE_GOOD;
            }
            /* Get Branch Entry*/
            UA_ConditionBranch *branch;
            LIST_FOREACH(branch, &cond->conditionBranches, listEntry) {
                if(UA_NodeId_equal(&branch->conditionBranchId, conditionNodeId)) {
                    *outConditionId = cond->conditionId;
                    return UA_STATUSCODE_GOOD;
                }
            }
        }
    }
    return UA_STATUSCODE_BADNOTFOUND;
}

/* Check whether the Condition Source Node has "EventSource" or one of its
 * subtypes inverse reference. */
static UA_Boolean
doesHasEventSourceReferenceExist(UA_Server *server, const UA_NodeId nodeToCheck) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    UA_NodeId hasEventSourceId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASEVENTSOURCE);
    const UA_Node* node = UA_NODESTORE_GET(server, &nodeToCheck);
    if(!node)
        return false;
    for(size_t i = 0; i < node->head.referencesSize; i++) {
        UA_Byte refTypeIndex = node->head.references[i].referenceTypeIndex;
        if((refTypeIndex == UA_REFERENCETYPEINDEX_HASEVENTSOURCE ||
            isNodeInTree_singleRef(server, UA_NODESTORE_GETREFERENCETYPEID(server, refTypeIndex),
                                   &hasEventSourceId, UA_REFERENCETYPEINDEX_HASSUBTYPE)) &&
           (node->head.references[i].isInverse == true)) {
            UA_NODESTORE_RELEASE(server, node);
            return true;
        }
    }
    UA_NODESTORE_RELEASE(server, node);
    return false;
}

struct UA_SetupConditionContext
{
    UA_Server *server;
    const UA_NodeId *conditionInstance;
    const void* conditionSetupProperties;
};

static UA_StatusCode recurseConditionTypeInstanceSetup (UA_Server *server, const struct UA_SetupConditionContext *ctx, const UA_Node *conditionTypeNode);

static void *
setupConditionInstanceSubtypeOfCallback(void *context, UA_ReferenceTarget *t) {
    if (!UA_NodePointer_isLocal(t->targetId)) return NULL;
    struct UA_SetupConditionContext *ctx = (struct UA_SetupConditionContext *) context;
    const UA_Node *node = UA_NODESTORE_GETFROMREF (ctx->server, t->targetId);
    return recurseConditionTypeInstanceSetup (ctx->server, ctx, node) == UA_STATUSCODE_GOOD ? (void*)0x1 : NULL;
}

/**
 * Recurse upwards through the conditionType subtypes until we reach the base condition type.
 * Then call each of the conditions setup functions in order
 */
static UA_StatusCode recurseConditionTypeInstanceSetup (UA_Server *server, const struct UA_SetupConditionContext *ctx, const UA_Node *conditionTypeNode)
{
    if (!conditionTypeNode) return UA_STATUSCODE_BADINTERNALERROR;
    if (conditionTypeNode->head.nodeClass != UA_NODECLASS_OBJECTTYPE) return UA_STATUSCODE_BADINTERNALERROR;
    const UA_NodeId conditionType = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    if (UA_NodeId_equal(&conditionTypeNode->head.nodeId, &conditionType)) return UA_STATUSCODE_GOOD;
    for (size_t i = 0; i < conditionTypeNode->head.referencesSize; i++) {
        UA_NodeReferenceKind *rk = &conditionTypeNode->head.references[i];
        UA_Boolean isSubtypeOf = rk->referenceTypeIndex == UA_REFERENCETYPEINDEX_HASSUBTYPE && rk->isInverse == true;
        if(!isSubtypeOf) continue;

        const void *success = UA_NodeReferenceKind_iterate (rk, setupConditionInstanceSubtypeOfCallback, (void *) (uintptr_t) ctx);
        if (!success) return UA_STATUSCODE_BAD;
    }
    UA_ConditionTypeSetupNodesFn setupFn = ((const UA_ObjectTypeNode *) conditionTypeNode)->_conditionNodeSetupFn;
    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    if (!setupFn) return retval;
    //call metadata constructor methods
    return setupFn (server, ctx->conditionInstance, ctx->conditionSetupProperties);
}


static UA_StatusCode
setupConditionInstance (UA_Server *server, const UA_NodeId *conditionId, const UA_NodeId *conditionType, const UA_ConditionProperties *properties, const void *conditionSetupProperties)
{
    UA_StatusCode retval = setupConditionNodes (server, conditionId, conditionType, properties);
    if (retval != UA_STATUSCODE_GOOD) return retval;
    const UA_Node* node = UA_NODESTORE_GET (server, conditionType);
    struct UA_SetupConditionContext ctx = {
        .server = server,
        .conditionInstance = conditionId,
        .conditionSetupProperties = conditionSetupProperties
    };
    return recurseConditionTypeInstanceSetup (server, &ctx, node);
}

static UA_StatusCode
addCondition_finish(
    UA_Server *server,
    const UA_NodeId conditionId,
    const UA_NodeId *conditionType,
    const UA_ConditionProperties *conditionProperties,
    const void *conditionSetupProperties
) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    UA_StatusCode retval = addNode_finish(server, &server->adminSession, &conditionId);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Finish node failed",);

    /* Make sure the ConditionSource has HasEventSource or one of its SubTypes ReferenceType */
    UA_NodeId serverObject = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    if(!doesHasEventSourceReferenceExist(server, conditionProperties->source) &&
       !UA_NodeId_equal(&serverObject, &conditionProperties->source)) {
         UA_NodeId hasHasEventSourceId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASEVENTSOURCE);
         retval = addRef(server, serverObject, hasHasEventSourceId, conditionProperties->source, true);
          CONDITION_ASSERT_RETURN_RETVAL(retval, "Creating HasHasEventSource Reference "
                                         "to the Server Object failed",);
    }

    /* create HasCondition Reference (HasCondition should be forward from the
     * ConditionSourceNode to the Condition. else, HasCondition should be
     * forward from the ConditionSourceNode to the ConditionType Node) */
    UA_NodeId hasCondition = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCONDITION);
    if(!UA_NodeId_isNull(&conditionProperties->hierarchialReferenceType)) {
        /* Create hierarchical Reference to ConditionSource to expose the
         * ConditionNode in Address Space */
        // only Check hierarchialReferenceType
        retval = addRef(server, conditionProperties->source, conditionProperties->hierarchialReferenceType, conditionId, true);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Creating hierarchical Reference to "
                                       "ConditionSource failed",);

        retval = addRef(server, conditionProperties->source, hasCondition, conditionId, true);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Creating HasCondition Reference failed",);
    } else {
        retval = addRef(server, conditionProperties->source, hasCondition, *conditionType, true);
        if(retval != UA_STATUSCODE_BADDUPLICATEREFERENCENOTALLOWED)
            CONDITION_ASSERT_RETURN_RETVAL(retval, "Creating HasCondition Reference failed",);
    }

    retval = setupConditionInstance (server, &conditionId, conditionType, conditionProperties, conditionSetupProperties);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Setup Condition failed",);

    /* append Condition to list */
    return appendConditionEntry(server, &conditionId, &conditionProperties->source);
}

static UA_StatusCode
addCondition_begin(UA_Server *server, const UA_NodeId conditionId,
                   const UA_NodeId conditionType,
                   const UA_QualifiedName conditionName, UA_NodeId *outNodeId) {
    if(!outNodeId) {
        UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,
                     "outNodeId cannot be NULL!");
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    }

    /* Make sure the conditionType is a Subtype of ConditionType */
    UA_NodeId conditionTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);
    UA_LOCK(&server->serviceMutex);
    UA_Boolean found = isNodeInTree_singleRef(server, &conditionType, &conditionTypeId,
                                              UA_REFERENCETYPEINDEX_HASSUBTYPE);
    UA_UNLOCK(&server->serviceMutex);
    if(!found) {
        UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,
                     "Condition Type must be a subtype of ConditionType!");
        return UA_STATUSCODE_BADNOMATCH;
    }

    /* Create an ObjectNode which represents the condition */
    UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
    oAttr.displayName.locale = UA_STRING("en");
    oAttr.displayName.text = conditionName.name;
    UA_StatusCode retval =
        UA_Server_addNode_begin(server, UA_NODECLASS_OBJECT, conditionId,
                                UA_NODEID_NULL, UA_NODEID_NULL, conditionName,
                                conditionType, &oAttr, &UA_TYPES[UA_TYPES_OBJECTATTRIBUTES],
                                NULL, outNodeId);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding Condition failed", );
    return UA_STATUSCODE_GOOD;
}

/* Create condition instance. The function checks first whether the passed
 * conditionType is a subType of ConditionType. Then checks whether the
 * condition source has HasEventSource reference to its parent. If not, a
 * HasEventSource reference will be created between condition source and server
 * object. To expose the condition in address space, a hierarchical
 * ReferenceType should be passed to create the reference to condition source.
 * Otherwise, UA_NODEID_NULL should be passed to make the condition unexposed. */
UA_StatusCode
UA_Server_createCondition(UA_Server *server,
                          const UA_NodeId conditionId,
                          const UA_NodeId conditionType,
                          const UA_ConditionProperties *conditionProperties,
                          const UA_ConditionTypeFunctionsTable *fns,
                          const void *conditionSetupProperties,
                          UA_NodeId *outNodeId) {
    if(!outNodeId) {
        UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,
                     "outNodeId cannot be NULL!");
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    }

    UA_StatusCode retval = addCondition_begin(server, conditionId, conditionType,
                                              conditionProperties->name, outNodeId);
    if(retval != UA_STATUSCODE_GOOD)
        return retval;

    UA_LOCK(&server->serviceMutex);
    retval = addCondition_finish(server, *outNodeId, &conditionType, conditionProperties,
                               conditionSetupProperties);
    UA_UNLOCK(&server->serviceMutex);
    return retval;
}

UA_StatusCode
UA_Server_deleteCondition(UA_Server *server, const UA_NodeId condition,
                          const UA_NodeId conditionSource) {
    /* Get ConditionSource Entry */
    UA_Boolean found = false; /* Delete from internal list */
    UA_ConditionSource *source, *tmp_source;

    UA_LOCK(&server->serviceMutex);
    LIST_FOREACH_SAFE(source, &server->conditionSources, listEntry, tmp_source) {
        if(!UA_NodeId_equal(&source->conditionSourceId, &conditionSource))
            continue;

        /* Get Condition Entry */
        UA_Condition *cond, *tmp_cond;
        LIST_FOREACH_SAFE(cond, &source->conditions, listEntry, tmp_cond) {
            if(!UA_NodeId_equal(&cond->conditionId, &condition))
                continue;
            deleteCondition(cond);
            found = true;
            break;
        }

        if(LIST_EMPTY(&source->conditions)){
            UA_NodeId_clear(&source->conditionSourceId);
            LIST_REMOVE(source, listEntry);
            UA_free(source);
        }
        break;
    }
    UA_UNLOCK(&server->serviceMutex);

    if(!found)
        return UA_STATUSCODE_BADNOTFOUND;

    /* Delete from address space */
    return UA_Server_deleteNode(server, condition, true);
}

static UA_StatusCode createConditionBranch (UA_Server *server, const UA_NodeId *originConditionBranchId)
{
    UA_NodeId sourceId;
    UA_NodeId_init (&sourceId);
    UA_ByteString lastEventId;
    UA_ByteString_init (&lastEventId);

    UA_NodeId conditionType = getTypeDefinitionId (server, originConditionBranchId);
    if (UA_NodeId_isNull(&conditionType)) return UA_STATUSCODE_BADINTERNALERROR;
    conditionType = UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE);

    UA_ObjectAttributes oa = UA_ObjectAttributes_default;
    oa.displayName = UA_LOCALIZEDTEXT("en", "ConditionBranch");
    const UA_QualifiedName qn = STATIC_QN("ConditionBranch");
    UA_NodeId branchId;
    UA_StatusCode retval = addNode_begin (server, UA_NODECLASS_OBJECT, UA_NODEID_NULL,
                                          UA_NODEID_NULL, UA_NODEID_NULL, qn, conditionType, &oa, &UA_TYPES[UA_TYPES_OBJECTATTRIBUTES],
                                          NULL,
                                          &branchId
    );
    CONDITION_ASSERT_GOTOLABEL(retval,"Adding ConditionBranch failed", fail);

    /* Copy the state of the condition */
    retval = copyAllChildren (server, &server->adminSession, originConditionBranchId, &branchId);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Copying Condition State failed", );

    /* Update branchId property */
    UA_Variant value;
    UA_Variant_setScalar(&value, (void*)(uintptr_t) &branchId, &UA_TYPES[UA_TYPES_NODEID]);
    retval = setConditionField(server, branchId, &value,
                               UA_QUALIFIEDNAME(0,CONDITION_FIELD_BRANCHID));
    CONDITION_ASSERT_GOTOLABEL(retval, "Set EventType Field failed", fail);

    //TODO DELETE - FOR TESTING expose it in the address space
    {
        UA_NodeId source = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
        retval = addRef(server, source, UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), branchId,
                        true);
        CONDITION_ASSERT_GOTOLABEL(retval,
                                  "Creating hierarchical Reference to "
                                  "ConditionSource failed", fail);

        UA_NodeId hasCondition = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCONDITION);
        retval = addRef(server, source, hasCondition, branchId, true);
        CONDITION_ASSERT_GOTOLABEL(retval,
                                  "Creating HasCondition Reference failed", fail);
    }

    retval = getByteStringValueOfConditionField(server, originConditionBranchId, fieldEventIdQN, &lastEventId);
    CONDITION_ASSERT_GOTOLABEL(retval, "Could not get sourceId of condition", fail);

    retval = getNodeIdValueOfConditionField (server, originConditionBranchId, fieldSourceQN, &sourceId);
    CONDITION_ASSERT_GOTOLABEL(retval, "Could not get sourceId of condition", fail);

    retval = setConditionBranchInConditionList (server, &sourceId, originConditionBranchId, &branchId, &lastEventId);
    UA_NodeId_clear(&sourceId);
    UA_ByteString_clear(&lastEventId);
    CONDITION_ASSERT_GOTOLABEL(retval, "Set ConditionBranch in list failed", fail);
    //create the internal condition branch
    return UA_STATUSCODE_GOOD;
fail:
    UA_NodeId_clear(&sourceId);
    UA_ByteString_clear(&lastEventId);
    deleteNode(server, branchId, true);
    return retval;
}


UA_StatusCode UA_Server_createConditionBranch (UA_Server *server, const UA_NodeId *conditionId)
{
    UA_LOCK(&server->serviceMutex);
    UA_StatusCode retval = createConditionBranch(server, conditionId);
    UA_UNLOCK(&server->serviceMutex);
    return retval;
}

static UA_StatusCode deleteConditionBranch (UA_Server *server, const UA_NodeId *branchId)
{
    //TODO
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
addOptionalVariableField(UA_Server *server, const UA_NodeId *originCondition,
                         const UA_QualifiedName *fieldName,
                         const UA_VariableNode *optionalVariableFieldNode,
                         UA_NodeId *outOptionalVariable) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    UA_VariableAttributes vAttr = UA_VariableAttributes_default;
    vAttr.valueRank = optionalVariableFieldNode->valueRank;
    vAttr.displayName = UA_Session_getNodeDisplayName(&server->adminSession,
                                                      &optionalVariableFieldNode->head);
    vAttr.dataType = optionalVariableFieldNode->dataType;

    /* Get typedefintion */
    const UA_Node *type = getNodeType(server, &optionalVariableFieldNode->head);
    if(!type) {
        UA_LOG_WARNING(server->config.logging, UA_LOGCATEGORY_USERLAND,
                       "Invalid VariableType. StatusCode %s",
                       UA_StatusCode_name(UA_STATUSCODE_BADTYPEDEFINITIONINVALID));
        return UA_STATUSCODE_BADTYPEDEFINITIONINVALID;
    }

    /* Set referenceType to parent */
    UA_NodeId referenceToParent;
    UA_NodeId propertyTypeNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE);
    if(UA_NodeId_equal(&type->head.nodeId, &propertyTypeNodeId))
        referenceToParent = UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY);
    else
        referenceToParent = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);

    /* Set a random unused NodeId with specified Namespace Index*/
    UA_NodeId optionalVariable = {originCondition->namespaceIndex, UA_NODEIDTYPE_NUMERIC, {0}};
    UA_StatusCode retval =
        addNode(server, UA_NODECLASS_VARIABLE, optionalVariable,
                *originCondition, referenceToParent, *fieldName,
                type->head.nodeId, &vAttr, &UA_TYPES[UA_TYPES_VARIABLEATTRIBUTES],
                NULL, outOptionalVariable);
    UA_NODESTORE_RELEASE(server, type);
    return retval;
}

static UA_StatusCode
addOptionalObjectField(UA_Server *server, const UA_NodeId *originCondition,
                       const UA_QualifiedName* fieldName,
                       const UA_ObjectNode *optionalObjectFieldNode,
                       UA_NodeId *outOptionalObject) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
    oAttr.displayName = UA_Session_getNodeDisplayName(&server->adminSession,
                                                      &optionalObjectFieldNode->head);

    /* Get typedefintion */
    const UA_Node *type = getNodeType(server, &optionalObjectFieldNode->head);
    if(!type) {
        UA_LOG_WARNING(server->config.logging, UA_LOGCATEGORY_USERLAND,
                       "Invalid ObjectType. StatusCode %s",
                       UA_StatusCode_name(UA_STATUSCODE_BADTYPEDEFINITIONINVALID));
        return UA_STATUSCODE_BADTYPEDEFINITIONINVALID;
    }

    /* Set referenceType to parent */
    UA_NodeId referenceToParent;
    UA_NodeId propertyTypeNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE);
    if(UA_NodeId_equal(&type->head.nodeId, &propertyTypeNodeId))
        referenceToParent = UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY);
    else
        referenceToParent = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);

    UA_NodeId optionalObject = {originCondition->namespaceIndex, UA_NODEIDTYPE_NUMERIC, {0}};
    UA_StatusCode retval = addNode(server, UA_NODECLASS_OBJECT, optionalObject,
                                   *originCondition, referenceToParent, *fieldName,
                                   type->head.nodeId, &oAttr, &UA_TYPES[UA_TYPES_OBJECTATTRIBUTES],
                                   NULL, outOptionalObject);
    UA_NODESTORE_RELEASE(server, type);
    return retval;
}

static UA_StatusCode
addConditionOptionalField(UA_Server *server, const UA_NodeId condition,
                          const UA_NodeId conditionType, const UA_QualifiedName fieldName,
                          UA_NodeId *outOptionalNode) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    /* Get optional Field NodId from ConditionType -> user should give the
     * correct ConditionType or Subtype!!!! */
    UA_BrowsePathResult bpr = browseSimplifiedBrowsePath(server, conditionType, 1, &fieldName);
    if(bpr.statusCode != UA_STATUSCODE_GOOD)
        return bpr.statusCode;

    /* Get Node */
    UA_NodeId optionalFieldNodeId = bpr.targets[0].targetId.nodeId;
    const UA_Node *optionalFieldNode = UA_NODESTORE_GET(server, &optionalFieldNodeId);
    if(NULL == optionalFieldNode) {
        UA_LOG_WARNING(server->config.logging, UA_LOGCATEGORY_USERLAND,
                       "Couldn't find optional Field Node in ConditionType. StatusCode %s",
                       UA_StatusCode_name(UA_STATUSCODE_BADNOTFOUND));
        UA_BrowsePathResult_clear(&bpr);
        return UA_STATUSCODE_BADNOTFOUND;
    }

    switch(optionalFieldNode->head.nodeClass) {
        case UA_NODECLASS_VARIABLE: {
            UA_StatusCode retval =
                addOptionalVariableField(server, &condition, &fieldName,
                                         (const UA_VariableNode *)optionalFieldNode, outOptionalNode);
            if(retval != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,
                             "Adding Condition Optional Variable Field failed. StatusCode %s",
                             UA_StatusCode_name(retval));
            }
            UA_BrowsePathResult_clear(&bpr);
            UA_NODESTORE_RELEASE(server, optionalFieldNode);
            return retval;
        }
        case UA_NODECLASS_OBJECT:{
          UA_StatusCode retval =
              addOptionalObjectField(server, &condition, &fieldName,
                                     (const UA_ObjectNode *)optionalFieldNode, outOptionalNode);
          if(retval != UA_STATUSCODE_GOOD) {
              UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,
                           "Adding Condition Optional Object Field failed. StatusCode %s",
                           UA_StatusCode_name(retval));
          }
          UA_BrowsePathResult_clear(&bpr);
          UA_NODESTORE_RELEASE(server, optionalFieldNode);
          return retval;
        }
        case UA_NODECLASS_METHOD:
            /*TODO method: Check first logic of creating methods at all (should
              we create a new method or just reference it from the
              ConditionType?)*/
            UA_BrowsePathResult_clear(&bpr);
            UA_NODESTORE_RELEASE(server, optionalFieldNode);
            return UA_STATUSCODE_BADNOTSUPPORTED;
        default:
            UA_BrowsePathResult_clear(&bpr);
            UA_NODESTORE_RELEASE(server, optionalFieldNode);
            return UA_STATUSCODE_BADNOTSUPPORTED;
    }
}

/**
 * add an optional condition field using its name. (Adding optional methods
 * is not implemented yet)
 */
UA_StatusCode
UA_Server_addConditionOptionalField(UA_Server *server, const UA_NodeId condition,
                                    const UA_NodeId conditionType, const UA_QualifiedName fieldName,
                                    UA_NodeId *outOptionalNode) {
    UA_LOCK(&server->serviceMutex);
    UA_StatusCode res = addConditionOptionalField(server, condition, conditionType,
                                                  fieldName, outOptionalNode);
    UA_UNLOCK(&server->serviceMutex);
    return res;
}

/* Set the value of condition field (only scalar). */
static UA_StatusCode
setConditionField(UA_Server *server, const UA_NodeId condition,
                  const UA_Variant* value, const UA_QualifiedName fieldName) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    if(value->arrayLength != 0 || value->data <= UA_EMPTY_ARRAY_SENTINEL) {
      //TODO implement logic for array variants!
      CONDITION_ASSERT_RETURN_RETVAL(UA_STATUSCODE_BADNOTIMPLEMENTED,
                                     "Set Condition Field with Array value not implemented",);
    }

    UA_BrowsePathResult bpr = browseSimplifiedBrowsePath(server, condition, 1, &fieldName);
    if(bpr.statusCode != UA_STATUSCODE_GOOD)
        return bpr.statusCode;

    UA_StatusCode retval = writeValueAttribute(server, bpr.targets[0].targetId.nodeId, value);
    UA_BrowsePathResult_clear(&bpr);

    return retval;
}

/* Set the value of condition field (only scalar). */
UA_StatusCode
UA_Server_setConditionField(UA_Server *server, const UA_NodeId condition,
                            const UA_Variant* value, const UA_QualifiedName fieldName) {
    UA_LOCK(&server->serviceMutex);
    UA_StatusCode retval = setConditionField(server, condition, value, fieldName);
    UA_UNLOCK(&server->serviceMutex);
    return retval;
}

static UA_StatusCode
setConditionVariableFieldProperty(UA_Server *server, const UA_NodeId condition,
                                  const UA_Variant* value,
                                  const UA_QualifiedName variableFieldName,
                                  const UA_QualifiedName variablePropertyName) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    if(value->arrayLength != 0 || value->data <= UA_EMPTY_ARRAY_SENTINEL) {
        //TODO implement logic for array variants!
        CONDITION_ASSERT_RETURN_RETVAL(UA_STATUSCODE_BADNOTIMPLEMENTED,
                                       "Set Property of Condition Field with Array value not implemented",);
    }

    /* 1) find Variable Field of the Condition*/
    UA_BrowsePathResult bprConditionVariableField =
        browseSimplifiedBrowsePath(server, condition, 1, &variableFieldName);
    if(bprConditionVariableField.statusCode != UA_STATUSCODE_GOOD)
        return bprConditionVariableField.statusCode;

    /* 2) find Property of the Variable Field of the Condition*/
    UA_BrowsePathResult bprVariableFieldProperty =
        browseSimplifiedBrowsePath(server, bprConditionVariableField.targets->targetId.nodeId,
                                   1, &variablePropertyName);
    if(bprVariableFieldProperty.statusCode != UA_STATUSCODE_GOOD) {
        UA_BrowsePathResult_clear(&bprConditionVariableField);
        return bprVariableFieldProperty.statusCode;
    }

    UA_StatusCode retval =
        writeValueAttribute(server, bprVariableFieldProperty.targets[0].targetId.nodeId, value);
    UA_BrowsePathResult_clear(&bprConditionVariableField);
    UA_BrowsePathResult_clear(&bprVariableFieldProperty);
    return retval;
}

/* Set the value of property of condition field. */
UA_StatusCode
UA_Server_setConditionVariableFieldProperty(UA_Server *server, const UA_NodeId condition,
                                            const UA_Variant* value,
                                            const UA_QualifiedName variableFieldName,
                                            const UA_QualifiedName variablePropertyName) {
    UA_LOCK(&server->serviceMutex);
    UA_StatusCode res = setConditionVariableFieldProperty(server, condition, value,
                                                          variableFieldName, variablePropertyName);
    UA_UNLOCK(&server->serviceMutex);
    return res;
}

static UA_StatusCode
triggerConditionEvent(UA_Server *server, const UA_NodeId condition, const UA_NodeId conditionSource, UA_ByteString *outEventId) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    /* Set time */
    UA_DateTime time = UA_DateTime_now();
    UA_StatusCode retval = writeObjectProperty_scalar(server, condition, fieldTimeQN,
                                        &time,
                                        &UA_TYPES[UA_TYPES_DATETIME]);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set Condition Time failed",);
    /* Check if enabled */
    UA_ByteString eventId = UA_BYTESTRING_NULL;
    setIsCallerAC(server, &condition, &conditionSource, true);

    /* Trigger the event for Condition*/
    //Condition Nodes should not be deleted after triggering the event
    retval = triggerEvent(server, condition, conditionSource, &eventId, false);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Triggering condition event failed",);

    setIsCallerAC(server, &condition, &conditionSource, false);

    /* Update list */
    retval = updateConditionLastEventId(server, &condition, &conditionSource, &eventId);
    if(outEventId)
        *outEventId = eventId;
    else
        UA_ByteString_clear(&eventId);
    return retval;
}

/* triggers an event only for an enabled condition. The condition list is
 * updated then with the last generated EventId. */
UA_StatusCode
UA_Server_triggerConditionEvent(UA_Server *server, const UA_NodeId condition,
                                const UA_NodeId conditionSource, UA_ByteString *outEventId) {
    UA_LOCK(&server->serviceMutex);
    UA_StatusCode res = triggerConditionEvent(server, condition, conditionSource, outEventId);
    UA_UNLOCK(&server->serviceMutex);
    return res;
}

static UA_StatusCode
triggerNewBranchState (UA_Server *server, const UA_NodeId condition, const UA_NodeId conditionSource) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);
    UA_StatusCode status = triggerConditionEvent(server, condition, conditionSource, NULL);
    if (status != UA_STATUSCODE_GOOD) return status;

    if (!isBranchIdMainCondition(server, &condition) && isRetained(server, &condition))
    {
       status = deleteConditionBranch (server, &condition);
    }
    return status;
}


// --------------- After variable write

static void
afterWriteCallbackSeverityChange(UA_Server *server,
                                     const UA_NodeId *sessionId, void *sessionContext,
                                     const UA_NodeId *nodeId, void *nodeContext,
                                     const UA_NumericRange *range, const UA_DataValue *data) {
    //TODO
}

static void
afterWriteCallbackQualityChange(UA_Server *server,
                                     const UA_NodeId *sessionId, void *sessionContext,
                                     const UA_NodeId *nodeId, void *nodeContext,
                                     const UA_NumericRange *range, const UA_DataValue *data) {
    //TODO
}


static void
afterWriteCallbackEnabledStateChange(UA_Server *server,
                                     const UA_NodeId *sessionId, void *sessionContext,
                                     const UA_NodeId *nodeId, void *nodeContext,
                                     const UA_NumericRange *range, const UA_DataValue *data)
{
    //TODO
}

static void
afterWriteCallbackAckedStateChange(UA_Server *server,
                                   const UA_NodeId *sessionId, void *sessionContext,
                                   const UA_NodeId *nodeId, void *nodeContext,
                                   const UA_NumericRange *range, const UA_DataValue *data)
{
   //TODO
}


static void
afterWriteCallbackConfirmedStateChange(UA_Server *server,
                                       const UA_NodeId *sessionId, void *sessionContext,
                                       const UA_NodeId *nodeId, void *nodeContext,
                                       const UA_NumericRange *range, const UA_DataValue *data) {
    //TODO
}

static void
afterWriteCallbackActiveStateChange(UA_Server *server,
                                    const UA_NodeId *sessionId, void *sessionContext,
                                    const UA_NodeId *nodeId, void *nodeContext,
                                    const UA_NumericRange *range, const UA_DataValue *data)
{
    //TODO
}


static void
afterWriteCallbackLatchedStateChange(UA_Server *server,
                                     const UA_NodeId *sessionId, void *sessionContext,
                                     const UA_NodeId *nodeId, void *nodeContext,
                                     const UA_NumericRange *range, const UA_DataValue *data)
{
    //TODO
}

static void
afterWriteCallbackSuppressedStateChange(UA_Server *server,
                                        const UA_NodeId *sessionId, void *sessionContext,
                                        const UA_NodeId *nodeId, void *nodeContext,
                                        const UA_NumericRange *range, const UA_DataValue *data)
{
    //TODO
}

static void
afterWriteCallbackOutOfServiceStateChange(UA_Server *server,
                                          const UA_NodeId *sessionId, void *sessionContext,
                                          const UA_NodeId *nodeId, void *nodeContext,
                                          const UA_NumericRange *range, const UA_DataValue *data)
{
    //TODO
}


// ConditionType implementation

static UA_StatusCode
setupConditionNodes (UA_Server *server, const UA_NodeId *condition,
                    const UA_NodeId *conditionType,
                    const UA_ConditionProperties *properties)
{
    /* Set Fields */
    /* 1.Set EventType */
    UA_Variant value;
    UA_Variant_setScalar(&value, (void*)(uintptr_t) conditionType, &UA_TYPES[UA_TYPES_NODEID]);
    UA_StatusCode retval = setConditionField(server, *condition, &value,
                                             UA_QUALIFIEDNAME(0,CONDITION_FIELD_EVENTTYPE));
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set EventType Field failed",);

    /* 2.Set ConditionName */
    UA_Variant_setScalar(&value, (void*)(uintptr_t)&properties->name.name,
                         &UA_TYPES[UA_TYPES_STRING]);
    retval = setConditionField(server, *condition, &value,
                               UA_QUALIFIEDNAME(0,CONDITION_FIELD_CONDITIONNAME));
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set ConditionName Field failed",);

    /* 3.Set EnabledState (Disabled by default -> Retain Field = false) */
    UA_LocalizedText text = UA_LOCALIZEDTEXT(LOCALE, DISABLED_TEXT);
    UA_Variant_setScalar(&value, &text, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
    retval = setConditionField(server, *condition, &value,
                               UA_QUALIFIEDNAME(0,CONDITION_FIELD_ENABLEDSTATE));
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set EnabledState Field failed",);

    /* 4.Set EnabledState/Id */
    UA_Boolean stateId = false;
    UA_Variant_setScalar(&value, &stateId, &UA_TYPES[UA_TYPES_BOOLEAN]);
    retval = setConditionVariableFieldProperty(server, *condition, &value,
                                               UA_QUALIFIEDNAME(0,CONDITION_FIELD_ENABLEDSTATE),
                                               twoStateVariableIdQN);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set EnabledState/Id Field failed",);

    /* 5.Set Retain*/
    UA_Variant_setScalar(&value, &stateId, &UA_TYPES[UA_TYPES_BOOLEAN]);
    retval = setConditionField(server, *condition, &value, fieldRetainQN);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set Retain Field failed",);

    /* Get ConditionSourceNode*/
    const UA_Node *conditionSourceNode = UA_NODESTORE_GET(server, &properties->source);
    if(!conditionSourceNode) {
        UA_LOG_WARNING(server->config.logging, UA_LOGCATEGORY_USERLAND,
                       "Couldn't find ConditionSourceNode. StatusCode %s", UA_StatusCode_name(retval));
        return UA_STATUSCODE_BADNOTFOUND;
    }

    /* 6.Set SourceName*/
    UA_Variant_setScalar(&value, (void*)(uintptr_t)&conditionSourceNode->head.browseName.name,
                         &UA_TYPES[UA_TYPES_STRING]);
    retval = setConditionField(server, *condition, &value,
                               UA_QUALIFIEDNAME(0,CONDITION_FIELD_SOURCENAME));
    if(retval != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,
                     "Set SourceName Field failed. StatusCode %s",
                     UA_StatusCode_name(retval));
        UA_NODESTORE_RELEASE(server, conditionSourceNode);
        return retval;
    }

    /* 7.Set SourceNode*/
    UA_Variant_setScalar(&value, (void*)(uintptr_t)&conditionSourceNode->head.nodeId,
                         &UA_TYPES[UA_TYPES_NODEID]);
    retval = setConditionField(server, *condition, &value, fieldSourceQN);
    if(retval != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,
                     "Set SourceNode Field failed. StatusCode %s", UA_StatusCode_name(retval));
        UA_NODESTORE_RELEASE(server, conditionSourceNode);
        return retval;
    }

    UA_NODESTORE_RELEASE(server, conditionSourceNode);

    /* 8. Set Quality (TODO not supported, thus set with Status Good) */
    UA_StatusCode qualityValue = UA_STATUSCODE_GOOD;
    UA_Variant_setScalar(&value, &qualityValue, &UA_TYPES[UA_TYPES_STATUSCODE]);
    retval = setConditionField(server, *condition, &value,
                               UA_QUALIFIEDNAME(0,CONDITION_FIELD_QUALITY));
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set Quality Field failed",);

    /* 9. Set Severity */
    UA_UInt16 severityValue = 0;
    UA_Variant_setScalar(&value, &severityValue, &UA_TYPES[UA_TYPES_UINT16]);
    retval = setConditionField(server, *condition, &value,
                               UA_QUALIFIEDNAME(0,CONDITION_FIELD_SEVERITY));
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set Severity Field failed",);
    return retval;
}

static UA_StatusCode
UA_Server_setupAcknowledgeableConditionNodes (UA_Server *server, const UA_NodeId *condition,
                                   const UA_AcknowledgeableConditionProperties *properties)
{
    UA_NodeId acknowledgeableConditionTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE);
    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    UA_Variant value;
    UA_LocalizedText text = UA_LOCALIZEDTEXT(LOCALE, UNACKED_TEXT);
    UA_Variant_setScalar(&value, &text, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
    retval = setConditionField(server, *condition, &value, fieldAckedStateQN);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set AckedState Field failed",);

    UA_Boolean stateId = false;
    UA_Variant_setScalar(&value, &stateId, &UA_TYPES[UA_TYPES_BOOLEAN]);
    retval = setConditionVariableFieldProperty(server, *condition, &value,
                                               fieldAckedStateQN, twoStateVariableIdQN);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set AckedState/Id Field failed",);

    UA_NodeId twoStateVariableIdNodeId = UA_NODEID_NULL;
    retval = getConditionFieldPropertyNodeId(server, condition, &fieldAckedStateQN,
                                             &twoStateVariableIdQN, &twoStateVariableIdNodeId);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Id Property of TwoStateVariable not found",);

    UA_ValueCallback callback;
    callback.onRead = NULL;
    callback.onWrite = afterWriteCallbackAckedStateChange;
    retval = setVariableNode_valueCallback(server, twoStateVariableIdNodeId, callback);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set AckedState Callback failed",
                                   UA_NodeId_clear(&twoStateVariableIdNodeId););

    /* add optional field ConfirmedState*/
    if (properties->confirmable)
    {
        retval = addConditionOptionalField(server, *condition, acknowledgeableConditionTypeId,
                                           fieldConfirmedStateQN, NULL);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding ConfirmedState optional Field failed",);

        /* Set ConfirmedState (Id = false by default) */
        text = UA_LOCALIZEDTEXT(LOCALE, UNCONFIRMED_TEXT);
        UA_Variant_setScalar(&value, &text, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
        retval = setConditionField(server, *condition, &value, fieldConfirmedStateQN);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Set ConfirmedState Field failed",);

        UA_Variant_setScalar(&value, &stateId, &UA_TYPES[UA_TYPES_BOOLEAN]);
        retval = setConditionVariableFieldProperty(server, *condition, &value,
                                                   fieldConfirmedStateQN,
                                                   twoStateVariableIdQN);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Set Confirmed/Id Field failed",);

        /* add callback */
        callback.onWrite = afterWriteCallbackConfirmedStateChange;
        UA_NodeId_clear(&twoStateVariableIdNodeId);
        retval = getConditionFieldPropertyNodeId(server, condition, &fieldConfirmedStateQN,
                                                 &twoStateVariableIdQN, &twoStateVariableIdNodeId);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Id Property of TwoStateVariable not found",);

        /* add reference from Condition to Confirm Method */
        UA_NodeId hasComponent = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
        UA_NodeId confirm = UA_NODEID_NUMERIC(0, UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_CONFIRM);
        retval = addRef(server, *condition, hasComponent, confirm, true);
        CONDITION_ASSERT_RETURN_RETVAL(retval,
                                       "Adding HasComponent Reference to Confirm Method failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););

        retval = setVariableNode_valueCallback(server, twoStateVariableIdNodeId, callback);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding ConfirmedState/Id callback failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););
    }
    return retval;
}

static UA_StatusCode
UA_Server_setupAlarmConditionNodes (UA_Server *server, const UA_NodeId *condition,
                                    const UA_AlarmConditionProperties *properties)
{
    UA_Boolean stateId = false;
    UA_NodeId alarmConditionTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE);
    UA_LocalizedText text = UA_LOCALIZEDTEXT(LOCALE, INACTIVE_TEXT);
    UA_Variant value;
    UA_Variant_setScalar(&value, &text, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
    UA_StatusCode retval = setConditionField(server, *condition, &value,
                               UA_QUALIFIEDNAME(0,CONDITION_FIELD_ACTIVESTATE));
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set ActiveState Field failed",);

    UA_NodeId twoStateVariableIdNodeId = UA_NODEID_NULL;
    retval = getConditionFieldPropertyNodeId(server, condition, &fieldActiveStateQN,
                                             &twoStateVariableIdQN, &twoStateVariableIdNodeId);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Id Property of TwoStateVariable not found",);

    UA_ValueCallback callback;
    callback.onRead = NULL;
    callback.onWrite = afterWriteCallbackActiveStateChange;
    retval = setVariableNode_valueCallback(server, twoStateVariableIdNodeId,
                                           callback);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Set ActiveState Callback failed",
                                   UA_NodeId_clear(&twoStateVariableIdNodeId););

    if (!UA_NodeId_isNull(&properties->inputNode))
    {
        UA_Variant_setScalar(&value,(void *)(uintptr_t) &properties->inputNode, &UA_TYPES[UA_TYPES_NODEID]);
        retval = setConditionField (server, *condition, &value,
                                       UA_QUALIFIEDNAME (0, CONDITION_FIELD_INPUTNODE));
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Set InputNode Field failed",);
    }

    if (properties->latchable)
    {
        retval = addConditionOptionalField(server, *condition, alarmConditionTypeId,
                                           fieldLatchedStateQN, NULL);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding LatchedState optional Field failed",);

        /* Set LatchedState (Id = false by default) */
        text = UA_LOCALIZEDTEXT (LOCALE,  NOT_LATCHED_TEXT);
        UA_Variant_setScalar(&value, &text, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
        retval = setConditionField (server, *condition, &value, fieldLatchedStateQN);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Set LatchedState Field failed",);

        UA_Variant_setScalar(&value, &stateId, &UA_TYPES[UA_TYPES_BOOLEAN]);
        retval = setConditionVariableFieldProperty(server, *condition, &value,
                                                   fieldLatchedStateQN,
                                                   twoStateVariableIdQN);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Set Latched/Id Field failed",);

        /* add callback */
        UA_NodeId_clear(&twoStateVariableIdNodeId);
        retval = getConditionFieldPropertyNodeId(server, condition, &fieldLatchedStateQN,
                                                 &twoStateVariableIdQN, &twoStateVariableIdNodeId);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Id Property of TwoStateVariable not found",);

        callback.onWrite = afterWriteCallbackLatchedStateChange;
        retval = setVariableNode_valueCallback(server, twoStateVariableIdNodeId, callback);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding LatchedState/Id callback failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););

        /* add reference from Condition to Reset Method */
        UA_NodeId hasComponent = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
        UA_NodeId reset = UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE_RESET );
        retval = addRef(server, *condition, hasComponent, reset, true);
        CONDITION_ASSERT_RETURN_RETVAL(retval,
                                       "Adding HasComponent Reference to Reset Method failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););

        /* add reference from Condition to Reset2 Method */
        UA_NodeId reset2 = UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE_RESET2);
        retval = addRef(server, *condition, hasComponent, reset2, true);
        CONDITION_ASSERT_RETURN_RETVAL(retval,
                                       "Adding HasComponent Reference to Reset2 Method failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););
    }

    if (properties->suppressible)
    {
        retval = addConditionOptionalField(server, *condition, alarmConditionTypeId,
                                           fieldSuppressedStateQN, NULL);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding SuppressedState optional Field failed",);

        /* Set SuppressedState (Id = false by default) */
        text = UA_LOCALIZEDTEXT(LOCALE, NOT_SUPPRESSED_TEXT);
        UA_Variant_setScalar(&value, &text, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
        retval = setConditionField (server, *condition, &value, fieldSuppressedStateQN);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Set SuppressedState Field failed",);

        UA_Variant_setScalar(&value, &stateId, &UA_TYPES[UA_TYPES_BOOLEAN]);
        retval = setConditionVariableFieldProperty(server, *condition, &value,
                                                   fieldSuppressedStateQN,
                                                   twoStateVariableIdQN);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Set Suppressed/Id Field failed",);

        /* add callback */
        UA_NodeId_clear(&twoStateVariableIdNodeId);
        retval = getConditionFieldPropertyNodeId(server, condition, &fieldSuppressedStateQN,
                                                 &twoStateVariableIdQN, &twoStateVariableIdNodeId);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Id Property of TwoStateVariable not found",);

        callback.onWrite = afterWriteCallbackSuppressedStateChange;
        retval = setVariableNode_valueCallback(server, twoStateVariableIdNodeId, callback);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding Suppressed/Id callback failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););

        /* add reference from Condition to Suppress Method */
        UA_NodeId hasComponent = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
        UA_NodeId suppress = UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE_SUPPRESS);
        retval = addRef(server, *condition, hasComponent, suppress, true);
        CONDITION_ASSERT_RETURN_RETVAL(retval,
                                       "Adding HasComponent Reference to Suppress Method failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););

        /* add reference from Condition to Suppress2 Method */
        UA_NodeId suppress2 = UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE_SUPPRESS2);
        retval = addRef(server, *condition, hasComponent, suppress2, true);
        CONDITION_ASSERT_RETURN_RETVAL(retval,
                                       "Adding HasComponent Reference to Suppress2 Method failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););

        /* add reference from Condition to UnSuppress Method */
        UA_NodeId unsuppress = UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE_UNSUPPRESS);
        retval = addRef(server, *condition, hasComponent, unsuppress, true);
        CONDITION_ASSERT_RETURN_RETVAL(retval,
                                       "Adding HasComponent Reference to UnSuppress Method failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););

        /* add reference from Condition to UnSuppress2 Method */
        UA_NodeId unsuppress2 = UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE_UNSUPPRESS2);
        retval = addRef(server, *condition, hasComponent, unsuppress2, true);
        CONDITION_ASSERT_RETURN_RETVAL(retval,
                                       "Adding HasComponent Reference to UnSuppress2 Method failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););
    }

    if (properties->serviceable)
    {
        retval = addConditionOptionalField(server, *condition, alarmConditionTypeId,
                                           fieldOutOfServiceStateQN, NULL);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding OutOfServiceState optional Field failed",);

        /* Set OutOfServiceState (Id = false by default) */
        text = UA_LOCALIZEDTEXT(LOCALE, IN_SERVICE_TEXT);
        UA_Variant_setScalar(&value, &text, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
        retval = setConditionField (server, *condition, &value, fieldOutOfServiceStateQN);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Set OutOfServiceState Field failed",);

        UA_Variant_setScalar(&value, &stateId, &UA_TYPES[UA_TYPES_BOOLEAN]);
        retval = setConditionVariableFieldProperty(server, *condition, &value,
                                                   fieldOutOfServiceStateQN,
                                                   twoStateVariableIdQN);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Set OutOfServiceState/Id Field failed",);

        /* add callback */
        UA_NodeId_clear(&twoStateVariableIdNodeId);
        retval = getConditionFieldPropertyNodeId(server, condition, &fieldOutOfServiceStateQN,
                                                 &twoStateVariableIdQN, &twoStateVariableIdNodeId);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Id Property of TwoStateVariable not found",);

        callback.onWrite = afterWriteCallbackOutOfServiceStateChange;
        retval = setVariableNode_valueCallback(server, twoStateVariableIdNodeId, callback);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding OutOfServiceState/Id callback failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););

        if (properties->maxTimeShelved)
        {
            retval = addConditionOptionalField (server, *condition, alarmConditionTypeId,
                                                fieldMaxTimeShelvedQN, NULL);
            CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding MaxTimeShelved optional Field failed",);
            UA_Variant_setScalar(&value, (void *) (uintptr_t) properties->maxTimeShelved, &UA_TYPES[UA_TYPES_DURATION]);
            retval = setConditionField (server, *condition, &value, fieldMaxTimeShelvedQN);
            CONDITION_ASSERT_RETURN_RETVAL(retval, "Set MaxTimeShelved Field failed",);
        }

        UA_NodeId hasComponent = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
        UA_NodeId place = UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE_PLACEINSERVICE);
        retval = addRef(server, *condition, hasComponent, place, true);
        CONDITION_ASSERT_RETURN_RETVAL(retval,
                                       "Adding HasComponent Reference to PlaceInService Method failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););

        UA_NodeId place2 = UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE_PLACEINSERVICE2);
        retval = addRef(server, *condition, hasComponent, place2, true);
        CONDITION_ASSERT_RETURN_RETVAL(retval,
                                       "Adding HasComponent Reference to PlaceInService2 Method failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););

        UA_NodeId remove = UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE_REMOVEFROMSERVICE);
        retval = addRef(server, *condition, hasComponent, remove, true);
        CONDITION_ASSERT_RETURN_RETVAL(retval,
                                       "Adding HasComponent Reference to RemoveFromService Method failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););

        UA_NodeId remove2 = UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE_REMOVEFROMSERVICE2);
        retval = addRef(server, *condition, hasComponent, remove2, true);
        CONDITION_ASSERT_RETURN_RETVAL(retval,
                                       "Adding HasComponent Reference to RemoveFromService2 Method failed",
                                       UA_NodeId_clear(&twoStateVariableIdNodeId););
    }

    if (properties->onDelay)
    {
        retval = addConditionOptionalField (server, *condition, alarmConditionTypeId,
                                            fieldOnDelayQN, NULL);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding OnDelay optional Field failed",);
        UA_Variant_setScalar(&value, (void *) (uintptr_t) properties->onDelay, &UA_TYPES[UA_TYPES_DURATION]);
        retval = setConditionField (server, *condition, &value, fieldOnDelayQN);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Set OnDelay Field failed",);
    }
    if (properties->offDelay)
    {
        retval = addConditionOptionalField (server, *condition, alarmConditionTypeId,
                                            fieldOffDelayQN, NULL);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding OffDelay optional Field failed",);
        UA_Variant_setScalar(&value, (void *) (uintptr_t) properties->offDelay, &UA_TYPES[UA_TYPES_DURATION]);
        retval = setConditionField (server, *condition, &value, fieldOffDelayQN);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Set OffDelay Field failed",);
    }
    if (properties->reAlarmTime)
    {
        retval = addConditionOptionalField (server, *condition, alarmConditionTypeId,
                                            fieldReAlarmTimeQN, NULL);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding ReAlarmTime optional Field failed",);
        UA_Variant_setScalar(&value, (void *) (uintptr_t) properties->reAlarmTime, &UA_TYPES[UA_TYPES_DURATION]);
        retval = setConditionField (server, *condition, &value, fieldReAlarmTimeQN);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Set ReAlarmTime Field failed",);
    }
    if (properties->reAlarmRepeatCount)
    {
        retval = addConditionOptionalField (server, *condition, alarmConditionTypeId,
                                            fieldReAlarmRepeatCountQN, NULL);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding ReAlarmRepeatCount optional Field failed",);
        UA_Variant_setScalar(&value, (void *) (uintptr_t) properties->reAlarmRepeatCount, &UA_TYPES[UA_TYPES_INT16]);
        retval = setConditionField (server, *condition, &value, fieldReAlarmRepeatCountQN);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Set ReAlarmTimeRepeatCount Field failed",);
    }

    //TODO add support for alarm audio
    //TODO alarm suppression groups
    return retval;
}

static UA_StatusCode
UA_Server_setupDiscrepancyAlarmNodes (UA_Server *server, const UA_NodeId *condition,
                                            const UA_DiscrepancyAlarmProperties *properties)
{
    //TODO
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
UA_Server_setupOffNormalAlarmNodes (UA_Server *server, const UA_NodeId *condition,
                                      const UA_DiscrepancyAlarmProperties *properties)
{
    //TODO
    return UA_STATUSCODE_GOOD;
}


static UA_StatusCode
UA_Server_setupCertificateExpirationAlarmNodes (UA_Server *server, const UA_NodeId *condition,
                                                const UA_CertificateExpirationAlarmProperties *properties)
{
    UA_NodeId certificateConditionTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_CERTIFICATEEXPIRATIONALARMTYPE);
    UA_StatusCode retval = addConditionOptionalField(server, *condition, certificateConditionTypeId,
                                                     fieldExpirationDateQN, NULL);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding Expiration Limit optional field failed",);

    /* Set the default value for the Expiration limit property */
    UA_Duration defaultValue = EXPIRATION_LIMIT_DEFAULT_VALUE;
    retval |= writeObjectProperty_scalar (server, *condition, fieldExpirationDateQN,
                                          &defaultValue, &UA_TYPES[UA_TYPES_DURATION]);
    return retval;
}

static UA_StatusCode
UA_Server_setupLimitAlarmNodes(UA_Server *server, const UA_NodeId *condition, const UA_LimitAlarmProperties * properties)
{
    UA_NodeId LimitAlarmTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_LIMITALARMTYPE);
    UA_StatusCode retval = addConditionOptionalField(server, *condition, LimitAlarmTypeId,
                                       fieldLowLimitQN, NULL);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding LowLimit optional Field failed",);

    /* Add optional field HighLimit */
    retval = addConditionOptionalField(server, *condition, LimitAlarmTypeId,
                                       fieldHighLimitQN, NULL);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding HighLimit optional Field failed",);

    /* Add optional field HighHighLimit */
    retval = addConditionOptionalField(server, *condition, LimitAlarmTypeId,
                                       fieldHighHighLimitQN, NULL);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding HighLimit optional Field failed",);

    /* Add optional field LowLowLimit */
    retval = addConditionOptionalField(server, *condition, LimitAlarmTypeId,
                                       fieldLowLowLimitQN, NULL);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Adding LowLowLimit optional Field failed",);
    return retval;
}

static UA_StatusCode
setupDeviationAlarmNodes (UA_Server *server, const UA_NodeId *condition,
                            const UA_DeviationAlarmProperties *properties)
{
    //TODO
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
UA_Server_setupExclusiveDeviationAlarmNodes (UA_Server *server, const UA_NodeId *condition,
                                             const UA_DeviationAlarmProperties *properties)
{
    return setupDeviationAlarmNodes (server, condition, properties);
}

static UA_StatusCode
UA_Server_setupNonExclusiveDeviationAlarmNodes (UA_Server *server, const UA_NodeId *condition,
                                             const UA_DeviationAlarmProperties *properties)
{
    return setupDeviationAlarmNodes (server, condition, properties);
}

static UA_StatusCode
setupRateOfChangeAlarmNodes (UA_Server *server, const UA_NodeId *condition,
                          const UA_RateOfChangeAlarmProperties *properties)
{
    UA_NodeId RateOfChangeAlarmTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_EXCLUSIVERATEOFCHANGEALARMTYPE);
    UA_StatusCode retval = addConditionOptionalField(server, *condition, RateOfChangeAlarmTypeId,
                                                     fieldEngineeringUnitsQN, NULL);
    //todo write value to node
    return retval;
}


static UA_StatusCode
UA_Server_setupExclusiveRateOfChangeAlarmNodes (UA_Server *server, const UA_NodeId *condition,
                                         const UA_RateOfChangeAlarmProperties *properties)
{
    return setupRateOfChangeAlarmNodes (server, condition, properties);
}

static UA_StatusCode
UA_Server_setupNonExclusiveRateOfChangeAlarmNodes (UA_Server *server, const UA_NodeId *condition,
                                                   const UA_RateOfChangeAlarmProperties *properties)
{
    return setupRateOfChangeAlarmNodes (server, condition, properties);
}


// -------- Interact with condition

static UA_StatusCode
conditionSetEnabledState (UA_Server *server, const UA_NodeId *conditionId, UA_Boolean enabled)
{
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    /* Cant enable/disable branches - only main condition */
    if (isBranch(server, conditionId)) return UA_STATUSCODE_BADNODEIDINVALID;
    if (conditionEnabled (server, conditionId) == enabled)
        return enabled ? UA_STATUSCODE_BADCONDITIONALREADYENABLED : UA_STATUSCODE_BADCONDITIONALREADYDISABLED;

    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    retval = setEnabledState(server, conditionId, enabled);
    if (retval != UA_STATUSCODE_GOOD) return retval;

    /* Set condition message */
    UA_LocalizedText message = UA_LOCALIZEDTEXT(LOCALE, enabled ? ENABLED_MESSAGE : DISABLED_MESSAGE);
    setMessage(server, conditionId, &message);

    UA_NodeId conditionSource;
    retval = getNodeIdValueOfConditionField(server, conditionId, fieldSourceQN, &conditionSource);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "ConditionSource not found",);
    UA_Condition *cond = getCondition(server, &conditionSource, conditionId);

    /**
     * When the Condition instance enters the Disabled state, the Retain Property of this
     * Condition shall be set to False by the Server to indicate to the Client that the
     * Condition instance is currently not of interest to Clients. This includes all
     * ConditionBranches if any branches exist.
     * https://reference.opcfoundation.org/Core/Part9/v105/docs/5.5
     *
     *
     */
    if (enabled == false)
    {
        UA_ConditionBranch *branch;
        LIST_FOREACH(branch, &cond->conditionBranches, listEntry) {
            UA_NodeId triggeredNode;
            UA_NodeId_init(&triggeredNode);
            triggeredNode = UA_NodeId_isNull(&branch->conditionBranchId) ?
                cond->conditionId : branch->conditionBranchId;
            //set retain property for each branch
            setRetain (server, &triggeredNode, false);

            retval = triggerConditionEvent(server, triggeredNode, conditionSource, NULL);
            CONDITION_ASSERT_RETURN_RETVAL(retval, "triggering condition event failed", UA_NodeId_clear(&conditionSource););
        }
    }
    else
    {
        // TODO evaluate condition

        //resend notifications for any branch where retain = true
        size_t resent_count = 0;
        UA_ConditionBranch *branch;
        LIST_FOREACH(branch, &cond->conditionBranches, listEntry) {
            UA_NodeId triggeredNode;
            UA_NodeId_init(&triggeredNode);
            triggeredNode = UA_NodeId_isNull(&branch->conditionBranchId) ?
                cond->conditionId : branch->conditionBranchId;
            //check retain property for each branch
            if (!isRetained(server, &triggeredNode))
                continue;

            /* Set time */
            UA_DateTime time = UA_DateTime_now();
            retval = writeObjectProperty_scalar(server, *conditionId, fieldTimeQN,
                                                &time,
                                                &UA_TYPES[UA_TYPES_DATETIME]);
            CONDITION_ASSERT_RETURN_RETVAL(retval, "Set Time failed",
                                           UA_NodeId_clear(&conditionSource););
            retval = triggerConditionEvent(server, triggeredNode, conditionSource, NULL);
            if (retval != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,
                             "triggering condition event failed. StatusCode %s",
                             UA_StatusCode_name(retval));
            }
            resent_count++;
        }

        if (resent_count == 0)
        {
            //always send a notification
            retval = triggerConditionEvent(server, *conditionId, conditionSource, NULL);
            CONDITION_ASSERT_RETURN_RETVAL(retval, "triggering condition event failed", UA_NodeId_clear(&conditionSource););
        }

    }
    UA_NodeId_clear(&conditionSource);
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
UA_Server_conditionSetEnabledState (UA_Server *server, const UA_NodeId *conditionId, UA_Boolean enabled)
{
    UA_LOCK(&server->serviceMutex);
    UA_StatusCode res = conditionSetEnabledState (server, conditionId, enabled);
    UA_UNLOCK(&server->serviceMutex);
    return res;
}

static UA_StatusCode
conditionAcknowledge (UA_Server *server, const UA_NodeId *conditionId, UA_LocalizedText comment)
{
    UA_LOCK_ASSERT(&server->serviceMutex, 1);
    if(conditionAcked (server, conditionId))
        return UA_STATUSCODE_BADCONDITIONBRANCHALREADYACKED;

    setAckedState (server, conditionId, true);

    if (isConfirmable(server, conditionId))
    {
        setConfirmedState(server, conditionId, false);
        setRetain(server, conditionId, true);
    }
    else
    {
        setRetain(server, conditionId, false);
    }

    setComment (server, conditionId, &comment);

    UA_NodeId conditionSource;
    UA_StatusCode retval = getNodeIdValueOfConditionField(server, conditionId, fieldSourceQN, &conditionSource);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "ConditionSource not found",);
    triggerNewBranchState (server, *conditionId, conditionSource);
    return retval;
}

static UA_StatusCode
UA_Server_conditionAcknowledge (UA_Server *server, const UA_NodeId *conditionId, UA_LocalizedText comment)
{
    UA_LOCK(&server->serviceMutex);
    UA_StatusCode res = conditionAcknowledge (server, conditionId, comment);
    UA_UNLOCK(&server->serviceMutex);
    return res;
}

static UA_StatusCode
conditionConfirm (UA_Server *server, const UA_NodeId *conditionId, UA_LocalizedText comment)
{
    UA_LOCK_ASSERT(&server->serviceMutex, 1);

    if (conditionConfirmed (server, conditionId))
        return UA_STATUSCODE_BADCONDITIONBRANCHALREADYCONFIRMED;

    setConfirmedState (server, conditionId, true);
    setRetain (server, conditionId, false);
    setComment (server, conditionId, &comment);
    UA_NodeId conditionSource;
    UA_StatusCode retval = getNodeIdValueOfConditionField(server, conditionId, fieldSourceQN, &conditionSource);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "ConditionSource not found",);
    triggerNewBranchState (server, *conditionId, conditionSource);
    return retval;
}

static UA_StatusCode
UA_Server_conditionConfirm (UA_Server *server, const UA_NodeId *conditionId, UA_LocalizedText comment)
{
    UA_LOCK(&server->serviceMutex);
    UA_StatusCode res = conditionConfirm (server, conditionId, comment);
    UA_UNLOCK(&server->serviceMutex);
    return res;
}

static UA_StatusCode
refreshLogic(UA_Server *server, const UA_NodeId *refreshStartNodId,
             const UA_NodeId *refreshEndNodId, UA_MonitoredItem *monitoredItem) {
    UA_LOCK_ASSERT(&server->serviceMutex, 1);
    UA_assert(monitoredItem != NULL);

    /* 1. Trigger RefreshStartEvent */
    UA_DateTime fieldTimeValue = UA_DateTime_now();
    UA_StatusCode retval =
        writeObjectProperty_scalar(server, *refreshStartNodId, fieldTimeQN,
                                   &fieldTimeValue, &UA_TYPES[UA_TYPES_DATETIME]);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Write Object Property scalar failed",);

    retval = UA_MonitoredItem_addEvent(server, monitoredItem, refreshStartNodId);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Events: Could not add the event to a listening node",);

    /* 2. Refresh (see 5.5.7) */
    /* Get ConditionSource Entry */
    UA_ConditionSource *source;
    LIST_FOREACH(source, &server->conditionSources, listEntry) {
        UA_NodeId conditionSource = source->conditionSourceId;
        UA_NodeId serverObjectNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
        /* Check if the conditionSource is being monitored. If the Server Object
         * is being monitored, then all Events of all monitoredItems should be
         * refreshed */
        if(!UA_NodeId_equal(&monitoredItem->itemToMonitor.nodeId, &conditionSource) &&
           !UA_NodeId_equal(&monitoredItem->itemToMonitor.nodeId, &serverObjectNodeId) &&
           !isConditionSourceInMonitoredItem(server, monitoredItem, &conditionSource))
            continue;

        /* Get Condition Entry */
        UA_Condition *cond;
        LIST_FOREACH(cond, &source->conditions, listEntry) {
            /* Get Branch Entry */
            UA_ConditionBranch *branch;
            LIST_FOREACH(branch, &cond->conditionBranches, listEntry) {
                /* If no event was triggered for that branch, then check next
                 * without refreshing */
                if(UA_ByteString_equal(&branch->lastEventId, &UA_BYTESTRING_NULL))
                    continue;

                UA_NodeId triggeredNode;
                if(UA_NodeId_isNull(&branch->conditionBranchId))
                    triggeredNode = cond->conditionId;
                else
                    triggeredNode = branch->conditionBranchId;

                /* Check if Retain is set to true */
                if(!isRetained(server, &triggeredNode))
                    continue;

                /* Add the event */
                retval = UA_MonitoredItem_addEvent(server, monitoredItem, &triggeredNode);
                CONDITION_ASSERT_RETURN_RETVAL(retval, "Events: Could not add the event to a listening node",);
            }
        }
    }

    /* 3. Trigger RefreshEndEvent */
    fieldTimeValue = UA_DateTime_now();
    retval = writeObjectProperty_scalar(server, *refreshEndNodId, fieldTimeQN,
                                        &fieldTimeValue, &UA_TYPES[UA_TYPES_DATETIME]);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Write Object Property scalar failed",);
    return UA_MonitoredItem_addEvent(server, monitoredItem, refreshEndNodId);
}


// -------- Method Node Callbacks

static UA_StatusCode
enableMethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                     void *sessionContext, const UA_NodeId *methodId,
                     void *methodContext, const UA_NodeId *objectId,
                     void *objectContext, size_t inputSize,
                     const UA_Variant *input, size_t outputSize,
                     UA_Variant *output)
{
    //TODO check object id type
    return UA_Server_conditionSetEnabledState (server, objectId, true);
}

static UA_StatusCode
disableMethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                      void *sessionContext, const UA_NodeId *methodId,
                      void *methodContext, const UA_NodeId *objectId,
                      void *objectContext, size_t inputSize,
                      const UA_Variant *input, size_t outputSize, UA_Variant *output)
{
    //TODO check object id type
    return UA_Server_conditionSetEnabledState (server, objectId, false);
}

static UA_StatusCode
addCommentMethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                         void *sessionContext, const UA_NodeId *methodId,
                         void *methodContext, const UA_NodeId *objectId,
                         void *objectContext, size_t inputSize,
                         const UA_Variant *input, size_t outputSize,
                         UA_Variant *output)
{
    //TODO check object id type
    UA_NodeId conditionId;
    UA_NodeId_init (&conditionId);

    UA_StatusCode retval = UA_Server_getConditionBranchNodeId(
        server,
        objectId,
        (UA_ByteString *)input[0].data,
        &conditionId
    );
    CONDITION_ASSERT_GOTOLABEL(retval, "ConditionId based on EventId not found",done);

    /* Check if enabled */
    if(!UA_Server_conditionEnabled (server, &conditionId))
        return UA_STATUSCODE_BADCONDITIONDISABLED;

    UA_LocalizedText message = UA_LOCALIZEDTEXT(LOCALE, COMMENT_MESSAGE);
    setMessage(server, &conditionId, &message);

    /* Set Comment. Check whether comment is empty -> leave the last value as is*/
    UA_LocalizedText *inputComment = (UA_LocalizedText *)input[1].data;
    UA_String nullString = UA_STRING_NULL;
    if(!UA_ByteString_equal(&inputComment->locale, &nullString) &&
       !UA_ByteString_equal(&inputComment->text, &nullString)) {
        retval = UA_Server_setComment(server, &conditionId, inputComment);
        CONDITION_ASSERT_GOTOLABEL (retval, "Set Condition Comment failed", done);
    }

    /* Get conditionSource */
    UA_NodeId conditionSource;
    retval = UA_Server_getNodeIdValueOfConditionField(server, &conditionId,
                                                      fieldSourceQN, &conditionSource);
    CONDITION_ASSERT_GOTOLABEL (retval, "ConditionSource not found", done);

    /* Trigger event */
    retval = UA_Server_triggerConditionEvent (server, conditionId, conditionSource, NULL);
    UA_NodeId_clear(&conditionSource);
    CONDITION_ASSERT_GOTOLABEL (retval, "Triggering condition event failed",done);
done:
    UA_NodeId_clear(&conditionId);
    return retval;
}

static UA_StatusCode
refreshMethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                      void *sessionContext, const UA_NodeId *methodId,
                      void *methodContext, const UA_NodeId *objectId,
                      void *objectContext, size_t inputSize,
                      const UA_Variant *input, size_t outputSize,
                      UA_Variant *output) {
    UA_LOCK(&server->serviceMutex);

    //TODO implement logic for subscription array
    /* Check if valid subscriptionId */
    UA_Session *session = getSessionById(server, sessionId);
    UA_Subscription *subscription =
        UA_Session_getSubscriptionById(session, *((UA_UInt32 *)input[0].data));
    if(!subscription) {
        UA_UNLOCK(&server->serviceMutex);
        return UA_STATUSCODE_BADSUBSCRIPTIONIDINVALID;
    }

    /* set RefreshStartEvent and RefreshEndEvent */
    UA_StatusCode retval =
        setRefreshMethodEvents(server, &server->refreshEvents[REFRESHEVENT_START_IDX],
                               &server->refreshEvents[REFRESHEVENT_END_IDX]);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Create Event RefreshStart or RefreshEnd failed",
                                   UA_UNLOCK(&server->serviceMutex););

    /* Trigger RefreshStartEvent and RefreshEndEvent for the each monitoredItem
     * in the subscription */
    //TODO when there are a lot of monitoreditems (not only events)?
    UA_MonitoredItem *monitoredItem = NULL;
    LIST_FOREACH(monitoredItem, &subscription->monitoredItems, listEntry) {
        retval = refreshLogic(server, &server->refreshEvents[REFRESHEVENT_START_IDX],
                              &server->refreshEvents[REFRESHEVENT_END_IDX], monitoredItem);
        CONDITION_ASSERT_RETURN_RETVAL(retval, "Could not refresh Condition",
                                       UA_UNLOCK(&server->serviceMutex););
    }
    UA_UNLOCK(&server->serviceMutex);
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
refresh2MethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                       void *sessionContext, const UA_NodeId *methodId,
                       void *methodContext, const UA_NodeId *objectId,
                       void *objectContext, size_t inputSize,
                       const UA_Variant *input, size_t outputSize,
                       UA_Variant *output)
{
    UA_LOCK(&server->serviceMutex);
    //TODO implement logic for subscription array
    /* Check if valid subscriptionId */
    UA_Session *session = getSessionById(server, sessionId);
    UA_Subscription *subscription =
        UA_Session_getSubscriptionById(session, *((UA_UInt32 *)input[0].data));
    if(!subscription) {
        UA_UNLOCK(&server->serviceMutex);
        return UA_STATUSCODE_BADSUBSCRIPTIONIDINVALID;
    }

    /* set RefreshStartEvent and RefreshEndEvent */
    UA_StatusCode retval = setRefreshMethodEvents(server,
                                                  &server->refreshEvents[REFRESHEVENT_START_IDX],
                                                  &server->refreshEvents[REFRESHEVENT_END_IDX]);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Create Event RefreshStart or RefreshEnd failed",
                                   UA_UNLOCK(&server->serviceMutex););

    /* Trigger RefreshStartEvent and RefreshEndEvent for the each monitoredItem
     * in the subscription */
    UA_MonitoredItem *monitoredItem =
        UA_Subscription_getMonitoredItem(subscription, *((UA_UInt32 *)input[1].data));
    if(!monitoredItem) {
        UA_UNLOCK(&server->serviceMutex);
        return UA_STATUSCODE_BADMONITOREDITEMIDINVALID;
    }

    //TODO when there are a lot of monitoreditems (not only events)?
    retval = refreshLogic(server, &server->refreshEvents[REFRESHEVENT_START_IDX],
                          &server->refreshEvents[REFRESHEVENT_END_IDX], monitoredItem);
    CONDITION_ASSERT_RETURN_RETVAL(retval, "Could not refresh Condition",
                                   UA_UNLOCK(&server->serviceMutex););
    UA_UNLOCK(&server->serviceMutex);
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
acknowledgeMethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                          void *sessionContext, const UA_NodeId *methodId,
                          void *methodContext, const UA_NodeId *objectId,
                          void *objectContext, size_t inputSize,
                          const UA_Variant *input, size_t outputSize,
                          UA_Variant *output)
{
    //TODO check object id type

    UA_NodeId conditionId;
    UA_StatusCode retval = UA_Server_getConditionBranchNodeId (
        server,
        objectId,
        (UA_ByteString *)input[0].data,
        &conditionId
    );
    CONDITION_ASSERT_GOTOLABEL(retval, "ConditionId based on EventId not found",done);
    UA_LocalizedText *comment = (UA_LocalizedText *)input[1].data;
    retval = UA_Server_conditionAcknowledge (server, &conditionId, *comment);
done:
    UA_NodeId_clear(&conditionId);
    return retval;
}

static UA_StatusCode
confirmMethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                      void *sessionContext, const UA_NodeId *methodId,
                      void *methodContext, const UA_NodeId *objectId,
                      void *objectContext, size_t inputSize,
                      const UA_Variant *input, size_t outputSize,
                      UA_Variant *output)
{
    UA_NodeId conditionId;
    UA_StatusCode retval = UA_Server_getConditionBranchNodeId (
        server,
        objectId,
        (UA_ByteString *)input[0].data,
        &conditionId
    );
    CONDITION_ASSERT_GOTOLABEL(retval, "ConditionId based on EventId not found",done);
    UA_LocalizedText *comment = (UA_LocalizedText *)input[1].data;
    retval = UA_Server_conditionConfirm(server, &conditionId, *comment);
    done:
    UA_NodeId_clear(&conditionId);
    return retval;
}


static UA_StatusCode
resetMethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                    void *sessionContext, const UA_NodeId *methodId,
                    void *methodContext, const UA_NodeId *objectId,
                    void *objectContext, size_t inputSize,
                    const UA_Variant *input, size_t outputSize,
                    UA_Variant *output)
{
    return UA_STATUSCODE_BADNOTIMPLEMENTED;
}

static UA_StatusCode
reset2MethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                     void *sessionContext, const UA_NodeId *methodId,
                     void *methodContext, const UA_NodeId *objectId,
                     void *objectContext, size_t inputSize,
                     const UA_Variant *input, size_t outputSize,
                     UA_Variant *output)
{
    //TODO
    return UA_STATUSCODE_BADNOTIMPLEMENTED;
}

static UA_StatusCode
suppressMethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                       void *sessionContext, const UA_NodeId *methodId,
                       void *methodContext, const UA_NodeId *objectId,
                       void *objectContext, size_t inputSize,
                       const UA_Variant *input, size_t outputSize,
                       UA_Variant *output)
{
    //TODO
    return UA_STATUSCODE_BADNOTIMPLEMENTED;
}

static UA_StatusCode
suppress2MethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                        void *sessionContext, const UA_NodeId *methodId,
                        void *methodContext, const UA_NodeId *objectId,
                        void *objectContext, size_t inputSize,
                        const UA_Variant *input, size_t outputSize,
                        UA_Variant *output)
{
    //TODO
    return UA_STATUSCODE_BADNOTIMPLEMENTED;
}

static UA_StatusCode
unsuppressMethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                         void *sessionContext, const UA_NodeId *methodId,
                         void *methodContext, const UA_NodeId *objectId,
                         void *objectContext, size_t inputSize,
                         const UA_Variant *input, size_t outputSize,
                         UA_Variant *output)
{
    //TODO
    return UA_STATUSCODE_BADNOTIMPLEMENTED;
}

static UA_StatusCode
unsuppress2MethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                          void *sessionContext, const UA_NodeId *methodId,
                          void *methodContext, const UA_NodeId *objectId,
                          void *objectContext, size_t inputSize,
                          const UA_Variant *input, size_t outputSize,
                          UA_Variant *output)
{
    //TODO
    return UA_STATUSCODE_BADNOTIMPLEMENTED;
}

static UA_StatusCode
placeInServiceMethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                             void *sessionContext, const UA_NodeId *methodId,
                             void *methodContext, const UA_NodeId *objectId,
                             void *objectContext, size_t inputSize,
                             const UA_Variant *input, size_t outputSize,
                             UA_Variant *output)
{
    //TODO
    return UA_STATUSCODE_BADNOTIMPLEMENTED;
}

static UA_StatusCode
placeInService2MethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                              void *sessionContext, const UA_NodeId *methodId,
                              void *methodContext, const UA_NodeId *objectId,
                              void *objectContext, size_t inputSize,
                              const UA_Variant *input, size_t outputSize,
                              UA_Variant *output)
{
    //TODO
    return UA_STATUSCODE_BADNOTIMPLEMENTED;
}

static UA_StatusCode
removeFromServiceMethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                                void *sessionContext, const UA_NodeId *methodId,
                                void *methodContext, const UA_NodeId *objectId,
                                void *objectContext, size_t inputSize,
                                const UA_Variant *input, size_t outputSize,
                                UA_Variant *output)
{
    //TODO
    return UA_STATUSCODE_BADNOTIMPLEMENTED;
}

static UA_StatusCode
removeFromService2MethodCallback(UA_Server *server, const UA_NodeId *sessionId,
                                 void *sessionContext, const UA_NodeId *methodId,
                                 void *methodContext, const UA_NodeId *objectId,
                                 void *objectContext, size_t inputSize,
                                 const UA_Variant *input, size_t outputSize,
                                 UA_Variant *output)
{
    //TODO
    return UA_STATUSCODE_BADNOTIMPLEMENTED;
}

static UA_StatusCode UA_Server_setConditionNodesSetupFn (UA_Server *server, const UA_NodeId *typeId, UA_ConditionTypeSetupNodesFn fn)
{
    const UA_Node *node = UA_NODESTORE_GET (server, typeId);
    //TODO check if subtype of condition type
    if (node->head.nodeClass != UA_NODECLASS_OBJECTTYPE) return UA_STATUSCODE_BADNODECLASSINVALID;
    UA_ObjectTypeNode *object_node = (UA_ObjectTypeNode *) (uintptr_t) node;
    object_node->_conditionNodeSetupFn = fn;
    UA_NODESTORE_RELEASE(server, node);
    return UA_STATUSCODE_GOOD;
}

struct UA_ConditionTypeMetaData
{
    UA_NodeId typeId;
    UA_ConditionTypeSetupNodesFn nodeSetup;
};

void initNs0ConditionAndAlarms (UA_Server *server)
{
    /* Set callbacks for Method Fields of a condition. The current implementation
     * references methods without copying them when creating objects. So the
     * callbacks will be attached to the methods of the conditionType. */
    UA_NodeId methodId[] = {
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_CONDITIONTYPE_DISABLE}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_CONDITIONTYPE_ENABLE}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_CONDITIONTYPE_ADDCOMMENT}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_CONDITIONTYPE_CONDITIONREFRESH}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_CONDITIONTYPE_CONDITIONREFRESH2}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_ACKNOWLEDGE}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_CONFIRM}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_ALARMCONDITIONTYPE_RESET}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_ALARMCONDITIONTYPE_RESET2}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_ALARMCONDITIONTYPE_SUPPRESS}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_ALARMCONDITIONTYPE_SUPPRESS2}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_ALARMCONDITIONTYPE_UNSUPPRESS}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_ALARMCONDITIONTYPE_UNSUPPRESS}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_ALARMCONDITIONTYPE_PLACEINSERVICE}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_ALARMCONDITIONTYPE_PLACEINSERVICE2}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_ALARMCONDITIONTYPE_REMOVEFROMSERVICE}},
        {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_ALARMCONDITIONTYPE_REMOVEFROMSERVICE2}}
    };

    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    retval |= setMethodNode_callback(server, methodId[0], disableMethodCallback);
    retval |= setMethodNode_callback(server, methodId[1], enableMethodCallback);
    retval |= setMethodNode_callback(server, methodId[2], addCommentMethodCallback);
    retval |= setMethodNode_callback(server, methodId[3], refreshMethodCallback);
    retval |= setMethodNode_callback(server, methodId[4], refresh2MethodCallback);
    retval |= setMethodNode_callback(server, methodId[5], acknowledgeMethodCallback);
    retval |= setMethodNode_callback(server, methodId[6], confirmMethodCallback);
    retval |= setMethodNode_callback(server, methodId[7], resetMethodCallback);
    retval |= setMethodNode_callback(server, methodId[8], reset2MethodCallback);
    retval |= setMethodNode_callback(server, methodId[9], suppressMethodCallback);
    retval |= setMethodNode_callback(server, methodId[10], suppress2MethodCallback);
    retval |= setMethodNode_callback(server, methodId[11], unsuppressMethodCallback);
    retval |= setMethodNode_callback(server, methodId[12], unsuppress2MethodCallback);
    retval |= setMethodNode_callback(server, methodId[13], placeInServiceMethodCallback);
    retval |= setMethodNode_callback(server, methodId[14], placeInService2MethodCallback);
    retval |= setMethodNode_callback(server, methodId[15], removeFromServiceMethodCallback);
    retval |= setMethodNode_callback(server, methodId[16], removeFromService2MethodCallback);

    // Create RefreshEvents
    if(UA_NodeId_isNull(&server->refreshEvents[REFRESHEVENT_START_IDX]) &&
       UA_NodeId_isNull(&server->refreshEvents[REFRESHEVENT_END_IDX])) {

        UA_NodeId refreshStartEventTypeNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_REFRESHSTARTEVENTTYPE);
        UA_NodeId refreshEndEventTypeNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_REFRESHENDEVENTTYPE);

        /* Create RefreshStartEvent */
        retval = createEvent(server, refreshStartEventTypeNodeId, &server->refreshEvents[REFRESHEVENT_START_IDX]);
        if (retval != UA_STATUSCODE_GOOD)
        {
            UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,
                         "CreateEvent RefreshStart failed. StatusCode %s", UA_StatusCode_name(retval));
        }

        /* Create RefreshEndEvent */
        retval = createEvent(server, refreshEndEventTypeNodeId, &server->refreshEvents[REFRESHEVENT_END_IDX]);
        if (retval != UA_STATUSCODE_GOOD)
        {
            UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_USERLAND,
                         "CreateEvent RefreshEnd failed. StatusCode %s", UA_StatusCode_name(retval));
        }
    }

    /* change Refresh Events IsAbstract = false
     * so abstract Events : RefreshStart and RefreshEnd could be created */
    UA_NodeId refreshStartEventTypeNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_REFRESHSTARTEVENTTYPE);
    UA_NodeId refreshEndEventTypeNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_REFRESHENDEVENTTYPE);

    UA_Boolean startAbstract = false;
    UA_Boolean endAbstract = false;
    readWithReadValue(server, &refreshStartEventTypeNodeId,
                      UA_ATTRIBUTEID_ISABSTRACT, &startAbstract);
    readWithReadValue(server, &refreshEndEventTypeNodeId,
                      UA_ATTRIBUTEID_ISABSTRACT, &endAbstract);

    UA_Boolean inner = (startAbstract == false && endAbstract == false);
    if(inner) {
        writeIsAbstractAttribute(server, refreshStartEventTypeNodeId, false);
        writeIsAbstractAttribute(server, refreshEndEventTypeNodeId, false);
    }

    const struct UA_ConditionTypeMetaData conditions[] = {
        {
            .typeId = {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE}},
            .nodeSetup = (UA_ConditionTypeSetupNodesFn) UA_Server_setupAcknowledgeableConditionNodes
        },
        {
            .typeId = {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_ALARMCONDITIONTYPE}},
            .nodeSetup = (UA_ConditionTypeSetupNodesFn) UA_Server_setupAlarmConditionNodes
        },
        {
            .typeId = {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_DISCREPANCYALARMTYPE}},
            .nodeSetup = (UA_ConditionTypeSetupNodesFn)UA_Server_setupDiscrepancyAlarmNodes
        },
        {
            .typeId = {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_OFFNORMALALARMTYPE}},
            .nodeSetup = (UA_ConditionTypeSetupNodesFn) UA_Server_setupOffNormalAlarmNodes
        },
        {
            .typeId = {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_CERTIFICATEEXPIRATIONALARMTYPE}},
            .nodeSetup = (UA_ConditionTypeSetupNodesFn) UA_Server_setupCertificateExpirationAlarmNodes
        },
        {
            .typeId = {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_LIMITALARMTYPE}},
            .nodeSetup = (UA_ConditionTypeSetupNodesFn) UA_Server_setupLimitAlarmNodes
        },
        {
            .typeId = {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_EXCLUSIVEDEVIATIONALARMTYPE}},
            .nodeSetup = (UA_ConditionTypeSetupNodesFn) UA_Server_setupExclusiveDeviationAlarmNodes
        },
        {
            .typeId = {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_NONEXCLUSIVEDEVIATIONALARMTYPE}},
            .nodeSetup = (UA_ConditionTypeSetupNodesFn) UA_Server_setupNonExclusiveDeviationAlarmNodes
        },
        {
            .typeId = {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_EXCLUSIVERATEOFCHANGEALARMTYPE}},
            .nodeSetup = (UA_ConditionTypeSetupNodesFn) UA_Server_setupExclusiveRateOfChangeAlarmNodes
        },
        {
            .typeId = {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_NONEXCLUSIVERATEOFCHANGEALARMTYPE}},
            .nodeSetup = (UA_ConditionTypeSetupNodesFn) UA_Server_setupNonExclusiveRateOfChangeAlarmNodes
        }
    };
    for (size_t i=0; i<sizeof(conditions)/sizeof(conditions[0]); i++)
    {
        if (UA_Server_setConditionNodesSetupFn (server, &conditions[i].typeId, conditions[i].nodeSetup) != UA_STATUSCODE_GOOD)
        {
            UA_LOG_ERROR(server->config.logging, UA_LOGCATEGORY_SERVER,
                         "Error encountered when setting up the server's alarms and conditions");
        }
    }

}

#endif /* UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS */
