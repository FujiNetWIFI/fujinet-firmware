/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2011-2013 by Aris Adamantiadis
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the SSH Library; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include "config.h"
#include "libssh/session.h"
#include "libssh/ecdh.h"
#include "libssh/dh.h"
#include "libssh/buffer.h"
#include "libssh/ssh2.h"
#include "libssh/pki.h"
#include "libssh/bignum.h"

#ifdef HAVE_ECDH
#include <openssl/ecdh.h>
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
#define NISTP256 NID_X9_62_prime256v1
#define NISTP384 NID_secp384r1
#define NISTP521 NID_secp521r1
#else
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/core_names.h>
#include "libcrypto-compat.h"
#endif /* OPENSSL_VERSION_NUMBER */

/** @internal
 * @brief Map the given key exchange enum value to its curve name.
 */
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
static int ecdh_kex_type_to_curve(enum ssh_key_exchange_e kex_type) {
#else
static const char *ecdh_kex_type_to_curve(enum ssh_key_exchange_e kex_type) {
#endif /* OPENSSL_VERSION_NUMBER */
    if (kex_type == SSH_KEX_ECDH_SHA2_NISTP256) {
        return NISTP256;
    } else if (kex_type == SSH_KEX_ECDH_SHA2_NISTP384) {
        return NISTP384;
    } else if (kex_type == SSH_KEX_ECDH_SHA2_NISTP521) {
        return NISTP521;
    }
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
    return SSH_ERROR;
#else
    return NULL;
#endif
}

/** @internal
 * @brief Starts ecdh-sha2-nistp256 key exchange
 */
int ssh_client_ecdh_init(ssh_session session){
  int rc;
  ssh_string client_pubkey;
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
  EC_KEY *key;
  const EC_GROUP *group;
  const EC_POINT *pubkey;
  int curve;
  int len;
  bignum_CTX ctx = BN_CTX_new();
  if (ctx == NULL) {
      return SSH_ERROR;
  }
#else
  const char *curve = NULL;
  EVP_PKEY *key = NULL;
  OSSL_PARAM *out_params = NULL;
  const OSSL_PARAM *pubkey_param = NULL;
  const uint8_t *pubkey = NULL;
  size_t pubkey_len;
#endif /* OPENSSL_VERSION_NUMBER */

  rc = ssh_buffer_add_u8(session->out_buffer, SSH2_MSG_KEX_ECDH_INIT);
  if (rc < 0) {
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
      BN_CTX_free(ctx);
#endif /* OPENSSL_VERSION_NUMBER */
      return SSH_ERROR;
  }

  curve = ecdh_kex_type_to_curve(session->next_crypto->kex_type);
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
  if (curve == SSH_ERROR) {
      BN_CTX_free(ctx);
      return SSH_ERROR;
  }

  key = EC_KEY_new_by_curve_name(curve);
#else
  if (curve == NULL) {
      return SSH_ERROR;
  }

  key = EVP_EC_gen(curve);
#endif /* OPENSSL_VERSION_NUMBER */

  if (key == NULL) {
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
      BN_CTX_free(ctx);
#endif /* OPENSSL_VERSION_NUMBER */
      return SSH_ERROR;
  }

/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
  group = EC_KEY_get0_group(key);

  EC_KEY_generate_key(key);

  pubkey=EC_KEY_get0_public_key(key);
  len = EC_POINT_point2oct(group,pubkey,POINT_CONVERSION_UNCOMPRESSED,
      NULL,0,ctx);

  client_pubkey = ssh_string_new(len);
  if (client_pubkey == NULL) {
      BN_CTX_free(ctx);
      EC_KEY_free(key);
      return SSH_ERROR;
  }

  EC_POINT_point2oct(group,pubkey,POINT_CONVERSION_UNCOMPRESSED,
      ssh_string_data(client_pubkey),len,ctx);
  BN_CTX_free(ctx);
#else
  rc = EVP_PKEY_todata(key, EVP_PKEY_PUBLIC_KEY, &out_params);
  if (rc != 1) {
      EVP_PKEY_free(key);
      return SSH_ERROR;
  }

  pubkey_param = OSSL_PARAM_locate_const(out_params, OSSL_PKEY_PARAM_PUB_KEY);
  if (pubkey_param == NULL) {
      EVP_PKEY_free(key);
      OSSL_PARAM_free(out_params);
      return SSH_ERROR;
  }

  rc = OSSL_PARAM_get_octet_string_ptr(pubkey_param,
                                       (const void**)&pubkey,
                                       &pubkey_len);
  if (rc != 1) {
      OSSL_PARAM_free(out_params);
      EVP_PKEY_free(key);
      return SSH_ERROR;
  }

  client_pubkey = ssh_string_new(pubkey_len);
  if (client_pubkey == NULL) {
      OSSL_PARAM_free(out_params);
      EVP_PKEY_free(key);
      return SSH_ERROR;
  }

  memcpy(ssh_string_data(client_pubkey), pubkey, pubkey_len);
  OSSL_PARAM_free(out_params);
#endif /* OPENSSL_VERSION_NUMBER */

  rc = ssh_buffer_add_ssh_string(session->out_buffer,client_pubkey);
  if (rc < 0) {
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
      EC_KEY_free(key);
#else
      EVP_PKEY_free(key);
#endif /* OPENSSL_VERSION_NUMBER */
      SSH_STRING_FREE(client_pubkey);
      return SSH_ERROR;
  }

  session->next_crypto->ecdh_privkey = key;
  session->next_crypto->ecdh_client_pubkey = client_pubkey;

  /* register the packet callbacks */
  ssh_packet_set_callbacks(session, &ssh_ecdh_client_callbacks);
  session->dh_handshake_state = DH_STATE_INIT_SENT;

  rc = ssh_packet_send(session);

  return rc;
}

int ecdh_build_k(ssh_session session) {
  struct ssh_crypto_struct *next_crypto = session->next_crypto;
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
 #if 1
  const EC_GROUP *group = EC_KEY_get0_group(next_crypto->ecdh_privkey);
  EC_POINT *pubkey;
  void *buffer;
  int rc;
  int len = (EC_GROUP_get_degree(group) + 7) / 8;
  bignum_CTX ctx = bignum_ctx_new();
  if (ctx == NULL) {
    return -1;
  }
  pubkey = EC_POINT_new(group);
  if (pubkey == NULL) {
    bignum_ctx_free(ctx);
    return -1;
  }

  if (session->server) {
      rc = EC_POINT_oct2point(group,
                              pubkey,
                              ssh_string_data(next_crypto->ecdh_client_pubkey),
                              ssh_string_len(next_crypto->ecdh_client_pubkey),
                              ctx);
  } else {
      rc = EC_POINT_oct2point(group,
                              pubkey,
                              ssh_string_data(next_crypto->ecdh_server_pubkey),
                              ssh_string_len(next_crypto->ecdh_server_pubkey),
                              ctx);
  }
  bignum_ctx_free(ctx);
  if (rc <= 0) {
      EC_POINT_clear_free(pubkey);
      return -1;
  }

  buffer = malloc(len);
  if (buffer == NULL) {
      EC_POINT_clear_free(pubkey);
      return -1;
  }

  rc = ECDH_compute_key(buffer,
                        len,
                        pubkey,
                        next_crypto->ecdh_privkey,
                        NULL);
  EC_POINT_clear_free(pubkey);
  if (rc <= 0) {
      free(buffer);
      return -1;
  }

  bignum_bin2bn(buffer, len, &next_crypto->shared_secret);
  free(buffer);
#else
  EVP_PKEY *pubkey = NULL;
  void *secret = NULL;
  size_t secret_len;
  int rc;
  OSSL_PARAM params[2];
  EVP_PKEY_CTX *dh_ctx = EVP_PKEY_CTX_new_from_pkey(NULL,
                                                    next_crypto->ecdh_privkey,
                                                    NULL);
  EVP_PKEY_CTX *pubkey_ctx = EVP_PKEY_CTX_new_from_pkey(NULL,
                                                        next_crypto->ecdh_privkey,
                                                        NULL);
  if (dh_ctx == NULL || pubkey_ctx == NULL) {
      EVP_PKEY_CTX_free(dh_ctx);
      EVP_PKEY_CTX_free(pubkey_ctx);
      return -1;
  }

  rc = EVP_PKEY_derive_init(dh_ctx);
  if (rc != 1) {
      EVP_PKEY_CTX_free(dh_ctx);
      EVP_PKEY_CTX_free(pubkey_ctx);
      return -1;
  }

  rc = EVP_PKEY_fromdata_init(pubkey_ctx);
  if (rc != 1) {
      EVP_PKEY_CTX_free(dh_ctx);
      EVP_PKEY_CTX_free(pubkey_ctx);
      return -1;
  }

  if (session->server) {
      params[0] = OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY,
                              ssh_string_data(next_crypto->ecdh_client_pubkey),
                              ssh_string_len(next_crypto->ecdh_client_pubkey));
  } else {
      params[0] = OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY,
                              ssh_string_data(next_crypto->ecdh_server_pubkey),
                              ssh_string_len(next_crypto->ecdh_server_pubkey));
  }
  params[1] = OSSL_PARAM_construct_end();

  rc = EVP_PKEY_fromdata(pubkey_ctx, &pubkey, EVP_PKEY_PUBLIC_KEY, params);
  if (rc != 1) {
      EVP_PKEY_CTX_free(dh_ctx);
      EVP_PKEY_CTX_free(pubkey_ctx);
      return -1;
  }

  EVP_PKEY_CTX_free(pubkey_ctx);

  rc = EVP_PKEY_derive_set_peer(dh_ctx, pubkey);
  if (rc != 1) {
      EVP_PKEY_CTX_free(dh_ctx);
      return -1;
  }

  /* get the max length of the secret */
  rc = EVP_PKEY_derive(dh_ctx, NULL, &secret_len);
  if (rc != 1) {
      EVP_PKEY_CTX_free(dh_ctx);
      return -1;
  }

  secret = malloc(secret_len);
  if (secret == NULL) {
      EVP_PKEY_CTX_free(dh_ctx);
      return -1;
  }

  rc = EVP_PKEY_derive(dh_ctx, secret, &secret_len);
  if (rc != 1) {
      EVP_PKEY_CTX_free(dh_ctx);
      return -1;
  }

  EVP_PKEY_CTX_free(dh_ctx);

  bignum_bin2bn(secret, secret_len, &next_crypto->shared_secret);
  free(secret);
#endif /* OPENSSL_VERSION_NUMBER */
  if (next_crypto->shared_secret == NULL) {
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
      EC_KEY_free(next_crypto->ecdh_privkey);
#else
      EVP_PKEY_free(next_crypto->ecdh_privkey);
#endif /* OPENSSL_VERSION_NUMBER */
      next_crypto->ecdh_privkey = NULL;
      return -1;
  }
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
  EC_KEY_free(next_crypto->ecdh_privkey);
#else
  EVP_PKEY_free(next_crypto->ecdh_privkey);
#endif /* OPENSSL_VERSION_NUMBER */
  next_crypto->ecdh_privkey = NULL;

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("Session server cookie",
                   next_crypto->server_kex.cookie, 16);
    ssh_log_hexdump("Session client cookie",
                   next_crypto->client_kex.cookie, 16);
    ssh_print_bignum("Shared secret key", next_crypto->shared_secret);
#endif /* DEBUG_CRYPTO */

  return 0;
}

#ifdef WITH_SERVER

/** @brief Handle a SSH_MSG_KEXDH_INIT packet (server) and send a
 * SSH_MSG_KEXDH_REPLY
 */
SSH_PACKET_CALLBACK(ssh_packet_server_ecdh_init){
    /* ECDH keys */
    ssh_string q_c_string;
    ssh_string q_s_string;
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
    EC_KEY *ecdh_key;
    const EC_GROUP *group;
    const EC_POINT *ecdh_pubkey;
    bignum_CTX ctx;
    int curve;
    int len;
#else
    EVP_PKEY *ecdh_key = NULL;
    const void *pubkey_ptr = NULL;
    size_t len;
    OSSL_PARAM *params = NULL;
    const OSSL_PARAM *pubkey = NULL;
    const char *curve = NULL;
#endif /* OPENSSL_VERSION_NUMBER */
    /* SSH host keys (rsa,dsa,ecdsa) */
    ssh_key privkey;
    enum ssh_digest_e digest = SSH_DIGEST_AUTO;
    ssh_string sig_blob = NULL;
    ssh_string pubkey_blob = NULL;
    int rc;
    (void)type;
    (void)user;

    ssh_packet_remove_callbacks(session, &ssh_ecdh_server_callbacks);
    /* Extract the client pubkey from the init packet */
    q_c_string = ssh_buffer_get_ssh_string(packet);
    if (q_c_string == NULL) {
        ssh_set_error(session,SSH_FATAL, "No Q_C ECC point in packet");
        goto error;
    }
    session->next_crypto->ecdh_client_pubkey = q_c_string;

    curve = ecdh_kex_type_to_curve(session->next_crypto->kex_type);
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
    if (curve == SSH_ERROR) {
        return SSH_ERROR;
    }
#else
    if (curve == NULL) {
        return SSH_ERROR;
    }
#endif /* OPENSSL_VERSION_NUMBER */

/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
    ecdh_key = EC_KEY_new_by_curve_name(curve);
#else
    ecdh_key = EVP_EC_gen(curve);
#endif /* OPENSSL_VERSION_NUMBER */
    if (ecdh_key == NULL) {
        ssh_set_error_oom(session);
        goto error;
    }

/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
    /* Build server's keypair */
    ctx = BN_CTX_new();
    if (ctx == NULL) {
        EC_KEY_free(ecdh_key);
        return SSH_ERROR;
    }

    group = EC_KEY_get0_group(ecdh_key);
    EC_KEY_generate_key(ecdh_key);

    ecdh_pubkey = EC_KEY_get0_public_key(ecdh_key);
    len = EC_POINT_point2oct(group,
                             ecdh_pubkey,
                             POINT_CONVERSION_UNCOMPRESSED,
                             NULL,
                             0,
                             ctx);
#else
    rc = EVP_PKEY_todata(ecdh_key, EVP_PKEY_PUBLIC_KEY, &params);
    if (rc != 1) {
        EVP_PKEY_free(ecdh_key);
        return SSH_ERROR;
    }

    pubkey = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_PUB_KEY);
    if (pubkey == NULL) {
        OSSL_PARAM_free(params);
        EVP_PKEY_free(ecdh_key);
        return SSH_ERROR;
    }

    rc = OSSL_PARAM_get_octet_string_ptr(pubkey, &pubkey_ptr, &len);
    if (rc != 1) {
        OSSL_PARAM_free(params);
        EVP_PKEY_free(ecdh_key);
        return SSH_ERROR;
    }
#endif /* OPENSSL_VERSION_NUMBER */
    q_s_string = ssh_string_new(len);
    if (q_s_string == NULL) {
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
        EC_KEY_free(ecdh_key);
        BN_CTX_free(ctx);
#else
        EVP_PKEY_free(ecdh_key);
#endif /* OPENSSL_VERSION_NUMBER */
        goto error;
    }

/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
    EC_POINT_point2oct(group,
                       ecdh_pubkey,
                       POINT_CONVERSION_UNCOMPRESSED,
                       ssh_string_data(q_s_string),
                       len,
                       ctx);
    BN_CTX_free(ctx);
#else
    if (memcpy(ssh_string_data(q_s_string), pubkey_ptr, len)) {
        OSSL_PARAM_free(params);
        EVP_PKEY_free(ecdh_key);
        return SSH_ERROR;
    }

    OSSL_PARAM_free(params);
#endif /* OPENSSL_VERSION_NUMBER */

    session->next_crypto->ecdh_privkey = ecdh_key;
    session->next_crypto->ecdh_server_pubkey = q_s_string;

    /* build k and session_id */
    rc = ecdh_build_k(session);
    if (rc < 0) {
        ssh_set_error(session, SSH_FATAL, "Cannot build k number");
        goto error;
    }

    /* privkey is not allocated */
    rc = ssh_get_key_params(session, &privkey, &digest);
    if (rc == SSH_ERROR) {
        goto error;
    }

    rc = ssh_make_sessionid(session);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Could not create a session id");
        goto error;
    }

    sig_blob = ssh_srv_pki_do_sign_sessionid(session, privkey, digest);
    if (sig_blob == NULL) {
        ssh_set_error(session, SSH_FATAL, "Could not sign the session id");
        goto error;
    }

    rc = ssh_dh_get_next_server_publickey_blob(session, &pubkey_blob);
    if (rc != SSH_OK) {
        ssh_set_error(session, SSH_FATAL, "Could not export server public key");
        SSH_STRING_FREE(sig_blob);
        return SSH_ERROR;
    }

    rc = ssh_buffer_pack(session->out_buffer,
                         "bSSS",
                         SSH2_MSG_KEXDH_REPLY,
                         pubkey_blob, /* host's pubkey */
                         q_s_string, /* ecdh public key */
                         sig_blob); /* signature blob */

    SSH_STRING_FREE(sig_blob);
    SSH_STRING_FREE(pubkey_blob);

    if (rc != SSH_OK) {
        ssh_set_error_oom(session);
        goto error;
    }

    SSH_LOG(SSH_LOG_PROTOCOL, "SSH_MSG_KEXDH_REPLY sent");
    rc = ssh_packet_send(session);
    if (rc == SSH_ERROR) {
        goto error;
    }

    /* Send the MSG_NEWKEYS */
    rc = ssh_buffer_add_u8(session->out_buffer, SSH2_MSG_NEWKEYS);
    if (rc < 0) {
        goto error;
    }

    session->dh_handshake_state = DH_STATE_NEWKEYS_SENT;
    rc = ssh_packet_send(session);
    if (rc == SSH_ERROR){
        goto error;
    }
    SSH_LOG(SSH_LOG_PROTOCOL, "SSH_MSG_NEWKEYS sent");

    return SSH_PACKET_USED;
error:
    ssh_buffer_reinit(session->out_buffer);
    session->session_state = SSH_SESSION_STATE_ERROR;
    return SSH_PACKET_USED;
}

#endif /* WITH_SERVER */

#endif /* HAVE_ECDH */
