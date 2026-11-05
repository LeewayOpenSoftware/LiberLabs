/* SPDX-License-Identifier: GPL-2.0-or-later */
/* S0 local HTTPS test server (diagnostic): mbedTLS, tlspoc identity, one
 * simple HTTP/200 responder. Local test server only - no Internet. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>
#include "tlspoc_testcert.h"

int main(void)
{
    mbedtls_net_context listen_fd, client_fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt srvcert;
    mbedtls_pk_context pkey;
    const char *pers = "s0server";
    int ret;
    char buf[4096];
    static const char reply[] =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK";

    mbedtls_net_init(&listen_fd);
    mbedtls_net_init(&client_fd);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_x509_crt_init(&srvcert);
    mbedtls_pk_init(&pkey);

    printf("S0SERVER start\n"); fflush(stdout);

    ret = mbedtls_x509_crt_parse(&srvcert, (const unsigned char *)tlspoc_cert_pem,
                                 tlspoc_cert_pem_len + 1);
    if (ret)
    {
        printf("S0SERVER cert parse failed -%#x\n", -ret); fflush(stdout);
        return 1;
    }
    ret = mbedtls_pk_parse_key(&pkey, (const unsigned char *)tlspoc_key_pem,
                               tlspoc_key_pem_len + 1, NULL, 0, NULL, NULL);
    if (ret)
    {
        printf("S0SERVER key parse failed -%#x\n", -ret); fflush(stdout);
        return 1;
    }
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                (const unsigned char *)pers, strlen(pers));
    if (ret)
    {
        printf("S0SERVER drbg seed failed -%#x\n", -ret); fflush(stdout);
        return 1;
    }
    ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER,
                                      MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    ret = mbedtls_ssl_conf_own_cert(&conf, &srvcert, &pkey);
    if (ret)
    {
        printf("S0SERVER own_cert failed -%#x\n", -ret); fflush(stdout);
        return 1;
    }
    mbedtls_ssl_conf_renegotiation(&conf, MBEDTLS_SSL_RENEGOTIATION_DISABLED);

    ret = mbedtls_net_bind(&listen_fd, "0.0.0.0", "4443", MBEDTLS_NET_PROTO_TCP);
    if (ret)
    {
        printf("S0SERVER bind failed -%#x\n", -ret); fflush(stdout);
        return 1;
    }
    printf("S0SERVER listening on 0.0.0.0:4443\n"); fflush(stdout);

    for (;;)
    {
        ret = mbedtls_net_accept(&listen_fd, &client_fd, NULL, 0, NULL);
        if (ret)
        {
            printf("S0SERVER accept failed -%#x\n", -ret); fflush(stdout);
            Sleep(1000);
            continue;
        }
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_setup(&ssl, &conf);
        mbedtls_ssl_set_bio(&ssl, &client_fd, mbedtls_net_send, mbedtls_net_recv, NULL);
        ret = mbedtls_ssl_handshake(&ssl);
        if (ret == 0)
        {
            printf("S0SERVER handshake ok: %s %s\n",
                   mbedtls_ssl_get_version(&ssl), mbedtls_ssl_get_ciphersuite(&ssl));
            fflush(stdout);
            ret = mbedtls_ssl_read(&ssl, (unsigned char *)buf, sizeof(buf) - 1);
            if (ret > 0)
            {
                buf[ret] = 0;
                printf("S0SERVER got request %d bytes\n", ret);
                mbedtls_ssl_write(&ssl, (const unsigned char *)reply, sizeof(reply) - 1);
            }
            mbedtls_ssl_close_notify(&ssl);
        }
        else
        {
            printf("S0SERVER handshake failed -%#x\n", -ret); fflush(stdout);
        }
        mbedtls_ssl_free(&ssl);
        mbedtls_net_free(&client_fd);
    }
    return 0;
}
