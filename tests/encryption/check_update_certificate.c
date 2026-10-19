/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 *    Copyright 2024 (c) Fraunhofer IOSB (Author: Noel Graf)
 */

#include <open62541/client_config_default.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/certificategroup_default.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/plugin/create_certificate.h>

#include "ua_server_internal.h"

#include <check.h>
#include "test_helpers.h"
#include "certificates.h"

#if defined(__linux__) || defined(UA_ARCHITECTURE_WIN32)
#include "mp_printf.h"
#define TEST_PATH_MAX 256
#endif /* defined(__linux__) || defined(UA_ARCHITECTURE_WIN32) */

UA_Server *server;

static void setup(void) {
    /* Load certificate and private key */
    UA_ByteString certificate;
    certificate.length = CERT_DER_LENGTH;
    certificate.data = CERT_DER_DATA;

    UA_ByteString privateKey;
    privateKey.length = KEY_DER_LENGTH;
    privateKey.data = KEY_DER_DATA;

    server = UA_Server_newForUnitTestWithSecurityPolicies(4840, &certificate, &privateKey,
                                                          NULL, 0, NULL, 0, NULL, 0);
    ck_assert(server != NULL);
}

#if defined(__linux__) || defined(UA_ARCHITECTURE_WIN32)
static void setup2(void) {
    /* Load certificate and private key */
    UA_ByteString certificate;
    certificate.length = CERT_DER_LENGTH;
    certificate.data = CERT_DER_DATA;

    UA_ByteString privateKey;
    privateKey.length = KEY_DER_LENGTH;
    privateKey.data = KEY_DER_DATA;

    char storePathDir[TEST_PATH_MAX];
    getcwd(storePathDir, TEST_PATH_MAX - 4);
    mp_snprintf(storePathDir, TEST_PATH_MAX, "%s/pki", storePathDir);

    const UA_String storePath = UA_STRING(storePathDir);
    server =
        UA_Server_newForUnitTestWithSecurityPolicies_Filestore(4840, &certificate,
                                                               &privateKey, storePath);
    ck_assert(server != NULL);
}
#endif /* defined(__linux__) || defined(UA_ARCHITECTURE_WIN32) */

static void teardown(void) {
    UA_Server_delete(server);
}

static void generateCertificate(UA_ByteString *certificate, UA_ByteString *privKey) {
    UA_String subject[3] = {UA_STRING_STATIC("C=DE"),
                            UA_STRING_STATIC("O=SampleOrganization"),
                            UA_STRING_STATIC("CN=Open62541Server@localhost")};
    UA_UInt32 lenSubject = 3;
    UA_String subjectAltName[2]= {
        UA_STRING_STATIC("DNS:localhost"),
        UA_STRING_STATIC("URI:urn:open62541.unconfigured.application")
    };
    UA_UInt32 lenSubjectAltName = 2;
    UA_KeyValueMap *kvm = UA_KeyValueMap_new();
    UA_UInt16 expiresIn = 14;
    UA_KeyValueMap_setScalar(kvm, UA_QUALIFIEDNAME(0, "expires-in-days"),
                             (void *)&expiresIn, &UA_TYPES[UA_TYPES_UINT16]);
    UA_UInt16 keyLength = 2048;
    UA_KeyValueMap_setScalar(kvm, UA_QUALIFIEDNAME(0, "key-size-bits"),
                             (void *)&keyLength, &UA_TYPES[UA_TYPES_UINT16]);
    UA_CreateCertificate(UA_Log_Stdout, subject, lenSubject, subjectAltName,
                         lenSubjectAltName, UA_CERTIFICATEFORMAT_DER, kvm,
                         privKey, certificate);

    UA_KeyValueMap_delete(kvm);
}

START_TEST(update_certificate) {
    UA_ByteString newCertificate = UA_BYTESTRING_NULL;
    UA_ByteString newPrivateKey = UA_BYTESTRING_NULL;

    generateCertificate(&newCertificate, &newPrivateKey);

    UA_NodeId defaultApplicationGroup = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVERCONFIGURATION_CERTIFICATEGROUPS_DEFAULTAPPLICATIONGROUP);
    UA_NodeId certTypRsaSha256 = UA_NODEID_NUMERIC(0, UA_NS0ID_RSASHA256APPLICATIONCERTIFICATETYPE);

    UA_StatusCode retval =
            UA_Server_updateCertificate(server, defaultApplicationGroup, certTypRsaSha256,
                               newCertificate, &newPrivateKey);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_ByteString_clear(&newCertificate);
    UA_ByteString_clear(&newPrivateKey);
}
END_TEST

START_TEST(update_certificate_wrongKey) {
    UA_ByteString newCertificate = UA_BYTESTRING_NULL;
    UA_ByteString newPrivateKey = UA_BYTESTRING_NULL;

    generateCertificate(&newCertificate, &newPrivateKey);

    UA_ByteString wrongPrivateKey;
    wrongPrivateKey.length = KEY_DER_LENGTH;
    wrongPrivateKey.data = KEY_DER_DATA;

    UA_NodeId defaultApplicationGroup = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVERCONFIGURATION_CERTIFICATEGROUPS_DEFAULTAPPLICATIONGROUP);
    UA_NodeId certTypRsaSha256 = UA_NODEID_NUMERIC(0, UA_NS0ID_RSASHA256APPLICATIONCERTIFICATETYPE);

    UA_StatusCode retval =
            UA_Server_updateCertificate(server, defaultApplicationGroup, certTypRsaSha256,
                                        newCertificate, &wrongPrivateKey);
    ck_assert_uint_eq(retval, UA_STATUSCODE_BADNOTSUPPORTED);

    UA_ByteString_clear(&newCertificate);
    UA_ByteString_clear(&newPrivateKey);
}
END_TEST

START_TEST(update_certificate_noKey) {
    UA_ByteString newCertificate;
    newCertificate.length = CERT_DER_LENGTH;
    newCertificate.data = CERT_DER_DATA;

    UA_NodeId defaultApplicationGroup = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVERCONFIGURATION_CERTIFICATEGROUPS_DEFAULTAPPLICATIONGROUP);
    UA_NodeId certTypRsaSha256 = UA_NODEID_NUMERIC(0, UA_NS0ID_RSASHA256APPLICATIONCERTIFICATETYPE);

    UA_StatusCode retval =
            UA_Server_updateCertificate(server, defaultApplicationGroup, certTypRsaSha256,
                                        newCertificate, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
}
END_TEST

START_TEST(update_certificate_noKey_invalid) {
    UA_ByteString newCertificate = UA_BYTESTRING("not a certificate");

    UA_NodeId defaultApplicationGroup = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVERCONFIGURATION_CERTIFICATEGROUPS_DEFAULTAPPLICATIONGROUP);
    UA_NodeId certTypRsaSha256 = UA_NODEID_NUMERIC(0, UA_NS0ID_RSASHA256APPLICATIONCERTIFICATETYPE);

    UA_StatusCode retval =
            UA_Server_updateCertificate(server, defaultApplicationGroup, certTypRsaSha256,
                                        newCertificate, NULL);
    ck_assert_uint_eq(retval, UA_STATUSCODE_BADCERTIFICATEINVALID);
}
END_TEST

#if defined(UA_ENABLE_IOTECH_MODIFICATIONS) && defined(UA_ENABLE_ENCRYPTION_MBEDTLS) && \
    !defined(UA_ENABLE_GDS_PUSHMANAGEMENT)
static UA_SecurityPolicy *
getPolicy(const char *uriSuffix) {
    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_String suffix = UA_STRING((char*)(uintptr_t)uriSuffix);
    for(size_t i = 0; i < config->securityPoliciesSize; i++) {
        UA_String *uri = &config->securityPolicies[i].policyUri;
        if(uri->length >= suffix.length &&
           memcmp(uri->data + uri->length - suffix.length, suffix.data, suffix.length) == 0)
            return &config->securityPolicies[i];
    }
    return NULL;
}

/* The endpoints of a SecurityPolicy advertise its certificate. The endpoints
 * are only set once a certificate is replaced. So the endpoints of the
 * SecurityPolicies that hold the replaced certificate must be set. */
static void
assertEndpointsMatchPolicies(const UA_ByteString *replaced) {
    UA_ServerConfig *config = UA_Server_getConfig(server);
    for(size_t i = 0; i < config->endpointsSize; i++) {
        UA_EndpointDescription *ed = &config->endpoints[i];
        UA_SecurityPolicy *sp = getSecurityPolicyByUri(server, &ed->securityPolicyUri);
        ck_assert_ptr_ne(sp, NULL);
        if(replaced && UA_ByteString_equal(&sp->localCertificate, replaced))
            ck_assert_uint_gt(ed->serverCertificate.length, 0);
        if(ed->serverCertificate.length > 0)
            ck_assert(UA_ByteString_equal(&ed->serverCertificate, &sp->localCertificate));
    }
}

/* The private key of a SecurityPolicy pairs with its certificate: A signature
 * made with the local private key verifies against the local certificate */
static UA_Boolean
policyKeyMatchesCertificate(UA_SecurityPolicy *sp) {
    void *ctx = NULL;
    if(sp->newChannelContext(sp, &sp->localCertificate, &ctx) != UA_STATUSCODE_GOOD)
        return false;
    UA_ByteString message = UA_BYTESTRING("message to sign");
    UA_ByteString signature = UA_BYTESTRING_NULL;
    UA_SecurityPolicySignatureAlgorithm *sig = &sp->asymSignatureAlgorithm;
    UA_StatusCode res =
        UA_ByteString_allocBuffer(&signature, sig->getLocalSignatureSize(sp, ctx));
    if(res == UA_STATUSCODE_GOOD)
        res = sig->sign(sp, ctx, &message, &signature);
    if(res == UA_STATUSCODE_GOOD)
        res = sig->verify(sp, ctx, &message, &signature);
    UA_ByteString_clear(&signature);
    sp->deleteChannelContext(sp, ctx);
    return res == UA_STATUSCODE_GOOD;
}

static void
assertPolicyKeysMatchCertificates(void) {
    UA_ServerConfig *config = UA_Server_getConfig(server);
    for(size_t i = 0; i < config->securityPoliciesSize; i++) {
        UA_SecurityPolicy *sp = &config->securityPolicies[i];
        if(sp->policyType == UA_SECURITYPOLICYTYPE_NONE)
            continue;
        ck_assert_msg(policyKeyMatchesCertificate(sp),
                      "Private key does not match the certificate of %.*s",
                      (int)sp->policyUri.length, sp->policyUri.data);
    }
}

START_TEST(replace_certificate) {
    UA_ByteString oldCertificate = {CERT_DER_LENGTH, CERT_DER_DATA};
    UA_ByteString newCertificate = UA_BYTESTRING_NULL;
    UA_ByteString newPrivateKey = UA_BYTESTRING_NULL;
    generateCertificate(&newCertificate, &newPrivateKey);

    UA_StatusCode retval =
        UA_Server_replaceCertificate(server, &oldCertificate, &newCertificate,
                                     &newPrivateKey, true, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    /* Every SecurityPolicy with a certificate used the old one, incl. #None */
    UA_ServerConfig *config = UA_Server_getConfig(server);
    for(size_t i = 0; i < config->securityPoliciesSize; i++) {
        UA_SecurityPolicy *sp = &config->securityPolicies[i];
        if(sp->localCertificate.length > 0)
            ck_assert(UA_ByteString_equal(&sp->localCertificate, &newCertificate));
    }
    assertEndpointsMatchPolicies(&newCertificate);

    UA_ByteString_clear(&newCertificate);
    UA_ByteString_clear(&newPrivateKey);
}
END_TEST

START_TEST(replace_certificate_unique_per_policy) {
    UA_ByteString initialCertificate = {CERT_DER_LENGTH, CERT_DER_DATA};
    UA_ByteString certA = UA_BYTESTRING_NULL, keyA = UA_BYTESTRING_NULL;
    UA_ByteString certB = UA_BYTESTRING_NULL, keyB = UA_BYTESTRING_NULL;
    UA_ByteString certC = UA_BYTESTRING_NULL, keyC = UA_BYTESTRING_NULL;
    generateCertificate(&certA, &keyA);
    generateCertificate(&certB, &keyB);
    generateCertificate(&certC, &keyC);

    /* Basic256Sha256 gets its own certificate. Other SecurityPolicies of the
     * same CertificateType keep the initial certificate. */
    UA_SecurityPolicy *basic256Sha256 = getPolicy("#Basic256Sha256");
    UA_SecurityPolicy *aes128 = getPolicy("#Aes128_Sha256_RsaOaep");
    UA_SecurityPolicy *none = getPolicy("#None");
    ck_assert_ptr_ne(basic256Sha256, NULL);
    ck_assert_ptr_ne(aes128, NULL);
    ck_assert_ptr_ne(none, NULL);
    ck_assert_uint_gt(none->localCertificate.length, 0);
    ck_assert_uint_eq(basic256Sha256->updateCertificate(basic256Sha256, certA, keyA),
                      UA_STATUSCODE_GOOD);

    /* Replacing the initial certificate leaves Basic256Sha256 unchanged */
    UA_StatusCode retval =
        UA_Server_replaceCertificate(server, &initialCertificate, &certB, &keyB, true, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert(UA_ByteString_equal(&basic256Sha256->localCertificate, &certA));
    ck_assert(UA_ByteString_equal(&aes128->localCertificate, &certB));
    ck_assert(UA_ByteString_equal(&none->localCertificate, &certB));
    assertEndpointsMatchPolicies(&certB);

    /* Replacing the Basic256Sha256 certificate only changes Basic256Sha256 */
    retval = UA_Server_replaceCertificate(server, &certA, &certC, &keyC, true, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert(UA_ByteString_equal(&basic256Sha256->localCertificate, &certC));
    ck_assert(UA_ByteString_equal(&aes128->localCertificate, &certB));
    ck_assert(UA_ByteString_equal(&none->localCertificate, &certB));
    assertEndpointsMatchPolicies(&certC);

    UA_ByteString_clear(&certA);
    UA_ByteString_clear(&keyA);
    UA_ByteString_clear(&certB);
    UA_ByteString_clear(&keyB);
    UA_ByteString_clear(&certC);
    UA_ByteString_clear(&keyC);
}
END_TEST

START_TEST(replace_certificate_notFound) {
    UA_ByteString unknownCertificate = UA_BYTESTRING_NULL;
    UA_ByteString unknownKey = UA_BYTESTRING_NULL;
    UA_ByteString newCertificate = UA_BYTESTRING_NULL;
    UA_ByteString newPrivateKey = UA_BYTESTRING_NULL;
    generateCertificate(&unknownCertificate, &unknownKey);
    generateCertificate(&newCertificate, &newPrivateKey);

    UA_StatusCode retval =
        UA_Server_replaceCertificate(server, &unknownCertificate, &newCertificate,
                                     &newPrivateKey, true, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_BADNOTFOUND);

    UA_ByteString initialCertificate = {CERT_DER_LENGTH, CERT_DER_DATA};
    UA_ServerConfig *config = UA_Server_getConfig(server);
    for(size_t i = 0; i < config->securityPoliciesSize; i++) {
        UA_SecurityPolicy *sp = &config->securityPolicies[i];
        if(sp->localCertificate.length > 0)
            ck_assert(UA_ByteString_equal(&sp->localCertificate, &initialCertificate));
    }

    UA_ByteString_clear(&unknownCertificate);
    UA_ByteString_clear(&unknownKey);
    UA_ByteString_clear(&newCertificate);
    UA_ByteString_clear(&newPrivateKey);
}
END_TEST

/* Encode a DER certificate as PEM with 64 character lines */
static void
derToPem(const UA_ByteString *der, UA_ByteString *pem) {
    UA_String b64 = UA_STRING_NULL;
    ck_assert_uint_eq(UA_ByteString_toBase64(der, &b64), UA_STATUSCODE_GOOD);
    const char *begin = "-----BEGIN CERTIFICATE-----\n";
    const char *end = "-----END CERTIFICATE-----\n";
    size_t lines = (b64.length + 63) / 64;
    ck_assert_uint_eq(UA_ByteString_allocBuffer(pem, strlen(begin) + b64.length +
                                                lines + strlen(end)),
                      UA_STATUSCODE_GOOD);
    size_t pos = 0;
    memcpy(pem->data, begin, strlen(begin));
    pos += strlen(begin);
    for(size_t i = 0; i < b64.length; i += 64) {
        size_t len = (b64.length - i < 64) ? b64.length - i : 64;
        memcpy(pem->data + pos, b64.data + i, len);
        pos += len;
        pem->data[pos++] = '\n';
    }
    memcpy(pem->data + pos, end, strlen(end));
    UA_String_clear(&b64);
}

START_TEST(replace_certificate_noKey) {
    /* Without a private key, the SecurityPolicies keep their key. The new
     * certificate (here the PEM form of the old one) has the same key pair. */
    UA_ByteString oldCertificate = {CERT_DER_LENGTH, CERT_DER_DATA};
    UA_ByteString newCertificate = UA_BYTESTRING_NULL;
    derToPem(&oldCertificate, &newCertificate);
    assertPolicyKeysMatchCertificates();

    UA_StatusCode retval =
        UA_Server_replaceCertificate(server, &oldCertificate, &newCertificate,
                                     NULL, true, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    /* The SecurityPolicies hold the certificate as DER */
    UA_ServerConfig *config = UA_Server_getConfig(server);
    for(size_t i = 0; i < config->securityPoliciesSize; i++) {
        UA_SecurityPolicy *sp = &config->securityPolicies[i];
        if(sp->localCertificate.length > 0)
            ck_assert_msg(UA_ByteString_equal(&sp->localCertificate, &oldCertificate),
                          "%.*s holds a certificate of %lu bytes, expected %lu",
                          (int)sp->policyUri.length, sp->policyUri.data,
                          (unsigned long)sp->localCertificate.length,
                          (unsigned long)oldCertificate.length);
    }
    assertEndpointsMatchPolicies(&oldCertificate);
    assertPolicyKeysMatchCertificates();

    UA_ByteString_clear(&newCertificate);
}
END_TEST

START_TEST(replace_certificate_noKey_otherKeyPair) {
    /* Without a private key, the new certificate must have the key pair of the
     * old certificate. Otherwise the SecurityPolicies cannot use it. */
    UA_ByteString oldCertificate = {CERT_DER_LENGTH, CERT_DER_DATA};
    UA_ByteString newCertificate = UA_BYTESTRING_NULL;
    UA_ByteString newPrivateKey = UA_BYTESTRING_NULL;
    generateCertificate(&newCertificate, &newPrivateKey);

    UA_StatusCode retval =
        UA_Server_replaceCertificate(server, &oldCertificate, &newCertificate,
                                     NULL, true, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_BADNOTSUPPORTED);

    /* The SecurityPolicies are unchanged and can still sign */
    UA_ServerConfig *config = UA_Server_getConfig(server);
    for(size_t i = 0; i < config->securityPoliciesSize; i++) {
        UA_SecurityPolicy *sp = &config->securityPolicies[i];
        if(sp->localCertificate.length > 0)
            ck_assert(UA_ByteString_equal(&sp->localCertificate, &oldCertificate));
    }
    assertEndpointsMatchPolicies(NULL);
    assertPolicyKeysMatchCertificates();

    UA_ByteString_clear(&newCertificate);
    UA_ByteString_clear(&newPrivateKey);
}
END_TEST

/* Iterate the server and the clients without blocking */
static void
iterate(UA_Client **clients, size_t clientsSize) {
    UA_Server_run_iterate(server, false);
    for(size_t i = 0; i < clientsSize; i++)
        UA_Client_run_iterate(clients[i], 0);
}

/* Number of open SecureChannels on the server that use the SecurityPolicy */
static size_t
openChannels(const UA_SecurityPolicy *sp) {
    size_t count = 0;
    UA_SecureChannel *channel;
    TAILQ_FOREACH(channel, &server->channels, serverEntry) {
        if(channel->securityPolicy == sp && channel->state == UA_SECURECHANNELSTATE_OPEN)
            count++;
    }
    return count;
}

/* Connect a client with a SecureChannel using the SecurityPolicy. With
 * withSession, also wait for the Session to be activated. */
static UA_Client *
connectClient(const UA_SecurityPolicy *sp, UA_Boolean withSession) {
    UA_ByteString certificate = {CERT_DER_LENGTH, CERT_DER_DATA};
    UA_ByteString privateKey = {KEY_DER_LENGTH, KEY_DER_DATA};
    UA_Client *client = UA_Client_newForUnitTest();
    ck_assert_ptr_ne(client, NULL);
    UA_ClientConfig *cc = UA_Client_getConfig(client);
    UA_ClientConfig_setDefaultEncryption(cc, certificate, privateKey,
                                         NULL, 0, NULL, 0);
    UA_CertificateGroup_AcceptAll(&cc->certificateVerification);
    cc->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
    UA_String_copy(&sp->policyUri, &cc->securityPolicyUri);
    const char *url = "opc.tcp://localhost:4840";
    ck_assert_uint_eq(withSession ? UA_Client_connectAsync(client, url) :
                      UA_Client_connectSecureChannelAsync(client, url),
                      UA_STATUSCODE_GOOD);

    /* The client first gets the endpoints over a #None SecureChannel. Then it
     * reconnects with the selected endpoint. */
    size_t before = openChannels(sp);
    UA_DateTime deadline = UA_DateTime_nowMonotonic() + 5 * UA_DATETIME_SEC;
    while(openChannels(sp) == before && UA_DateTime_nowMonotonic() < deadline)
        iterate(&client, 1);
    ck_assert_uint_eq(openChannels(sp), before + 1);

    UA_SessionState sessionState = UA_SESSIONSTATE_CLOSED;
    while(withSession && sessionState != UA_SESSIONSTATE_ACTIVATED &&
          UA_DateTime_nowMonotonic() < deadline) {
        iterate(&client, 1);
        UA_Client_getState(client, NULL, &sessionState, NULL);
    }
    if(withSession)
        ck_assert_uint_eq(sessionState, UA_SESSIONSTATE_ACTIVATED);
    return client;
}

START_TEST(replace_certificate_closes_channels_of_policy) {
    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_CertificateGroup_AcceptAll(&config->secureChannelPKI);
    UA_CertificateGroup_AcceptAll(&config->sessionPKI);

    /* Basic256Sha256 gets its own certificate. Aes128_Sha256_RsaOaep keeps the
     * initial certificate. */
    UA_ByteString initialCertificate = {CERT_DER_LENGTH, CERT_DER_DATA};
    UA_ByteString initialKey = {KEY_DER_LENGTH, KEY_DER_DATA};
    UA_ByteString certA = UA_BYTESTRING_NULL, keyA = UA_BYTESTRING_NULL;
    generateCertificate(&certA, &keyA);
    ck_assert_uint_eq(UA_Server_replaceCertificate(server, &initialCertificate, &certA,
                                                   &keyA, false, false),
                      UA_STATUSCODE_GOOD);
    UA_SecurityPolicy *basic256Sha256 = getPolicy("#Basic256Sha256");
    UA_SecurityPolicy *aes128 = getPolicy("#Aes128_Sha256_RsaOaep");
    ck_assert_ptr_ne(basic256Sha256, NULL);
    ck_assert_ptr_ne(aes128, NULL);
    for(size_t i = 0; i < config->securityPoliciesSize; i++) {
        UA_SecurityPolicy *sp = &config->securityPolicies[i];
        if(sp != basic256Sha256 && sp->localCertificate.length > 0)
            ck_assert_uint_eq(sp->updateCertificate(sp, initialCertificate, initialKey),
                              UA_STATUSCODE_GOOD);
    }
    for(size_t i = 0; i < config->endpointsSize; i++) {
        UA_EndpointDescription *ed = &config->endpoints[i];
        UA_SecurityPolicy *sp = getSecurityPolicyByUri(server, &ed->securityPolicyUri);
        UA_ByteString_clear(&ed->serverCertificate);
        UA_ByteString_copy(&sp->localCertificate, &ed->serverCertificate);
    }

    ck_assert_uint_eq(UA_Server_run_startup(server), UA_STATUSCODE_GOOD);
    UA_Client *clients[2];
    clients[0] = connectClient(basic256Sha256, false);
    clients[1] = connectClient(aes128, false);

    /* Replace the Basic256Sha256 certificate with the certificate that
     * Aes128_Sha256_RsaOaep already uses. Only the SecureChannel of
     * Basic256Sha256 is closed. */
    ck_assert_uint_eq(UA_Server_replaceCertificate(server, &certA, &initialCertificate,
                                                   &initialKey, false, true),
                      UA_STATUSCODE_GOOD);
    for(size_t i = 0; i < 10; i++)
        UA_Server_run_iterate(server, false);
    ck_assert_uint_eq(openChannels(basic256Sha256), 0);
    ck_assert_uint_eq(openChannels(aes128), 1);

    for(size_t i = 0; i < 2; i++) {
        UA_Client_disconnectAsync(clients[i]);
        for(size_t j = 0; j < 100; j++)
            iterate(&clients[i], 1);
        UA_Client_delete(clients[i]);
    }
    UA_Server_run_shutdown(server);
    UA_ByteString_clear(&certA);
    UA_ByteString_clear(&keyA);
}
END_TEST

/* Give #None its own certificate and replace it with newCertificate. Only
 * #None is changed. */
static void
replaceNoneCertificate(const UA_ByteString *newCertificate) {
    UA_ByteString initialCertificate = {CERT_DER_LENGTH, CERT_DER_DATA};
    UA_ByteString noneCertificate = UA_BYTESTRING_NULL;
    UA_ByteString noneKey = UA_BYTESTRING_NULL;
    generateCertificate(&noneCertificate, &noneKey);
    UA_SecurityPolicy *none = getPolicy("#None");
    ck_assert_ptr_ne(none, NULL);
    ck_assert_uint_eq(none->updateCertificate(none, noneCertificate, UA_BYTESTRING_NULL),
                      UA_STATUSCODE_GOOD);

    /* #None has no private key. A certificate with another key pair is
     * accepted without a private key. */
    UA_StatusCode retval =
        UA_Server_replaceCertificate(server, &noneCertificate, newCertificate,
                                     NULL, true, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_ServerConfig *config = UA_Server_getConfig(server);
    for(size_t i = 0; i < config->securityPoliciesSize; i++) {
        UA_SecurityPolicy *sp = &config->securityPolicies[i];
        ck_assert(UA_ByteString_equal(&sp->localCertificate, sp == none ?
                                      newCertificate : &initialCertificate));
    }
    assertEndpointsMatchPolicies(newCertificate);
    assertPolicyKeysMatchCertificates();

    UA_ByteString_clear(&noneCertificate);
    UA_ByteString_clear(&noneKey);
}

START_TEST(replace_certificate_none_only) {
    UA_ByteString newCertificate = UA_BYTESTRING_NULL;
    UA_ByteString newPrivateKey = UA_BYTESTRING_NULL;
    generateCertificate(&newCertificate, &newPrivateKey);
    replaceNoneCertificate(&newCertificate);
    UA_ByteString_clear(&newCertificate);
    UA_ByteString_clear(&newPrivateKey);
}
END_TEST

START_TEST(replace_certificate_none_only_ecc) {
    /* #None does no crypto, so it also accepts a non-RSA certificate */
    UA_ByteString newCertificate = {CERT_P384_DER_LENGTH, CERT_P384_DER_DATA};
    replaceNoneCertificate(&newCertificate);
}
END_TEST

START_TEST(replace_certificate_notRsa) {
    /* An ECC certificate is rejected before any SecurityPolicy is modified */
    UA_ByteString oldCertificate = {CERT_DER_LENGTH, CERT_DER_DATA};
    UA_ByteString newCertificate = {CERT_P384_DER_LENGTH, CERT_P384_DER_DATA};
    UA_ByteString newPrivateKey = {KEY_P384_DER_LENGTH, KEY_P384_DER_DATA};
    UA_StatusCode retval =
        UA_Server_replaceCertificate(server, &oldCertificate, &newCertificate,
                                     &newPrivateKey, true, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_BADCERTIFICATEINVALID);

    UA_ServerConfig *config = UA_Server_getConfig(server);
    for(size_t i = 0; i < config->securityPoliciesSize; i++)
        ck_assert(UA_ByteString_equal(&config->securityPolicies[i].localCertificate,
                                      &oldCertificate));
    assertPolicyKeysMatchCertificates();
}
END_TEST

static UA_StatusCode
updateCertificate_fail(UA_SecurityPolicy *policy, const UA_ByteString certificate,
                       const UA_ByteString privateKey) {
    return UA_STATUSCODE_BADINTERNALERROR;
}

/* Number of Sessions on SecureChannels that use the SecurityPolicy */
static size_t
sessionsOfPolicy(const UA_SecurityPolicy *sp) {
    size_t count = 0;
    session_list_entry *sentry;
    LIST_FOREACH(sentry, &server->sessions, pointers) {
        if(sentry->session.channel && sentry->session.channel->securityPolicy == sp)
            count++;
    }
    return count;
}

START_TEST(replace_certificate_closes_sessions) {
    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_CertificateGroup_AcceptAll(&config->secureChannelPKI);
    UA_CertificateGroup_AcceptAll(&config->sessionPKI);
    UA_String_clear(&config->applicationDescription.applicationUri);
    config->applicationDescription.applicationUri =
        UA_STRING_ALLOC("urn:unconfigured:application");

    UA_SecurityPolicy *aes128 = getPolicy("#Aes128_Sha256_RsaOaep");
    ck_assert_ptr_ne(aes128, NULL);
    ck_assert_uint_eq(UA_Server_run_startup(server), UA_STATUSCODE_GOOD);
    UA_Client *client = connectClient(aes128, true);
    ck_assert_uint_eq(sessionsOfPolicy(aes128), 1);

    UA_ByteString oldCertificate = {CERT_DER_LENGTH, CERT_DER_DATA};
    UA_ByteString newCertificate = UA_BYTESTRING_NULL;
    UA_ByteString newPrivateKey = UA_BYTESTRING_NULL;
    generateCertificate(&newCertificate, &newPrivateKey);

    /* The certificate cannot be replaced. The failed SecurityPolicy is no
     * longer usable, so the Session is closed. */
    UA_StatusCode (*updateCertificate)(UA_SecurityPolicy *, const UA_ByteString,
                                       const UA_ByteString) = aes128->updateCertificate;
    aes128->updateCertificate = updateCertificate_fail;
    UA_StatusCode retval =
        UA_Server_replaceCertificate(server, &oldCertificate, &newCertificate,
                                     &newPrivateKey, true, false);
    aes128->updateCertificate = updateCertificate;
    ck_assert_uint_eq(retval, UA_STATUSCODE_BADINTERNALERROR);
    ck_assert_uint_eq(sessionsOfPolicy(aes128), 0);

    /* The stub did not modify the SecurityPolicy, so a new Session can be
     * opened with the old certificate */
    UA_Client_disconnectAsync(client);
    for(size_t i = 0; i < 100; i++)
        iterate(&client, 1);
    UA_Client_delete(client);
    client = connectClient(aes128, true);
    ck_assert_uint_eq(sessionsOfPolicy(aes128), 1);

    /* The certificate is replaced. The Session is closed. */
    retval = UA_Server_replaceCertificate(server, &oldCertificate, &newCertificate,
                                          &newPrivateKey, true, false);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(sessionsOfPolicy(aes128), 0);

    UA_Client_disconnectAsync(client);
    for(size_t i = 0; i < 100; i++)
        iterate(&client, 1);
    UA_Client_delete(client);
    UA_Server_run_shutdown(server);
    UA_ByteString_clear(&newCertificate);
    UA_ByteString_clear(&newPrivateKey);
}
END_TEST

START_TEST(replace_certificate_wrongKey) {
    UA_ByteString oldCertificate = {CERT_DER_LENGTH, CERT_DER_DATA};
    UA_ByteString newCertificate = UA_BYTESTRING_NULL, newPrivateKey = UA_BYTESTRING_NULL;
    UA_ByteString otherCertificate = UA_BYTESTRING_NULL, otherKey = UA_BYTESTRING_NULL;
    generateCertificate(&newCertificate, &newPrivateKey);
    generateCertificate(&otherCertificate, &otherKey);

    UA_StatusCode retval =
        UA_Server_replaceCertificate(server, &oldCertificate, &newCertificate,
                                     &otherKey, true, true);
    ck_assert_uint_eq(retval, UA_STATUSCODE_BADNOTSUPPORTED);

    UA_ByteString_clear(&newCertificate);
    UA_ByteString_clear(&newPrivateKey);
    UA_ByteString_clear(&otherCertificate);
    UA_ByteString_clear(&otherKey);
}
END_TEST

START_TEST(replace_certificate_partialFailure) {
    UA_ByteString oldCertificate = {CERT_DER_LENGTH, CERT_DER_DATA};
    UA_ByteString newCertificate = UA_BYTESTRING_NULL;
    UA_ByteString newPrivateKey = UA_BYTESTRING_NULL;
    generateCertificate(&newCertificate, &newPrivateKey);

    /* The update of Aes256_Sha256_RsaPss fails. The SecurityPolicies before it
     * are updated, the ones after it are not attempted. */
    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_SecurityPolicy *failedPolicy = getPolicy("#Aes256_Sha256_RsaPss");
    ck_assert_ptr_ne(failedPolicy, NULL);
    size_t failed = (size_t)(failedPolicy - config->securityPolicies);
    ck_assert_uint_gt(failed, 0);
    ck_assert_uint_lt(failed + 1, config->securityPoliciesSize);
    UA_StatusCode (*updateCertificate)(UA_SecurityPolicy *, const UA_ByteString,
                                       const UA_ByteString) =
        failedPolicy->updateCertificate;
    failedPolicy->updateCertificate = updateCertificate_fail;

    UA_StatusCode retval =
        UA_Server_replaceCertificate(server, &oldCertificate, &newCertificate,
                                     &newPrivateKey, false, false);
    failedPolicy->updateCertificate = updateCertificate;
    ck_assert_uint_eq(retval, UA_STATUSCODE_BADINTERNALERROR);

    for(size_t i = 0; i < config->securityPoliciesSize; i++) {
        UA_SecurityPolicy *sp = &config->securityPolicies[i];
        ck_assert(UA_ByteString_equal(&sp->localCertificate,
                                      i < failed ? &newCertificate : &oldCertificate));
    }

    /* The endpoints of the updated and of the failed SecurityPolicy advertise
     * the current certificate of their SecurityPolicy */
    assertEndpointsMatchPolicies(&newCertificate);
    for(size_t i = 0; i < config->endpointsSize; i++) {
        UA_EndpointDescription *ed = &config->endpoints[i];
        UA_SecurityPolicy *sp = getSecurityPolicyByUri(server, &ed->securityPolicyUri);
        if((size_t)(sp - config->securityPolicies) <= failed)
            ck_assert_uint_gt(ed->serverCertificate.length, 0);
    }

    UA_ByteString_clear(&newCertificate);
    UA_ByteString_clear(&newPrivateKey);
}
END_TEST
#endif /* UA_ENABLE_IOTECH_MODIFICATIONS && UA_ENABLE_ENCRYPTION_MBEDTLS &&
        * !UA_ENABLE_GDS_PUSHMANAGEMENT */

static Suite* testSuite_create_certificate(void) {
    Suite *s = suite_create("Update Certificate");
    TCase *tc_cert = tcase_create("Update Certificate");
    tcase_add_checked_fixture(tc_cert, setup, teardown);
#ifdef UA_ENABLE_ENCRYPTION
    tcase_add_test(tc_cert, update_certificate);
    tcase_add_test(tc_cert, update_certificate_wrongKey);
    tcase_add_test(tc_cert, update_certificate_noKey);
    tcase_add_test(tc_cert, update_certificate_noKey_invalid);
#if defined(UA_ENABLE_IOTECH_MODIFICATIONS) && defined(UA_ENABLE_ENCRYPTION_MBEDTLS) && \
    !defined(UA_ENABLE_GDS_PUSHMANAGEMENT)
    tcase_add_test(tc_cert, replace_certificate);
    tcase_add_test(tc_cert, replace_certificate_unique_per_policy);
    tcase_add_test(tc_cert, replace_certificate_notFound);
    tcase_add_test(tc_cert, replace_certificate_noKey);
    tcase_add_test(tc_cert, replace_certificate_noKey_otherKeyPair);
    tcase_add_test(tc_cert, replace_certificate_closes_channels_of_policy);
    tcase_add_test(tc_cert, replace_certificate_closes_sessions);
    tcase_add_test(tc_cert, replace_certificate_wrongKey);
    tcase_add_test(tc_cert, replace_certificate_partialFailure);
    tcase_add_test(tc_cert, replace_certificate_notRsa);
    tcase_add_test(tc_cert, replace_certificate_none_only);
    tcase_add_test(tc_cert, replace_certificate_none_only_ecc);
#endif
#endif /* UA_ENABLE_ENCRYPTION */
    suite_add_tcase(s,tc_cert);

#if defined(__linux__) || defined(UA_ARCHITECTURE_WIN32)
    TCase *tc_cert_filestore = tcase_create("Update Certificate Filestore");
    tcase_add_checked_fixture(tc_cert_filestore, setup2, teardown);
#ifdef UA_ENABLE_ENCRYPTION
    tcase_add_test(tc_cert_filestore, update_certificate);
    tcase_add_test(tc_cert_filestore, update_certificate_wrongKey);
    tcase_add_test(tc_cert_filestore, update_certificate_noKey);
    tcase_add_test(tc_cert_filestore, update_certificate_noKey_invalid);
#if defined(UA_ENABLE_IOTECH_MODIFICATIONS) && defined(UA_ENABLE_ENCRYPTION_MBEDTLS) && \
    !defined(UA_ENABLE_GDS_PUSHMANAGEMENT)
    tcase_add_test(tc_cert_filestore, replace_certificate);
    tcase_add_test(tc_cert_filestore, replace_certificate_unique_per_policy);
    tcase_add_test(tc_cert_filestore, replace_certificate_notFound);
    tcase_add_test(tc_cert_filestore, replace_certificate_noKey);
    tcase_add_test(tc_cert_filestore, replace_certificate_noKey_otherKeyPair);
    tcase_add_test(tc_cert_filestore, replace_certificate_closes_channels_of_policy);
    tcase_add_test(tc_cert_filestore, replace_certificate_closes_sessions);
    tcase_add_test(tc_cert_filestore, replace_certificate_wrongKey);
    tcase_add_test(tc_cert_filestore, replace_certificate_partialFailure);
    tcase_add_test(tc_cert_filestore, replace_certificate_notRsa);
    tcase_add_test(tc_cert_filestore, replace_certificate_none_only);
    tcase_add_test(tc_cert_filestore, replace_certificate_none_only_ecc);
#endif
#endif /* UA_ENABLE_ENCRYPTION */
    suite_add_tcase(s,tc_cert_filestore);
#endif /* defined(__linux__) || defined(UA_ARCHITECTURE_WIN32) */

    return s;
}

int main(void) {
    Suite *s = testSuite_create_certificate();
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr,CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
