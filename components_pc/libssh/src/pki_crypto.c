/*
 * pki_crypto.c - PKI infrastructure using OpenSSL
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2003-2009 by Aris Adamantiadis
 * Copyright (c) 2009-2013 by Andreas Schneider <asn@cryptomilk.org>
 * Copyright (c) 2019      by Sahana Prasad     <sahana@redhat.com>
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

#ifndef _PKI_CRYPTO_H
#define _PKI_CRYPTO_H

#include "config.h"

#include "libssh/priv.h"
#include "libcrypto-compat.h"

#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/engine.h>
#include <openssl/err.h>
#if OPENSSL_VERSION_NUMBER < 0x30000000L
#include <openssl/dsa.h>
#include <openssl/rsa.h>
#else
#include <openssl/params.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#endif /* OPENSSL_VERSION_NUMBER */

#ifdef HAVE_OPENSSL_EC_H
#include <openssl/ec.h>
#endif
#ifdef HAVE_OPENSSL_ECDSA_H
#include <openssl/ecdsa.h>
#endif

#include "libssh/libssh.h"
#include "libssh/buffer.h"
#include "libssh/session.h"
#include "libssh/pki.h"
#include "libssh/pki_priv.h"
#include "libssh/bignum.h"

struct pem_get_password_struct {
    ssh_auth_callback fn;
    void *data;
};

static int pem_get_password(char *buf, int size, int rwflag, void *userdata) {
    struct pem_get_password_struct *pgp = userdata;

    (void) rwflag; /* unused */

    if (buf == NULL) {
        return 0;
    }

    memset(buf, '\0', size);
    if (pgp) {
        int rc;

        rc = pgp->fn("Passphrase for private key:",
                     buf, size, 0, 0,
                     pgp->data);
        if (rc == 0) {
            return strlen(buf);
        }
    }

    return 0;
}

void pki_key_clean(ssh_key key)
{
    if (key == NULL)
        return;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    DSA_free(key->dsa);
    key->dsa = NULL;
    RSA_free(key->rsa);
    key->rsa = NULL;
#endif /* OPENSSL_VERSION_NUMBER */
#ifdef HAVE_OPENSSL_ECC
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * Move whole HAVE_OPENSSL_ECC into #if < 0x3 above
 */
#if 1
    EC_KEY_free(key->ecdsa);
    key->ecdsa = NULL;
#endif
#endif /* HAVE_OPENSSL_ECC */
    EVP_PKEY_free(key->key);
    key->key = NULL;
}

#ifdef HAVE_OPENSSL_ECC
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
static int pki_key_ecdsa_to_nid(EC_KEY *k)
{
    const EC_GROUP *g = EC_KEY_get0_group(k);
    int nid;

    nid = EC_GROUP_get_curve_name(g);
    if (nid) {
        return nid;
    }

    return -1;
}
#else
static int pki_key_ecdsa_to_nid(EVP_PKEY *k)
{
    char gname[25] = { 0 };
    int nid, rc;

    rc = EVP_PKEY_get_utf8_string_param(k, "group", gname, 25, NULL);
    if (rc != 1)
        return -1;

    if (strcmp(gname, NISTP256) == 0
        || strcmp(gname, "secp256r1") == 0
        || strcmp(gname, "prime256v1") == 0) {
        nid = NID_X9_62_prime256v1;
    } else if (strcmp(gname, NISTP384) == 0
               || strcmp(gname, "secp384r1") == 0) {
        nid = NID_secp384r1;
    } else if (strcmp(gname, NISTP521) == 0
               || strcmp(gname, "secp521r1") == 0) {
        nid = NID_secp521r1;
    } else
        return -1;

    return nid;
}
#endif /* OPENSSL_VERSION_NUMBER */

/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
static enum ssh_keytypes_e pki_key_ecdsa_to_key_type(EC_KEY *k)
#else
static enum ssh_keytypes_e pki_key_ecdsa_to_key_type(EVP_PKEY *k)
#endif /* OPENSSL_VERSION_NUMBER */
{
    int nid;

    nid = pki_key_ecdsa_to_nid(k);

    switch (nid) {
        case NID_X9_62_prime256v1:
            return SSH_KEYTYPE_ECDSA_P256;
        case NID_secp384r1:
            return SSH_KEYTYPE_ECDSA_P384;
        case NID_secp521r1:
            return SSH_KEYTYPE_ECDSA_P521;
        default:
            return SSH_KEYTYPE_UNKNOWN;
    }
}

const char *pki_key_ecdsa_nid_to_name(int nid)
{
    switch (nid) {
        case NID_X9_62_prime256v1:
            return "ecdsa-sha2-nistp256";
        case NID_secp384r1:
            return "ecdsa-sha2-nistp384";
        case NID_secp521r1:
            return "ecdsa-sha2-nistp521";
        default:
            break;
    }

    return "unknown";
}

static const char *pki_key_ecdsa_nid_to_char(int nid)
{
    switch (nid) {
        case NID_X9_62_prime256v1:
            return "nistp256";
        case NID_secp384r1:
            return "nistp384";
        case NID_secp521r1:
            return "nistp521";
        default:
            break;
    }

    return "unknown";
}

int pki_key_ecdsa_nid_from_name(const char *name)
{
    if (strcmp(name, "nistp256") == 0) {
        return NID_X9_62_prime256v1;
    } else if (strcmp(name, "nistp384") == 0) {
        return NID_secp384r1;
    } else if (strcmp(name, "nistp521") == 0) {
        return NID_secp521r1;
    }

    return -1;
}

/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
static ssh_string make_ecpoint_string(const EC_GROUP *g,
                                      const EC_POINT *p)
{
    ssh_string s;
    size_t len;

    len = EC_POINT_point2oct(g,
                             p,
                             POINT_CONVERSION_UNCOMPRESSED,
                             NULL,
                             0,
                             NULL);
    if (len == 0) {
        return NULL;
    }

    s = ssh_string_new(len);
    if (s == NULL) {
        return NULL;
    }

    len = EC_POINT_point2oct(g,
                             p,
                             POINT_CONVERSION_UNCOMPRESSED,
                             ssh_string_data(s),
                             ssh_string_len(s),
                             NULL);
    if (len != ssh_string_len(s)) {
        SSH_STRING_FREE(s);
        return NULL;
    }

    return s;
}
#endif /* OPENSSL_VERSION_NUMBER */

int pki_privkey_build_ecdsa(ssh_key key, int nid, ssh_string e, ssh_string exp)
{
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
    EC_POINT *p = NULL;
    const EC_GROUP *g = NULL;
    int ok;
    BIGNUM *bexp = NULL;
#else
    int rc;
    const BIGNUM *expb;
    const char *group_name = OSSL_EC_curve_nid2name(nid);
    OSSL_PARAM_BLD *param_bld = NULL;

    if (group_name == NULL) {
        return -1;
    }
    expb = ssh_make_string_bn(exp);
#endif /* OPENSSL_VERSION_NUMBER */

    key->ecdsa_nid = nid;
    key->type_c = pki_key_ecdsa_nid_to_name(nid);

/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
    key->ecdsa = EC_KEY_new_by_curve_name(key->ecdsa_nid);
    if (key->ecdsa == NULL) {
        return -1;
    }

    g = EC_KEY_get0_group(key->ecdsa);

    p = EC_POINT_new(g);
    if (p == NULL) {
        return -1;
    }

    ok = EC_POINT_oct2point(g,
                            p,
                            ssh_string_data(e),
                            ssh_string_len(e),
                            NULL);
    if (!ok) {
        EC_POINT_free(p);
        return -1;
    }

    /* EC_KEY_set_public_key duplicates p */
    ok = EC_KEY_set_public_key(key->ecdsa, p);
    EC_POINT_free(p);
    if (!ok) {
        return -1;
    }

    bexp = ssh_make_string_bn(exp);
    if (bexp == NULL) {
        EC_KEY_free(key->ecdsa);
        return -1;
    }
    /* EC_KEY_set_private_key duplicates exp */
    ok = EC_KEY_set_private_key(key->ecdsa, bexp);
    BN_free(bexp);
    if (!ok) {
        EC_KEY_free(key->ecdsa);
        return -1;
    }

    return 0;
#else
    param_bld = OSSL_PARAM_BLD_new();
    if (param_bld == NULL)
        goto err;

    rc = OSSL_PARAM_BLD_push_utf8_string(param_bld, OSSL_PKEY_PARAM_GROUP_NAME,
                                         group_name, strlen(group_name));
    if (rc != 1)
        goto err;
    rc = OSSL_PARAM_BLD_push_octet_string(param_bld, OSSL_PKEY_PARAM_PUB_KEY,
                                          ssh_string_data(e), ssh_string_len(e));
    if (rc != 1)
        goto err;
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_PRIV_KEY, expb);
    if (rc != 1)
        goto err;

    rc = evp_build_pkey("EC", param_bld, &(key->key), EVP_PKEY_KEYPAIR);
    OSSL_PARAM_BLD_free(param_bld);

    return rc;
err:
    OSSL_PARAM_BLD_free(param_bld);
    return -1;
#endif /* OPENSSL_VERSION_NUMBER */
}

int pki_pubkey_build_ecdsa(ssh_key key, int nid, ssh_string e)
{
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
    EC_POINT *p = NULL;
    const EC_GROUP *g = NULL;
    int ok;
#else
    int rc;
    const char *group_name = OSSL_EC_curve_nid2name(nid);
    OSSL_PARAM_BLD *param_bld;
#endif /* OPENSSL_VERSION_NUMBER */

    key->ecdsa_nid = nid;
    key->type_c = pki_key_ecdsa_nid_to_name(nid);

/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
 #if 1
    key->ecdsa = EC_KEY_new_by_curve_name(key->ecdsa_nid);
    if (key->ecdsa == NULL) {
        return -1;
    }

    g = EC_KEY_get0_group(key->ecdsa);

    p = EC_POINT_new(g);
    if (p == NULL) {
        return -1;
    }

    ok = EC_POINT_oct2point(g,
                            p,
                            ssh_string_data(e),
                            ssh_string_len(e),
                            NULL);
    if (!ok) {
        EC_POINT_free(p);
        return -1;
    }

    /* EC_KEY_set_public_key duplicates p */
    ok = EC_KEY_set_public_key(key->ecdsa, p);
    EC_POINT_free(p);
    if (!ok) {
        return -1;
    }

    return 0;
#else
    param_bld = OSSL_PARAM_BLD_new();
    if (param_bld == NULL)
        goto err;

    rc = OSSL_PARAM_BLD_push_utf8_string(param_bld, OSSL_PKEY_PARAM_GROUP_NAME,
                                         group_name, strlen(group_name));
    if (rc != 1)
        goto err;
    rc = OSSL_PARAM_BLD_push_octet_string(param_bld, OSSL_PKEY_PARAM_PUB_KEY,
                                          ssh_string_data(e), ssh_string_len(e));
    if (rc != 1)
        goto err;

    rc = evp_build_pkey("EC", param_bld, &(key->key), EVP_PKEY_PUBLIC_KEY);
    OSSL_PARAM_BLD_free(param_bld);

    return rc;
err:
    OSSL_PARAM_BLD_free(param_bld);
    return -1;
#endif /* OPENSSL_VERSION_NUMBER */
}
#endif /* HAVE_OPENSSL_ECC */

ssh_key pki_key_dup(const ssh_key key, int demote)
{
    ssh_key new = NULL;
    int rc;

    new = ssh_key_new();
    if (new == NULL) {
        return NULL;
    }

    new->type = key->type;
    new->type_c = key->type_c;
    if (demote) {
        new->flags = SSH_KEY_FLAG_PUBLIC;
    } else {
        new->flags = key->flags;
    }

    switch (key->type) {
    case SSH_KEYTYPE_DSS: {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        const BIGNUM *p = NULL, *q = NULL, *g = NULL,
          *pub_key = NULL, *priv_key = NULL;
        BIGNUM *np, *nq, *ng, *npub_key, *npriv_key;
        new->dsa = DSA_new();
        if (new->dsa == NULL) {
            goto fail;
        }

        /*
         * p        = public prime number
         * q        = public 160-bit subprime, q | p-1
         * g        = public generator of subgroup
         * pub_key  = public key y = g^x
         * priv_key = private key x
         */
        DSA_get0_pqg(key->dsa, &p, &q, &g);
        np = BN_dup(p);
        nq = BN_dup(q);
        ng = BN_dup(g);
        if (np == NULL || nq == NULL || ng == NULL) {
            BN_free(np);
            BN_free(nq);
            BN_free(ng);
            goto fail;
        }

        /* Memory management of np, nq and ng is transferred to DSA object */
        rc = DSA_set0_pqg(new->dsa, np, nq, ng);
        if (rc == 0) {
            BN_free(np);
            BN_free(nq);
            BN_free(ng);
            goto fail;
        }

        DSA_get0_key(key->dsa, &pub_key, &priv_key);
        npub_key = BN_dup(pub_key);
        if (npub_key == NULL) {
            goto fail;
        }

        /* Memory management of npubkey is transferred to DSA object */
        rc = DSA_set0_key(new->dsa, npub_key, NULL);
        if (rc == 0) {
            goto fail;
        }

        if (!demote && (key->flags & SSH_KEY_FLAG_PRIVATE)) {
            npriv_key = BN_dup(priv_key);
            if (npriv_key == NULL) {
                goto fail;
            }

            /* Memory management of npriv_key is transferred to DSA object */
            rc = DSA_set0_key(new->dsa, NULL, npriv_key);
            if (rc == 0) {
                goto fail;
            }
        }
#else
        rc = evp_dup_dsa_pkey(key, new, demote);
        if (rc != SSH_OK) {
            goto fail;
        }
#endif /* OPENSSL_VERSION_NUMBER */
        break;
    }
    case SSH_KEYTYPE_RSA:
    case SSH_KEYTYPE_RSA1: {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        const BIGNUM *n = NULL, *e = NULL, *d = NULL;
        BIGNUM *nn, *ne, *nd;
#endif /* OPENSSL_VERSION_NUMBER < 0x30000000L */
#ifdef WITH_PKCS11_URI
        /* Take the PKCS#11 keys as they are */
        if (key->flags & SSH_KEY_FLAG_PKCS11_URI && !demote) {
            rc = EVP_PKEY_up_ref(key->key);
            if (rc != 1) {
                goto fail;
            }
            new->key = key->key;
            return new;
        }
#endif /* WITH_PKCS11_URI */
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        new->rsa = RSA_new();
        if (new->rsa == NULL) {
            goto fail;
        }

        /*
         * n    = public modulus
         * e    = public exponent
         * d    = private exponent
         * p    = secret prime factor
         * q    = secret prime factor
         * dmp1 = d mod (p-1)
         * dmq1 = d mod (q-1)
         * iqmp = q^-1 mod p
         */
        RSA_get0_key(key->rsa, &n, &e, &d);
        nn = BN_dup(n);
        ne = BN_dup(e);
        if (nn == NULL || ne == NULL) {
            BN_free(nn);
            BN_free(ne);
            goto fail;
        }

        /* Memory management of nn and ne is transferred to RSA object */
        rc = RSA_set0_key(new->rsa, nn, ne, NULL);
        if (rc == 0) {
            BN_free(nn);
            BN_free(ne);
            goto fail;
        }

        if (!demote && (key->flags & SSH_KEY_FLAG_PRIVATE)) {
            const BIGNUM *p = NULL, *q = NULL, *dmp1 = NULL,
              *dmq1 = NULL, *iqmp = NULL;
            BIGNUM *np, *nq, *ndmp1, *ndmq1, *niqmp;

            nd = BN_dup(d);
            if (nd == NULL) {
                goto fail;
            }

            /* Memory management of nd is transferred to RSA object */
            rc = RSA_set0_key(new->rsa, NULL, NULL, nd);
            if (rc == 0) {
                goto fail;
            }

            /* p, q, dmp1, dmq1 and iqmp may be NULL in private keys, but the
             * RSA operations are much faster when these values are available.
             */
            RSA_get0_factors(key->rsa, &p, &q);
            if (p != NULL && q != NULL) { /* need to set both of them */
                np = BN_dup(p);
                nq = BN_dup(q);
                if (np == NULL || nq == NULL) {
                    BN_free(np);
                    BN_free(nq);
                    goto fail;
                }

                /* Memory management of np and nq is transferred to RSA object */
                rc = RSA_set0_factors(new->rsa, np, nq);
                if (rc == 0) {
                    BN_free(np);
                    BN_free(nq);
                    goto fail;
                }
            }

            RSA_get0_crt_params(key->rsa, &dmp1, &dmq1, &iqmp);
            if (dmp1 != NULL || dmq1 != NULL || iqmp != NULL) {
                ndmp1 = BN_dup(dmp1);
                ndmq1 = BN_dup(dmq1);
                niqmp = BN_dup(iqmp);
                if (ndmp1 == NULL || ndmq1 == NULL || niqmp == NULL) {
                    BN_free(ndmp1);
                    BN_free(ndmq1);
                    BN_free(niqmp);
                    goto fail;
                }

                /* Memory management of ndmp1, ndmq1 and niqmp is transferred
                 * to RSA object */
                rc = RSA_set0_crt_params(new->rsa, ndmp1, ndmq1, niqmp);
                if (rc == 0) {
                    BN_free(ndmp1);
                    BN_free(ndmq1);
                    BN_free(niqmp);
                    goto fail;
                }
            }
        }
#else
        rc = evp_dup_rsa_pkey(key, new, demote);
        if (rc != SSH_OK) {
            goto fail;
        }
#endif /* OPENSSL_VERSION_NUMBER */
        break;
    }
    case SSH_KEYTYPE_ECDSA_P256:
    case SSH_KEYTYPE_ECDSA_P384:
    case SSH_KEYTYPE_ECDSA_P521:
#ifdef HAVE_OPENSSL_ECC
        new->ecdsa_nid = key->ecdsa_nid;
#ifdef WITH_PKCS11_URI
        /* Take the PKCS#11 keys as they are */
        if (key->flags & SSH_KEY_FLAG_PKCS11_URI && !demote) {
            rc = EVP_PKEY_up_ref(key->key);
            if (rc != 1) {
                goto fail;
            }
            new->key = key->key;
            rc = EC_KEY_up_ref(key->ecdsa);
            if (rc != 1) {
                goto fail;
            }
            new->ecdsa = key->ecdsa;
            return new;
        }
#endif /* WITH_PKCS11_URI */
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
        /* privkey -> pubkey */
        if (demote && ssh_key_is_private(key)) {
            const EC_POINT *p;
            int ok;

            new->ecdsa = EC_KEY_new_by_curve_name(key->ecdsa_nid);
            if (new->ecdsa == NULL) {
                goto fail;
            }

            p = EC_KEY_get0_public_key(key->ecdsa);
            if (p == NULL) {
                goto fail;
            }

            ok = EC_KEY_set_public_key(new->ecdsa, p);
            if (!ok) {
                goto fail;
            }
        } else {
            rc = EC_KEY_up_ref(key->ecdsa);
            if (rc != 1) {
                goto fail;
            }
            new->ecdsa = key->ecdsa;
        }
#else
        rc = evp_dup_ecdsa_pkey(key, new, demote);
        if (rc != SSH_OK) {
            goto fail;
        }
#endif /* OPENSSL_VERSION_NUMBER */
        break;
#endif /* HAVE_OPENSSL_ECC */
    case SSH_KEYTYPE_ED25519:
        rc = pki_ed25519_key_dup(new, key);
        if (rc != SSH_OK) {
            goto fail;
        }
        break;
    case SSH_KEYTYPE_UNKNOWN:
    default:
        ssh_key_free(new);
        return NULL;
    }

    return new;
fail:
    ssh_key_free(new);
    return NULL;
}

int pki_key_generate_rsa(ssh_key key, int parameter){
	int rc;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    BIGNUM *e;
#else
    OSSL_PARAM params[3];
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL);
    unsigned e = 65537;
#endif /* OPENSSL_VERSION_NUMBER */

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    e = BN_new();
    key->rsa = RSA_new();

    BN_set_word(e, 65537);
    rc = RSA_generate_key_ex(key->rsa, parameter, e, NULL);

    BN_free(e);

    if (rc <= 0 || key->rsa == NULL)
        return SSH_ERROR;
#else
    key->key = NULL;

    rc = EVP_PKEY_keygen_init(pctx);
    if (rc != 1) {
        EVP_PKEY_CTX_free(pctx);
        return SSH_ERROR;
    }

    params[0] = OSSL_PARAM_construct_int("bits", &parameter);
    params[1] = OSSL_PARAM_construct_uint("e", &e);
    params[2] = OSSL_PARAM_construct_end();
    rc = EVP_PKEY_CTX_set_params(pctx, params);
    if (rc != 1) {
        EVP_PKEY_CTX_free(pctx);
        return SSH_ERROR;
    }

    rc = EVP_PKEY_generate(pctx, &(key->key));

    EVP_PKEY_CTX_free(pctx);

    if (rc != 1 || key->key == NULL)
        return SSH_ERROR;
#endif /* OPENSSL_VERSION_NUMBER */
	return SSH_OK;
}

int pki_key_generate_dss(ssh_key key, int parameter){
    int rc;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    key->dsa = DSA_new();
    if (key->dsa == NULL) {
        return SSH_ERROR;
    }
    rc = DSA_generate_parameters_ex(key->dsa,
                                    parameter,
                                    NULL,  /* seed */
                                    0,     /* seed_len */
                                    NULL,  /* counter_ret */
                                    NULL,  /* h_ret */
                                    NULL); /* cb */
    if (rc != 1) {
        DSA_free(key->dsa);
        key->dsa = NULL;
        return SSH_ERROR;
    }
    rc = DSA_generate_key(key->dsa);
    if (rc != 1) {
        DSA_free(key->dsa);
        key->dsa=NULL;
        return SSH_ERROR;
    }
#else
    OSSL_PARAM params[3];
    EVP_PKEY *param_key = NULL;
    EVP_PKEY_CTX *pctx = NULL;
    EVP_PKEY_CTX *gctx = NULL;
    int qbits = parameter < 2048 ? 160 : 256;

    key->key = EVP_PKEY_new();
    if (key->key == NULL) {
        return SSH_ERROR;
    }
    pctx = EVP_PKEY_CTX_new_from_name(NULL, "DSA", NULL);
    if (pctx == NULL) {
        return SSH_ERROR;
    }

    rc = EVP_PKEY_paramgen_init(pctx);
    if (rc != 1) {
        EVP_PKEY_CTX_free(pctx);
        return SSH_ERROR;
    }
    params[0] = OSSL_PARAM_construct_int("pbits", &parameter);
    params[1] = OSSL_PARAM_construct_int("qbits", &qbits);
    params[2] = OSSL_PARAM_construct_end();
    rc = EVP_PKEY_CTX_set_params(pctx, params);
    if (rc != 1) {
        EVP_PKEY_CTX_free(pctx);
        return SSH_ERROR;
    }
    /* generating the domain parameters */
    rc = EVP_PKEY_generate(pctx, &param_key);
    if (rc != 1) {
        EVP_PKEY_CTX_free(pctx);
        EVP_PKEY_free(param_key);
        return SSH_ERROR;
    }
    EVP_PKEY_CTX_free(pctx);

    gctx = EVP_PKEY_CTX_new_from_pkey(NULL, param_key, NULL);
    if (gctx == NULL) {
        EVP_PKEY_free(param_key);
        return SSH_ERROR;
    }

    EVP_PKEY_free(param_key);
    rc = EVP_PKEY_keygen_init(gctx);
    if (rc != 1) {
        EVP_PKEY_CTX_free(gctx);
        return SSH_ERROR;
    }
    /* generating the key from the domain parameters */
    rc = EVP_PKEY_generate(gctx, &key->key);
    if (rc != 1) {
        EVP_PKEY_free(key->key);
        key->key = NULL;
        EVP_PKEY_CTX_free(gctx);
        return SSH_ERROR;
    }
    EVP_PKEY_CTX_free(gctx);
#endif /* OPENSSL_VERSION_NUMBER */
    return SSH_OK;
}

#ifdef HAVE_OPENSSL_ECC
int pki_key_generate_ecdsa(ssh_key key, int parameter) {
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
    int ok;
#else
    const char *group_name = NULL;
#endif /* OPENSSL_VERSION_NUMBER */
    switch (parameter) {
        case 256:
            key->ecdsa_nid = NID_X9_62_prime256v1;
            key->type = SSH_KEYTYPE_ECDSA_P256;
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER >= 0x30000000L
 */
#if 0
            group_name = NISTP256;
#endif /* OPENSSL_VERSION_NUMBER */
            break;
        case 384:
            key->ecdsa_nid = NID_secp384r1;
            key->type = SSH_KEYTYPE_ECDSA_P384;
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER >= 0x30000000L
 */
#if 0
            group_name = NISTP384;
#endif /* OPENSSL_VERSION_NUMBER */
            break;
        case 521:
            key->ecdsa_nid = NID_secp521r1;
            key->type = SSH_KEYTYPE_ECDSA_P521;
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER >= 0x30000000L
 */
#if 0
            group_name = NISTP521;
#endif /* OPENSSL_VERSION_NUMBER */
            break;
        default:
            SSH_LOG(SSH_LOG_WARN, "Invalid parameter %d for ECDSA key "
                    "generation", parameter);
            return SSH_ERROR;
    }
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
    key->ecdsa = EC_KEY_new_by_curve_name(key->ecdsa_nid);
    if (key->ecdsa == NULL) {
        return SSH_ERROR;
    }
#else
    key->key = EVP_EC_gen(group_name);
    if (key->key == NULL) {
        return SSH_ERROR;
    }
#endif /* OPENSSL_VERSION_NUMBER */

/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
    ok = EC_KEY_generate_key(key->ecdsa);
    if (!ok) {
        EC_KEY_free(key->ecdsa);
        return SSH_ERROR;
    }

    EC_KEY_set_asn1_flag(key->ecdsa, OPENSSL_EC_NAMED_CURVE);
#endif /* OPENSSL_VERSION_NUMBER */
    return SSH_OK;
}
#endif /* HAVE_OPENSSL_ECC */

/* With OpenSSL 3.0 and higher the parameter 'what'
 * is ignored and the comparison is done by OpenSSL
 */
int pki_key_compare(const ssh_key k1,
                    const ssh_key k2,
                    enum ssh_keycmp_e what)
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    int rc;
    (void) what;
#endif /* OPENSSL_VERSION_NUMBER */

    switch (k1->type) {
        case SSH_KEYTYPE_DSS:
#if OPENSSL_VERSION_NUMBER < 0x30000000L
            {
                const BIGNUM *p1, *p2, *q1, *q2, *g1, *g2,
                    *pub_key1, *pub_key2, *priv_key1, *priv_key2;
                if (DSA_size(k1->dsa) != DSA_size(k2->dsa)) {
                    return 1;
                }
                DSA_get0_pqg(k1->dsa, &p1, &q1, &g1);
                DSA_get0_pqg(k2->dsa, &p2, &q2, &g2);
                if (bignum_cmp(p1, p2) != 0) {
                    return 1;
                }
                if (bignum_cmp(q1, q2) != 0) {
                    return 1;
                }
                if (bignum_cmp(g1, g2) != 0) {
                    return 1;
                }
                DSA_get0_key(k1->dsa, &pub_key1, &priv_key1);
                DSA_get0_key(k2->dsa, &pub_key2, &priv_key2);
                if (bignum_cmp(pub_key1, pub_key2) != 0) {
                    return 1;
                }

                if (what == SSH_KEY_CMP_PRIVATE) {
                    if (bignum_cmp(priv_key1, priv_key2) != 0) {
                        return 1;
                    }
                }
                break;
            }
#endif /* OPENSSL_VERSION_NUMBER */
        case SSH_KEYTYPE_RSA:
        case SSH_KEYTYPE_RSA1:
#if OPENSSL_VERSION_NUMBER < 0x30000000L
            {
                const BIGNUM *e1, *e2, *n1, *n2, *p1, *p2, *q1, *q2;
                if (RSA_size(k1->rsa) != RSA_size(k2->rsa)) {
                    return 1;
                }
                RSA_get0_key(k1->rsa, &n1, &e1, NULL);
                RSA_get0_key(k2->rsa, &n2, &e2, NULL);
                if (bignum_cmp(e1, e2) != 0) {
                    return 1;
                }
                if (bignum_cmp(n1, n2) != 0) {
                    return 1;
                }

                if (what == SSH_KEY_CMP_PRIVATE) {
                    RSA_get0_factors(k1->rsa, &p1, &q1);
                    RSA_get0_factors(k2->rsa, &p2, &q2);
                    if (bignum_cmp(p1, p2) != 0) {
                        return 1;
                    }

                    if (bignum_cmp(q1, q2) != 0) {
                        return 1;
                    }
                }
                break;
            }
#endif /* OPENSSL_VERSION_NUMBER */
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * delete this part of #if because it gets done below EC
 */
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
            rc = EVP_PKEY_eq(k1->key, k2->key);
            if (rc != 1) {
                return 1;
            }
            break;
#endif /* OPENSSL_VERSION_NUMBER */
        case SSH_KEYTYPE_ECDSA_P256:
        case SSH_KEYTYPE_ECDSA_P384:
        case SSH_KEYTYPE_ECDSA_P521:
        case SSH_KEYTYPE_SK_ECDSA:
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
#ifdef HAVE_OPENSSL_ECC
            {
                const EC_POINT *p1 = EC_KEY_get0_public_key(k1->ecdsa);
                const EC_POINT *p2 = EC_KEY_get0_public_key(k2->ecdsa);
                const EC_GROUP *g1 = EC_KEY_get0_group(k1->ecdsa);
                const EC_GROUP *g2 = EC_KEY_get0_group(k2->ecdsa);

                if (p1 == NULL || p2 == NULL) {
                    return 1;
                }

                if (EC_GROUP_cmp(g1, g2, NULL) != 0) {
                    return 1;
                }

                if (EC_POINT_cmp(g1, p1, p2, NULL) != 0) {
                    return 1;
                }

                if (what == SSH_KEY_CMP_PRIVATE) {
                    if (bignum_cmp(EC_KEY_get0_private_key(k1->ecdsa),
                                   EC_KEY_get0_private_key(k2->ecdsa))) {
                        return 1;
                    }
                }
                break;
            }
#endif /* HAVE_OPENSSL_ECC */
#endif /* OPENSSL_VERSION_NUMBER */
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * else
 */
#if 0
            rc = EVP_PKEY_eq(k1->key, k2->key);
            if (rc != 1) {
                return 1;
            }
            break;
#endif /* OPENSSL_VERSION_NUMBER */
        case SSH_KEYTYPE_ED25519:
        case SSH_KEYTYPE_SK_ED25519:
            /* ed25519 keys handled globally */
        case SSH_KEYTYPE_UNKNOWN:
        default:
            return 1;
    }
    return 0;
}

ssh_string pki_private_key_to_pem(const ssh_key key,
                                  const char *passphrase,
                                  ssh_auth_callback auth_fn,
                                  void *auth_data)
{
    ssh_string blob = NULL;
    BUF_MEM *buf = NULL;
    BIO *mem = NULL;
    EVP_PKEY *pkey = NULL;
    int rc;

    mem = BIO_new(BIO_s_mem());
    if (mem == NULL) {
        return NULL;
    }

    switch (key->type) {
        case SSH_KEYTYPE_DSS:
#if OPENSSL_VERSION_NUMBER < 0x30000000L
            pkey = EVP_PKEY_new();
            if (pkey == NULL) {
                goto err;
            }

            rc = EVP_PKEY_set1_DSA(pkey, key->dsa);
            break;
#endif /* OPENSSL_VERSION_NUMBER */
        case SSH_KEYTYPE_RSA:
        case SSH_KEYTYPE_RSA1:
#if OPENSSL_VERSION_NUMBER < 0x30000000L
            pkey = EVP_PKEY_new();
            if (pkey == NULL) {
                goto err;
            }

            rc = EVP_PKEY_set1_RSA(pkey, key->rsa);
            break;
#endif /* OPENSSL_VERSION_NUMBER */
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * Delete this part, because it is done below HAVE_ECC
 */
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
            rc = EVP_PKEY_up_ref(key->key);
            if (rc != 1) {
                goto err;
            }
            pkey = key->key;

            /* Mark the operation as successful as for the other key types */
            rc = 1;

            break;
#endif /* OPENSSL_VERSION_NUMBER */
        case SSH_KEYTYPE_ECDSA_P256:
        case SSH_KEYTYPE_ECDSA_P384:
        case SSH_KEYTYPE_ECDSA_P521:
#ifdef HAVE_ECC
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
            pkey = EVP_PKEY_new();
            if (pkey == NULL) {
                goto err;
            }

            rc = EVP_PKEY_set1_EC_KEY(pkey, key->ecdsa);
            break;
#endif /* OPENSSL_VERSION_NUMBER */
#endif /* HAVE_ECC */
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER >= 0x30000000L
 */
#if 0
            rc = EVP_PKEY_up_ref(key->key);
            if (rc != 1) {
                goto err;
            }
            pkey = key->key;

            /* Mark the operation as successful as for the other key types */
            rc = 1;

            break;
#endif /* OPENSSL_VERSION_NUMBER */
        case SSH_KEYTYPE_ED25519:
#ifdef HAVE_OPENSSL_ED25519
            /* In OpenSSL, the input is the private key seed only, which means
             * the first half of the SSH private key (the second half is the
             * public key) */
            pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL,
                    (const uint8_t *)key->ed25519_privkey,
                    ED25519_KEY_LEN);
            if (pkey == NULL) {
                SSH_LOG(SSH_LOG_TRACE,
                        "Failed to create ed25519 EVP_PKEY: %s",
                        ERR_error_string(ERR_get_error(), NULL));
                goto err;
            }

            /* Mark the operation as successful as for the other key types */
            rc = 1;
            break;
#else
            SSH_LOG(SSH_LOG_WARN, "PEM output not supported for key type ssh-ed25519");
            goto err;
#endif /* HAVE_OPENSSL_ED25519 */
        case SSH_KEYTYPE_DSS_CERT01:
        case SSH_KEYTYPE_RSA_CERT01:
        case SSH_KEYTYPE_ECDSA_P256_CERT01:
        case SSH_KEYTYPE_ECDSA_P384_CERT01:
        case SSH_KEYTYPE_ECDSA_P521_CERT01:
        case SSH_KEYTYPE_ED25519_CERT01:
        case SSH_KEYTYPE_UNKNOWN:
        default:
            SSH_LOG(SSH_LOG_WARN, "Unknown or invalid private key type %d", key->type);
            goto err;
    }
    if (rc != 1) {
        SSH_LOG(SSH_LOG_WARN, "Failed to initialize EVP_PKEY structure");
        goto err;
    }

    if (passphrase == NULL) {
        struct pem_get_password_struct pgp = { auth_fn, auth_data };

        rc = PEM_write_bio_PrivateKey(mem,
                                      pkey,
                                      NULL, /* cipher */
                                      NULL, /* kstr */
                                      0, /* klen */
                                      pem_get_password,
                                      &pgp);
    } else {
        rc = PEM_write_bio_PrivateKey(mem,
                                      pkey,
                                      EVP_aes_128_cbc(),
                                      NULL, /* kstr */
                                      0, /* klen */
                                      NULL, /* auth_fn */
                                      (void*) passphrase);
    }
    EVP_PKEY_free(pkey);
    pkey = NULL;

    if (rc != 1) {
        goto err;
    }

    BIO_get_mem_ptr(mem, &buf);

    blob = ssh_string_new(buf->length);
    if (blob == NULL) {
        goto err;
    }

    rc = ssh_string_fill(blob, buf->data, buf->length);
    if (rc < 0) {
        ssh_string_free(blob);
        goto err;
    }

    BIO_free(mem);

    return blob;

err:
    EVP_PKEY_free(pkey);
    BIO_free(mem);
    return NULL;
}

ssh_key pki_private_key_from_base64(const char *b64_key,
                                    const char *passphrase,
                                    ssh_auth_callback auth_fn,
                                    void *auth_data)
{
    BIO *mem = NULL;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    DSA *dsa = NULL;
    RSA *rsa = NULL;
#endif /* OPENSSL_VERSION_NUMBER */
#ifdef HAVE_OPENSSL_ECC
    EC_KEY *ecdsa = NULL;
#else
    void *ecdsa = NULL;
#endif /* HAVE_OPENSSL_ECC */
#ifdef HAVE_OPENSSL_ED25519
    uint8_t *ed25519 = NULL;
#else
    ed25519_privkey *ed25519 = NULL;
#endif /* HAVE_OPENSSL_ED25519 */
    ssh_key key = NULL;
    enum ssh_keytypes_e type = SSH_KEYTYPE_UNKNOWN;
    EVP_PKEY *pkey = NULL;

    mem = BIO_new_mem_buf((void*)b64_key, -1);

    if (passphrase == NULL) {
        if (auth_fn) {
            struct pem_get_password_struct pgp = { auth_fn, auth_data };

            pkey = PEM_read_bio_PrivateKey(mem, NULL, pem_get_password, &pgp);
        } else {
            /* openssl uses its own callback to get the passphrase here */
            pkey = PEM_read_bio_PrivateKey(mem, NULL, NULL, NULL);
        }
    } else {
        pkey = PEM_read_bio_PrivateKey(mem, NULL, NULL, (void *) passphrase);
    }

    BIO_free(mem);

    if (pkey == NULL) {
        SSH_LOG(SSH_LOG_WARN,
                "Parsing private key: %s",
                ERR_error_string(ERR_get_error(), NULL));
        return NULL;
    }
    switch (EVP_PKEY_base_id(pkey)) {
    case EVP_PKEY_DSA:
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        dsa = EVP_PKEY_get1_DSA(pkey);
        if (dsa == NULL) {
            SSH_LOG(SSH_LOG_WARN,
                    "Parsing private key: %s",
                    ERR_error_string(ERR_get_error(),NULL));
            goto fail;
        }
#endif
        type = SSH_KEYTYPE_DSS;
        break;
    case EVP_PKEY_RSA:
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        rsa = EVP_PKEY_get1_RSA(pkey);
        if (rsa == NULL) {
            SSH_LOG(SSH_LOG_WARN,
                    "Parsing private key: %s",
                    ERR_error_string(ERR_get_error(),NULL));
            goto fail;
        }
#endif /* OPENSSL_VERSION_NUMBER */
        type = SSH_KEYTYPE_RSA;
        break;
    case EVP_PKEY_EC:
#ifdef HAVE_OPENSSL_ECC
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
        ecdsa = EVP_PKEY_get1_EC_KEY(pkey);
        if (ecdsa == NULL) {
            SSH_LOG(SSH_LOG_WARN,
                    "Parsing private key: %s",
                    ERR_error_string(ERR_get_error(), NULL));
            goto fail;
        }
#endif /* OPENSSL_VERSION_NUMBER */

        /* pki_privatekey_type_from_string always returns P256 for ECDSA
         * keys, so we need to figure out the correct type here */
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
        type = pki_key_ecdsa_to_key_type(ecdsa);
#else
        type = pki_key_ecdsa_to_key_type(pkey);
#endif /* OPENSSL_VERSION_NUMBER */
        if (type == SSH_KEYTYPE_UNKNOWN) {
            SSH_LOG(SSH_LOG_WARN, "Invalid private key.");
            goto fail;
        }

/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * Remove these three lines
 */
        break;
#endif /* HAVE_OPENSSL_ECC */
#ifdef HAVE_OPENSSL_ED25519
    case EVP_PKEY_ED25519:
    {
        size_t key_len;
        int evp_rc = 0;

        /* Get the key length */
        evp_rc = EVP_PKEY_get_raw_private_key(pkey, NULL, &key_len);
        if (evp_rc != 1) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Failed to get ed25519 raw private key length:  %s",
                    ERR_error_string(ERR_get_error(), NULL));
            goto fail;
        }

        if (key_len != ED25519_KEY_LEN) {
            goto fail;
        }

        ed25519 = malloc(key_len);
        if (ed25519 == NULL) {
            SSH_LOG(SSH_LOG_WARN, "Out of memory");
            goto fail;
        }

        evp_rc = EVP_PKEY_get_raw_private_key(pkey, (uint8_t *)ed25519,
                                              &key_len);
        if (evp_rc != 1) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Failed to get ed25519 raw private key:  %s",
                    ERR_error_string(ERR_get_error(), NULL));
            goto fail;
        }
        type = SSH_KEYTYPE_ED25519;

    }
    break;
#endif /* HAVE_OPENSSL_ED25519 */
    default:
        SSH_LOG(SSH_LOG_WARN, "Unknown or invalid private key type %d",
                EVP_PKEY_base_id(pkey));
        EVP_PKEY_free(pkey);
        return NULL;
    }

    key = ssh_key_new();
    if (key == NULL) {
        goto fail;
    }

    key->type = type;
    key->type_c = ssh_key_type_to_char(type);
    key->flags = SSH_KEY_FLAG_PRIVATE | SSH_KEY_FLAG_PUBLIC;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    key->dsa = dsa;
    key->rsa = rsa;
#endif /* OPENSSL_VERSION_NUMBER */
    key->key = pkey;
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * Move key->ecdsa line into the #if above this
 */
    key->ecdsa = ecdsa;
    key->ed25519_privkey = ed25519;
#ifdef HAVE_OPENSSL_ECC
    if (is_ecdsa_key_type(key->type)) {
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
        key->ecdsa_nid = pki_key_ecdsa_to_nid(key->ecdsa);
#else
        key->ecdsa_nid = pki_key_ecdsa_to_nid(key->key);
#endif /* OPENSSL_VERSION_NUMBER */
    }
#endif /* HAVE_OPENSSL_ECC */

    return key;
fail:
    EVP_PKEY_free(pkey);
    ssh_key_free(key);
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    DSA_free(dsa);
    RSA_free(rsa);
#endif /* OPENSSL_VERSION_NUMBER */
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * Move HAVE_OPENSSL_ECC #ifdef inside the #if above
 */
#ifdef HAVE_OPENSSL_ECC
    EC_KEY_free(ecdsa);
#endif
#ifdef HAVE_OPENSSL_ED25519
    SAFE_FREE(ed25519);
#endif
    return NULL;
}

int pki_privkey_build_dss(ssh_key key,
                          ssh_string p,
                          ssh_string q,
                          ssh_string g,
                          ssh_string pubkey,
                          ssh_string privkey)
{
    int rc;
    BIGNUM *bp, *bq, *bg, *bpub_key, *bpriv_key;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    OSSL_PARAM_BLD *param_bld = OSSL_PARAM_BLD_new();
    if (param_bld == NULL) {
        return SSH_ERROR;
    }
#else
    key->dsa = DSA_new();
    if (key->dsa == NULL) {
        return SSH_ERROR;
    }
#endif /* OPENSSL_VERSION_NUMBER */

    bp = ssh_make_string_bn(p);
    bq = ssh_make_string_bn(q);
    bg = ssh_make_string_bn(g);
    bpub_key = ssh_make_string_bn(pubkey);
    bpriv_key = ssh_make_string_bn(privkey);
    if (bp == NULL || bq == NULL ||
        bg == NULL || bpub_key == NULL) {
        rc = SSH_ERROR;
        goto fail;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    /* Memory management of bp, qq and bg is transferred to DSA object */
    rc = DSA_set0_pqg(key->dsa, bp, bq, bg);
    if (rc == 0) {
        goto fail;
    }

    /* Memory management of bpub_key and bpriv_key is transferred to DSA object */
    rc = DSA_set0_key(key->dsa, bpub_key, bpriv_key);
    if (rc == 0) {
        goto fail;
    }

    return SSH_OK;
fail:
    DSA_free(key->dsa);
    return SSH_ERROR;
#else
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_FFC_P, bp);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_FFC_Q, bq);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_FFC_G, bg);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_PUB_KEY, bpub_key);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_PRIV_KEY, bpriv_key);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }

    rc = evp_build_pkey("DSA", param_bld, &(key->key), EVP_PKEY_KEYPAIR);

fail:
    OSSL_PARAM_BLD_free(param_bld);
    bignum_safe_free(bp);
    bignum_safe_free(bq);
    bignum_safe_free(bg);
    bignum_safe_free(bpub_key);
    bignum_safe_free(bpriv_key);

    return rc;
#endif /* OPENSSL_VERSION_NUMBER */
}

int pki_pubkey_build_dss(ssh_key key,
                         ssh_string p,
                         ssh_string q,
                         ssh_string g,
                         ssh_string pubkey) {
    int rc;
    BIGNUM *bp = NULL, *bq = NULL, *bg = NULL, *bpub_key = NULL;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    OSSL_PARAM_BLD *param_bld = OSSL_PARAM_BLD_new();
    if (param_bld == NULL) {
        return SSH_ERROR;
    }
#else
    key->dsa = DSA_new();
    if (key->dsa == NULL) {
        return SSH_ERROR;
    }
#endif /* OPENSSL_VERSION_NUMBER */

    bp = ssh_make_string_bn(p);
    bq = ssh_make_string_bn(q);
    bg = ssh_make_string_bn(g);
    bpub_key = ssh_make_string_bn(pubkey);
    if (bp == NULL || bq == NULL ||
        bg == NULL || bpub_key == NULL) {
        rc = SSH_ERROR;
        goto fail;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    /* Memory management of bp, bq and bg is transferred to DSA object */
    rc = DSA_set0_pqg(key->dsa, bp, bq, bg);
    if (rc == 0) {
        goto fail;
    }

    /* Memory management of npub_key is transferred to DSA object */
    rc = DSA_set0_key(key->dsa, bpub_key, NULL);
    if (rc == 0) {
        goto fail;
    }

    return SSH_OK;
fail:
    DSA_free(key->dsa);
    return SSH_ERROR;
#else
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_FFC_P, bp);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_FFC_Q, bq);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_FFC_G, bg);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_PUB_KEY, bpub_key);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }

    rc = evp_build_pkey("DSA", param_bld, &(key->key), EVP_PKEY_PUBLIC_KEY);

fail:
    OSSL_PARAM_BLD_free(param_bld);
    bignum_safe_free(bp);
    bignum_safe_free(bq);
    bignum_safe_free(bg);
    bignum_safe_free(bpub_key);

    return rc;
#endif /* OPENSSL_VERSION_NUMBER */
}

int pki_privkey_build_rsa(ssh_key key,
                          ssh_string n,
                          ssh_string e,
                          ssh_string d,
                          UNUSED_PARAM(ssh_string iqmp),
                          ssh_string p,
                          ssh_string q)
{
    int rc;
    BIGNUM *be, *bn, *bd/*, *biqmp*/, *bp, *bq;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    OSSL_PARAM_BLD *param_bld = OSSL_PARAM_BLD_new();
    if (param_bld == NULL) {
        return SSH_ERROR;
    }
#else
    key->rsa = RSA_new();
    if (key->rsa == NULL) {
        return SSH_ERROR;
    }
#endif /* OPENSSL_VERSION_NUMBER */

    bn = ssh_make_string_bn(n);
    be = ssh_make_string_bn(e);
    bd = ssh_make_string_bn(d);
    /*biqmp = ssh_make_string_bn(iqmp);*/
    bp = ssh_make_string_bn(p);
    bq = ssh_make_string_bn(q);
    if (be == NULL || bn == NULL || bd == NULL ||
        /*biqmp == NULL ||*/ bp == NULL || bq == NULL) {
        rc = SSH_ERROR;
        goto fail;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    /* Memory management of be, bn and bd is transferred to RSA object */
    rc = RSA_set0_key(key->rsa, bn, be, bd);
    if (rc == 0) {
        goto fail;
    }

    /* Memory management of bp and bq is transferred to RSA object */
    rc = RSA_set0_factors(key->rsa, bp, bq);
    if (rc == 0) {
        goto fail;
    }

    /* p, q, dmp1, dmq1 and iqmp may be NULL in private keys, but the RSA
     * operations are much faster when these values are available.
     * https://www.openssl.org/docs/man1.0.2/crypto/rsa.html
     */
    /* RSA_set0_crt_params(key->rsa, biqmp, NULL, NULL);
    TODO calculate missing crt_params */

    return SSH_OK;
fail:
    RSA_free(key->rsa);
    return SSH_ERROR;
#else
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_N, bn);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_E, be);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_D, bd);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }

    rc = evp_build_pkey("RSA", param_bld, &(key->key), EVP_PKEY_KEYPAIR);
    if (rc != SSH_OK) {
        rc = SSH_ERROR;
        goto fail;
    }

    rc = EVP_PKEY_set_bn_param(key->key, OSSL_PKEY_PARAM_RSA_FACTOR1, bp);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }

    rc = EVP_PKEY_set_bn_param(key->key, OSSL_PKEY_PARAM_RSA_FACTOR2, bq);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }

fail:
    OSSL_PARAM_BLD_free(param_bld);
    bignum_safe_free(bn);
    bignum_safe_free(be);
    bignum_safe_free(bd);
    bignum_safe_free(bp);
    bignum_safe_free(bq);

    return rc;
#endif /* OPENSSL_VERSION_NUMBER */
}

int pki_pubkey_build_rsa(ssh_key key,
                         ssh_string e,
                         ssh_string n) {
    int rc;
    BIGNUM *be = NULL, *bn = NULL;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    OSSL_PARAM_BLD *param_bld = OSSL_PARAM_BLD_new();
    if (param_bld == NULL) {
        return SSH_ERROR;
    }
#else
    key->rsa = RSA_new();
    if (key->rsa == NULL) {
        return SSH_ERROR;
    }
#endif /* OPENSSL_VERSION_NUMBER */

    be = ssh_make_string_bn(e);
    bn = ssh_make_string_bn(n);
    if (be == NULL || bn == NULL) {
        rc = SSH_ERROR;
        goto fail;
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    /* Memory management of bn and be is transferred to RSA object */
    rc = RSA_set0_key(key->rsa, bn, be, NULL);
    if (rc == 0) {
        goto fail;
    }

    return SSH_OK;
fail:
    RSA_free(key->rsa);
    return SSH_ERROR;
#else
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_N, bn);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }
    rc = OSSL_PARAM_BLD_push_BN(param_bld, OSSL_PKEY_PARAM_RSA_E, be);
    if (rc != 1) {
        rc = SSH_ERROR;
        goto fail;
    }

    rc = evp_build_pkey("RSA", param_bld, &(key->key), EVP_PKEY_PUBLIC_KEY);

fail:
    OSSL_PARAM_BLD_free(param_bld);
    bignum_safe_free(bn);
    bignum_safe_free(be);

    return rc;
#endif /* OPENSSL_VERSION_NUMBER */
}

ssh_string pki_publickey_to_blob(const ssh_key key)
{
    ssh_buffer buffer;
    ssh_string type_s;
    ssh_string str = NULL;
    ssh_string e = NULL;
    ssh_string n = NULL;
    ssh_string p = NULL;
    ssh_string g = NULL;
    ssh_string q = NULL;
    int rc;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    BIGNUM *bp = NULL, *bq = NULL, *bg = NULL, *bpub_key = NULL,
           *bn = NULL, *be = NULL;
    OSSL_PARAM *params = NULL;
#endif /* OPENSSL_VERSION_NUMBER */

    buffer = ssh_buffer_new();
    if (buffer == NULL) {
        return NULL;
    }

    if (key->cert != NULL) {
        rc = ssh_buffer_add_buffer(buffer, key->cert);
        if (rc < 0) {
            SSH_BUFFER_FREE(buffer);
            return NULL;
        }
        goto makestring;
    }

    type_s = ssh_string_from_char(key->type_c);
    if (type_s == NULL) {
        SSH_BUFFER_FREE(buffer);
        return NULL;
    }

    rc = ssh_buffer_add_ssh_string(buffer, type_s);
    SSH_STRING_FREE(type_s);
    if (rc < 0) {
        SSH_BUFFER_FREE(buffer);
        return NULL;
    }

    switch (key->type) {
        case SSH_KEYTYPE_DSS: {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
            const BIGNUM *bp, *bq, *bg, *bpub_key;
            DSA_get0_pqg(key->dsa, &bp, &bq, &bg);
            DSA_get0_key(key->dsa, &bpub_key, NULL);
#else
            const OSSL_PARAM *out_param = NULL;
            rc = EVP_PKEY_todata(key->key, EVP_PKEY_PUBLIC_KEY, &params);
            if (rc != 1) {
                goto fail;
            }
            out_param = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_FFC_P);
            if (out_param == NULL) {
                SSH_LOG(SSH_LOG_WARN, "DSA: No param P has been found");
                goto fail;
            }
            rc = OSSL_PARAM_get_BN(out_param, &bp);
            if (rc != 1) {
                goto fail;
            }
            out_param = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_FFC_Q);
            if (out_param == NULL) {
                SSH_LOG(SSH_LOG_WARN, "DSA: No param Q has been found");
                goto fail;
            }
            rc = OSSL_PARAM_get_BN(out_param, &bq);
            if (rc != 1) {
                goto fail;
            }
            out_param = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_FFC_G);
            if (out_param == NULL) {
                SSH_LOG(SSH_LOG_WARN, "DSA: No param G has been found");
                goto fail;
            }
            rc = OSSL_PARAM_get_BN(out_param, &bg);
            if (rc != 1) {
                goto fail;
            }
            out_param = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_PUB_KEY);
            if (out_param == NULL) {
                SSH_LOG(SSH_LOG_WARN, "DSA: No param PUB_KEY has been found");
                goto fail;
            }
            rc = OSSL_PARAM_get_BN(out_param, &bpub_key);
            if (rc != 1) {
                goto fail;
            }
#endif /* OPENSSL_VERSION_NUMBER */
            p = ssh_make_bignum_string((BIGNUM *)bp);
            if (p == NULL) {
                goto fail;
            }

            q = ssh_make_bignum_string((BIGNUM *)bq);
            if (q == NULL) {
                goto fail;
            }

            g = ssh_make_bignum_string((BIGNUM *)bg);
            if (g == NULL) {
                goto fail;
            }

            n = ssh_make_bignum_string((BIGNUM *)bpub_key);
            if (n == NULL) {
                goto fail;
            }

            if (ssh_buffer_add_ssh_string(buffer, p) < 0) {
                goto fail;
            }
            if (ssh_buffer_add_ssh_string(buffer, q) < 0) {
                goto fail;
            }
            if (ssh_buffer_add_ssh_string(buffer, g) < 0) {
                goto fail;
            }
            if (ssh_buffer_add_ssh_string(buffer, n) < 0) {
                goto fail;
            }

            ssh_string_burn(p);
            SSH_STRING_FREE(p);
            p = NULL;
            ssh_string_burn(g);
            SSH_STRING_FREE(g);
            g = NULL;
            ssh_string_burn(q);
            SSH_STRING_FREE(q);
            q = NULL;
            ssh_string_burn(n);
            SSH_STRING_FREE(n);
            n = NULL;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
            bignum_safe_free(bp);
            bignum_safe_free(bq);
            bignum_safe_free(bg);
            bignum_safe_free(bpub_key);
            OSSL_PARAM_free(params);
#endif /* OPENSSL_VERSION_NUMBER */

            break;
        }
        case SSH_KEYTYPE_RSA:
        case SSH_KEYTYPE_RSA1: {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
            const BIGNUM *be, *bn;
            RSA_get0_key(key->rsa, &bn, &be, NULL);
#else
            const OSSL_PARAM *out_param = NULL;
            rc = EVP_PKEY_todata(key->key, EVP_PKEY_PUBLIC_KEY, &params);
            if (rc != 1) {
                goto fail;
            }
            out_param = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_RSA_E);
            if (out_param == NULL) {
                SSH_LOG(SSH_LOG_WARN, "RSA: No param E has been found");
                goto fail;
            }
            rc = OSSL_PARAM_get_BN(out_param, &be);
            if (rc != 1) {
                goto fail;
            }
            out_param = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_RSA_N);
            if (out_param == NULL) {
                SSH_LOG(SSH_LOG_WARN, "RSA: No param N has been found");
                goto fail;
            }
            rc = OSSL_PARAM_get_BN(out_param, &bn);
            if (rc != 1) {
                goto fail;
            }
#endif /* OPENSSL_VERSION_NUMBER */
            e = ssh_make_bignum_string((BIGNUM *)be);
            if (e == NULL) {
                goto fail;
            }

            n = ssh_make_bignum_string((BIGNUM *)bn);
            if (n == NULL) {
                goto fail;
            }

            if (ssh_buffer_add_ssh_string(buffer, e) < 0) {
                goto fail;
            }
            if (ssh_buffer_add_ssh_string(buffer, n) < 0) {
                goto fail;
            }

            ssh_string_burn(e);
            SSH_STRING_FREE(e);
            e = NULL;
            ssh_string_burn(n);
            SSH_STRING_FREE(n);
            n = NULL;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
            bignum_safe_free(bn);
            bignum_safe_free(be);
            OSSL_PARAM_free(params);
#endif /* OPENSSL_VERSION_NUMBER */
            break;
        }
        case SSH_KEYTYPE_ED25519:
        case SSH_KEYTYPE_SK_ED25519:
            rc = pki_ed25519_public_key_to_blob(buffer, key);
            if (rc == SSH_ERROR){
                goto fail;
            }
            if (key->type == SSH_KEYTYPE_SK_ED25519 &&
                ssh_buffer_add_ssh_string(buffer, key->sk_application) < 0) {
                goto fail;
            }
            break;
        case SSH_KEYTYPE_ECDSA_P256:
        case SSH_KEYTYPE_ECDSA_P384:
        case SSH_KEYTYPE_ECDSA_P521:
        case SSH_KEYTYPE_SK_ECDSA:
#ifdef HAVE_OPENSSL_ECC
            {
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER >= 0x30000000L
 */
#if 0
                const void *pubkey;
                size_t pubkey_len;
                OSSL_PARAM *params = NULL, *locate_param = NULL;
#endif /* OPENSSL_VERSION_NUMBER */

                type_s = ssh_string_from_char(pki_key_ecdsa_nid_to_char(key->ecdsa_nid));
                if (type_s == NULL) {
                    SSH_BUFFER_FREE(buffer);
                    return NULL;
                }

                rc = ssh_buffer_add_ssh_string(buffer, type_s);
                SSH_STRING_FREE(type_s);
                if (rc < 0) {
                    SSH_BUFFER_FREE(buffer);
                    return NULL;
                }

/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
#ifdef WITH_PKCS11_URI
            if (ssh_key_is_private(key) && !EC_KEY_get0_public_key(key->ecdsa)) {
                SSH_LOG(SSH_LOG_INFO, "It is mandatory to have separate public"
                        " ECDSA key objects in the PKCS #11 device. Unlike RSA,"
                        " ECDSA public keys cannot be derived from their private keys.");
                goto fail;
            }
#endif /* WITH_PKCS11_URI */
                e = make_ecpoint_string(EC_KEY_get0_group(key->ecdsa),
                                        EC_KEY_get0_public_key(key->ecdsa));
#else
                rc = EVP_PKEY_todata(key->key, EVP_PKEY_PUBLIC_KEY, &params);
                if (rc < 0) {
                    OSSL_PARAM_free(params);
                    goto fail;
                }

                locate_param = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_PUB_KEY);
#ifdef WITH_PKCS11_URI
                if (ssh_key_is_private(key) && !locate_param) {
                    SSH_LOG(SSH_LOG_INFO, "It is mandatory to have separate"
                            " public ECDSA key objects in the PKCS #11 device."
                            " Unlike RSA, ECDSA public keys cannot be derived"
                            " from their private keys.");
                    goto fail;
                }
#endif /* WITH_PKCS11_URI */

                rc = OSSL_PARAM_get_octet_string_ptr(locate_param, &pubkey, &pubkey_len);
                if (rc != 1) {
                    OSSL_PARAM_free(params);
                    goto fail;
                }

                e = ssh_string_new(pubkey_len);
#endif /* OPENSSL_VERSION_NUMBER */
                if (e == NULL) {
                    SSH_BUFFER_FREE(buffer);
                    return NULL;
                }

/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER >= 0x30000000L
 */
#if 0
                if (memcpy(ssh_string_data(e), pubkey, pubkey_len) == NULL) {
                    OSSL_PARAM_free(params);
                    goto fail;
                }
#endif /* OPENSSL_VERSION_NUMBER */
                rc = ssh_buffer_add_ssh_string(buffer, e);
                if (rc < 0) {
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER >= 0x30000000L
 */
#if 0
                    OSSL_PARAM_free(params);
#endif /* OPENSSL_VERSION_NUMBER */
                    goto fail;
                }

                ssh_string_burn(e);
                SSH_STRING_FREE(e);
                e = NULL;
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER >= 0x30000000L
 */
#if 0
                OSSL_PARAM_free(params);
#endif /* OPENSSL_VERSION_NUMBER */

                if (key->type == SSH_KEYTYPE_SK_ECDSA &&
                    ssh_buffer_add_ssh_string(buffer, key->sk_application) < 0) {
                    goto fail;
                }

                break;
            }
#endif /* HAVE_OPENSSL_ECC */
        case SSH_KEYTYPE_UNKNOWN:
        default:
            goto fail;
    }

makestring:
    str = ssh_string_new(ssh_buffer_get_len(buffer));
    if (str == NULL) {
        goto fail;
    }

    rc = ssh_string_fill(str, ssh_buffer_get(buffer), ssh_buffer_get_len(buffer));
    if (rc < 0) {
        goto fail;
    }
    SSH_BUFFER_FREE(buffer);

    return str;
fail:
    SSH_BUFFER_FREE(buffer);
    ssh_string_burn(str);
    SSH_STRING_FREE(str);
    ssh_string_burn(e);
    SSH_STRING_FREE(e);
    ssh_string_burn(p);
    SSH_STRING_FREE(p);
    ssh_string_burn(g);
    SSH_STRING_FREE(g);
    ssh_string_burn(q);
    SSH_STRING_FREE(q);
    ssh_string_burn(n);
    SSH_STRING_FREE(n);
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    bignum_safe_free(bp);
    bignum_safe_free(bq);
    bignum_safe_free(bg);
    bignum_safe_free(bpub_key);
    bignum_safe_free(bn);
    bignum_safe_free(be);
    OSSL_PARAM_free(params);
#endif /* OPENSSL_VERSION_NUMBER */

    return NULL;
}

static ssh_string pki_dsa_signature_to_blob(const ssh_signature sig)
{
    char buffer[40] = { 0 };
    ssh_string sig_blob = NULL;
    const BIGNUM *pr = NULL, *ps = NULL;

    ssh_string r = NULL;
    size_t r_len, r_offset_in, r_offset_out;

    ssh_string s = NULL;
    size_t s_len, s_offset_in, s_offset_out;

    const unsigned char *raw_sig_data = NULL;
    size_t raw_sig_len;
    int rc;

    DSA_SIG *dsa_sig;

    if (sig == NULL || sig->raw_sig == NULL) {
        return NULL;
    }
    raw_sig_data = ssh_string_data(sig->raw_sig);
    if (raw_sig_data == NULL) {
        return NULL;
    }
    raw_sig_len = ssh_string_len(sig->raw_sig);

    dsa_sig = d2i_DSA_SIG(NULL, &raw_sig_data, raw_sig_len);
    if (dsa_sig == NULL) {
        return NULL;
    }

    DSA_SIG_get0(dsa_sig, &pr, &ps);
    if (pr == NULL || ps == NULL) {
        goto error;
    }

    r = ssh_make_bignum_string((BIGNUM *)pr);
    if (r == NULL) {
        goto error;
    }

    s = ssh_make_bignum_string((BIGNUM *)ps);
    if (s == NULL) {
        goto error;
    }

    r_len = ssh_string_len(r);
    r_offset_in  = (r_len > 20) ? (r_len - 20) : 0;
    r_offset_out = (r_len < 20) ? (20 - r_len) : 0;

    s_len = ssh_string_len(s);
    s_offset_in  = (s_len > 20) ? (s_len - 20) : 0;
    s_offset_out = (s_len < 20) ? (20 - s_len) : 0;

    memcpy(buffer + r_offset_out,
           ((char *)ssh_string_data(r)) + r_offset_in,
           r_len - r_offset_in);
    memcpy(buffer + 20 + s_offset_out,
           ((char *)ssh_string_data(s)) + s_offset_in,
           s_len - s_offset_in);

    DSA_SIG_free(dsa_sig);
    SSH_STRING_FREE(r);
    SSH_STRING_FREE(s);

    sig_blob = ssh_string_new(40);
    if (sig_blob == NULL) {
        return NULL;
    }

    rc = ssh_string_fill(sig_blob, buffer, 40);
    if (rc < 0) {
        SSH_STRING_FREE(sig_blob);
        return NULL;
    }

    return sig_blob;

error:
    DSA_SIG_free(dsa_sig);
    SSH_STRING_FREE(r);
    SSH_STRING_FREE(s);
    return NULL;
}

static ssh_string pki_ecdsa_signature_to_blob(const ssh_signature sig)
{
    ssh_string r = NULL;
    ssh_string s = NULL;

    ssh_buffer buf = NULL;
    ssh_string sig_blob = NULL;

    const BIGNUM *pr = NULL, *ps = NULL;

    const unsigned char *raw_sig_data = NULL;
    size_t raw_sig_len;

    ECDSA_SIG *ecdsa_sig;

    int rc;

    if (sig == NULL || sig->raw_sig == NULL) {
        return NULL;
    }
    raw_sig_data = ssh_string_data(sig->raw_sig);
    if (raw_sig_data == NULL) {
        return NULL;
    }
    raw_sig_len = ssh_string_len(sig->raw_sig);

    ecdsa_sig = d2i_ECDSA_SIG(NULL, &raw_sig_data, raw_sig_len);
    if (ecdsa_sig == NULL) {
        return NULL;
    }

    ECDSA_SIG_get0(ecdsa_sig, &pr, &ps);
    if (pr == NULL || ps == NULL) {
        goto error;
    }

    r = ssh_make_bignum_string((BIGNUM *)pr);
    if (r == NULL) {
        goto error;
    }

    s = ssh_make_bignum_string((BIGNUM *)ps);
    if (s == NULL) {
        goto error;
    }

    buf = ssh_buffer_new();
    if (buf == NULL) {
        goto error;
    }

    rc = ssh_buffer_add_ssh_string(buf, r);
    if (rc < 0) {
        goto error;
    }

    rc = ssh_buffer_add_ssh_string(buf, s);
    if (rc < 0) {
        goto error;
    }

    sig_blob = ssh_string_new(ssh_buffer_get_len(buf));
    if (sig_blob == NULL) {
        goto error;
    }

    rc = ssh_string_fill(sig_blob, ssh_buffer_get(buf), ssh_buffer_get_len(buf));
    if (rc < 0) {
        goto error;
    }

    SSH_STRING_FREE(r);
    SSH_STRING_FREE(s);
    ECDSA_SIG_free(ecdsa_sig);
    SSH_BUFFER_FREE(buf);

    return sig_blob;

error:
    SSH_STRING_FREE(sig_blob);
    SSH_STRING_FREE(r);
    SSH_STRING_FREE(s);
    ECDSA_SIG_free(ecdsa_sig);
    SSH_BUFFER_FREE(buf);
    return NULL;
}

ssh_string pki_signature_to_blob(const ssh_signature sig)
{
    ssh_string sig_blob = NULL;

    switch(sig->type) {
        case SSH_KEYTYPE_DSS:
            sig_blob = pki_dsa_signature_to_blob(sig);
            break;
        case SSH_KEYTYPE_RSA:
        case SSH_KEYTYPE_RSA1:
            sig_blob = ssh_string_copy(sig->raw_sig);
            break;
        case SSH_KEYTYPE_ED25519:
            sig_blob = pki_ed25519_signature_to_blob(sig);
            break;
        case SSH_KEYTYPE_ECDSA_P256:
        case SSH_KEYTYPE_ECDSA_P384:
        case SSH_KEYTYPE_ECDSA_P521:
#ifdef HAVE_OPENSSL_ECC
            sig_blob = pki_ecdsa_signature_to_blob(sig);
            break;
#endif /* HAVE_OPENSSL_ECC */
        default:
        case SSH_KEYTYPE_UNKNOWN:
            SSH_LOG(SSH_LOG_WARN, "Unknown signature key type: %s", sig->type_c);
            return NULL;
    }

    return sig_blob;
}

static int pki_signature_from_rsa_blob(const ssh_key pubkey,
                                       const ssh_string sig_blob,
                                       ssh_signature sig)
{
    uint32_t pad_len = 0;
    char *blob_orig = NULL;
    char *blob_padded_data = NULL;
    ssh_string sig_blob_padded = NULL;

    size_t rsalen = 0;
    size_t len = ssh_string_len(sig_blob);

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    if (pubkey->rsa == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Pubkey RSA field NULL");
        goto errout;
    }

    rsalen = RSA_size(pubkey->rsa);
#else
    if (EVP_PKEY_get_base_id(pubkey->key) != EVP_PKEY_RSA) {
        SSH_LOG(SSH_LOG_WARN, "Key has no RSA pubkey");
        goto errout;
    }

    rsalen = EVP_PKEY_size(pubkey->key);
#endif /* OPENSSL_VERSION_NUMBER */
    if (len > rsalen) {
        SSH_LOG(SSH_LOG_WARN,
                "Signature is too big: %lu > %lu",
                (unsigned long)len,
                (unsigned long)rsalen);
        goto errout;
    }

#ifdef DEBUG_CRYPTO
    SSH_LOG(SSH_LOG_DEBUG, "RSA signature len: %lu", (unsigned long)len);
    ssh_log_hexdump("RSA signature", ssh_string_data(sig_blob), len);
#endif /* DEBUG_CRYPTO */

    if (len == rsalen) {
        sig->raw_sig = ssh_string_copy(sig_blob);
    } else {
        /* pad the blob to the expected rsalen size */
        SSH_LOG(SSH_LOG_DEBUG,
                "RSA signature len %lu < %lu",
                (unsigned long)len,
                (unsigned long)rsalen);

        pad_len = rsalen - len;

        sig_blob_padded = ssh_string_new(rsalen);
        if (sig_blob_padded == NULL) {
            goto errout;
        }

        blob_padded_data = (char *) ssh_string_data(sig_blob_padded);
        blob_orig = (char *) ssh_string_data(sig_blob);

        if (blob_padded_data == NULL || blob_orig == NULL) {
            goto errout;
        }

        /* front-pad the buffer with zeroes */
        explicit_bzero(blob_padded_data, pad_len);
        /* fill the rest with the actual signature blob */
        memcpy(blob_padded_data + pad_len, blob_orig, len);

        sig->raw_sig = sig_blob_padded;
    }

    return SSH_OK;

errout:
    SSH_STRING_FREE(sig_blob_padded);
    return SSH_ERROR;
}

static int pki_signature_from_dsa_blob(UNUSED_PARAM(const ssh_key pubkey),
                                       const ssh_string sig_blob,
                                       ssh_signature sig)
{
    DSA_SIG *dsa_sig = NULL;
    BIGNUM *pr = NULL, *ps = NULL;

    ssh_string r;
    ssh_string s = NULL;

    size_t len;

    size_t raw_sig_len = 0;
    unsigned char *raw_sig_data = NULL;
    unsigned char *temp_raw_sig = NULL;

    int rc;

    len = ssh_string_len(sig_blob);

    /* 40 is the dual signature blob len. */
    if (len != 40) {
        SSH_LOG(SSH_LOG_WARN,
                "Signature has wrong size: %lu",
                (unsigned long)len);
        goto error;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("r", ssh_string_data(sig_blob), 20);
    ssh_log_hexdump("s", (unsigned char *)ssh_string_data(sig_blob) + 20, 20);
#endif /* DEBUG_CRYPTO */

    r = ssh_string_new(20);
    if (r == NULL) {
        goto error;
    }
    rc = ssh_string_fill(r, ssh_string_data(sig_blob), 20);
    if (rc < 0) {
        SSH_STRING_FREE(r);
        goto error;
    }

    pr = ssh_make_string_bn(r);
    ssh_string_burn(r);
    SSH_STRING_FREE(r);
    if (pr == NULL) {
        goto error;
    }

    s = ssh_string_new(20);
    if (s == NULL) {
        goto error;
    }
    rc = ssh_string_fill(s, (char *)ssh_string_data(sig_blob) + 20, 20);
    if (rc < 0) {
        SSH_STRING_FREE(s);
        goto error;
    }

    ps = ssh_make_string_bn(s);
    ssh_string_burn(s);
    SSH_STRING_FREE(s);
    if (ps == NULL) {
        goto error;
    }

    dsa_sig = DSA_SIG_new();
    if (dsa_sig == NULL) {
        goto error;
    }

    /* Memory management of pr and ps is transferred to DSA signature
     * object */
    rc = DSA_SIG_set0(dsa_sig, pr, ps);
    if (rc == 0) {
        goto error;
    }
    ps = NULL;
    pr = NULL;

    /* Get the expected size of the buffer */
    rc = i2d_DSA_SIG(dsa_sig, NULL);
    if (rc <= 0) {
        goto error;
    }
    raw_sig_len = rc;

    raw_sig_data = (unsigned char *)calloc(1, raw_sig_len);
    if (raw_sig_data == NULL) {
        goto error;
    }
    temp_raw_sig = raw_sig_data;

    /* It is necessary to use a temporary pointer as i2d_* "advances" the
     * pointer */
    raw_sig_len = i2d_DSA_SIG(dsa_sig, &temp_raw_sig);
    if (raw_sig_len <= 0) {
        goto error;
    }

    sig->raw_sig = ssh_string_new(raw_sig_len);
    if (sig->raw_sig == NULL) {
        explicit_bzero(raw_sig_data, raw_sig_len);
        goto error;
    }

    rc = ssh_string_fill(sig->raw_sig, raw_sig_data, raw_sig_len);
    if (rc < 0) {
        explicit_bzero(raw_sig_data, raw_sig_len);
        goto error;
    }

    explicit_bzero(raw_sig_data, raw_sig_len);
    SAFE_FREE(raw_sig_data);
    DSA_SIG_free(dsa_sig);

    return SSH_OK;

error:
    bignum_safe_free(ps);
    bignum_safe_free(pr);
    SAFE_FREE(raw_sig_data);
    DSA_SIG_free(dsa_sig);
    return SSH_ERROR;
}

static int pki_signature_from_ecdsa_blob(UNUSED_PARAM(const ssh_key pubkey),
                                         const ssh_string sig_blob,
                                         ssh_signature sig)
{
    ECDSA_SIG *ecdsa_sig = NULL;
    BIGNUM *pr = NULL, *ps = NULL;

    ssh_string r;
    ssh_string s;

    ssh_buffer buf = NULL;
    uint32_t rlen;

    unsigned char *raw_sig_data = NULL;
    unsigned char *temp_raw_sig = NULL;
    size_t raw_sig_len = 0;

    int rc;

    /* build ecdsa signature */
    buf = ssh_buffer_new();
    if (buf == NULL) {
        return SSH_ERROR;
    }

    rc = ssh_buffer_add_data(buf,
                             ssh_string_data(sig_blob),
                             ssh_string_len(sig_blob));
    if (rc < 0) {
        goto error;
    }

    r = ssh_buffer_get_ssh_string(buf);
    if (r == NULL) {
        goto error;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("r", ssh_string_data(r), ssh_string_len(r));
#endif

    pr = ssh_make_string_bn(r);
    ssh_string_burn(r);
    SSH_STRING_FREE(r);
    if (pr == NULL) {
        goto error;
    }

    s = ssh_buffer_get_ssh_string(buf);
    rlen = ssh_buffer_get_len(buf);
    SSH_BUFFER_FREE(buf);
    if (s == NULL) {
        goto error;
    }

    if (rlen != 0) {
        ssh_string_burn(s);
        SSH_STRING_FREE(s);
        SSH_LOG(SSH_LOG_WARN,
                "Signature has remaining bytes in inner "
                "sigblob: %lu",
                (unsigned long)rlen);
        goto error;
    }

#ifdef DEBUG_CRYPTO
    ssh_log_hexdump("s", ssh_string_data(s), ssh_string_len(s));
#endif

    ps = ssh_make_string_bn(s);
    ssh_string_burn(s);
    SSH_STRING_FREE(s);
    if (ps == NULL) {
        goto error;
    }

    ecdsa_sig = ECDSA_SIG_new();
    if (ecdsa_sig == NULL) {
        goto error;
    }

    /* Memory management of pr and ps is transferred to
     * ECDSA signature object */
    rc = ECDSA_SIG_set0(ecdsa_sig, pr, ps);
    if (rc == 0) {
        goto error;
    }
    pr = NULL;
    ps = NULL;

    /* Get the expected size of the buffer */
    rc = i2d_ECDSA_SIG(ecdsa_sig, NULL);
    if (rc <= 0) {
        goto error;
    }
    raw_sig_len = rc;

    raw_sig_data = (unsigned char *)calloc(1, raw_sig_len);
    if (raw_sig_data == NULL) {
        goto error;
    }
    temp_raw_sig = raw_sig_data;

    /* It is necessary to use a temporary pointer as i2d_* "advances" the
     * pointer */
    rc = i2d_ECDSA_SIG(ecdsa_sig, &temp_raw_sig);
    if (rc <= 0) {
        goto error;
    }

    sig->raw_sig = ssh_string_new(raw_sig_len);
    if (sig->raw_sig == NULL) {
        explicit_bzero(raw_sig_data, raw_sig_len);
        goto error;
    }

    rc = ssh_string_fill(sig->raw_sig, raw_sig_data, raw_sig_len);
    if (rc < 0) {
        explicit_bzero(raw_sig_data, raw_sig_len);
        goto error;
    }

    explicit_bzero(raw_sig_data, raw_sig_len);
    SAFE_FREE(raw_sig_data);
    ECDSA_SIG_free(ecdsa_sig);
    return SSH_OK;

error:
    SSH_BUFFER_FREE(buf);
    bignum_safe_free(ps);
    bignum_safe_free(pr);
    SAFE_FREE(raw_sig_data);
    if (ecdsa_sig != NULL) {
        ECDSA_SIG_free(ecdsa_sig);
    }
    return SSH_ERROR;
}

ssh_signature pki_signature_from_blob(const ssh_key pubkey,
                                      const ssh_string sig_blob,
                                      enum ssh_keytypes_e type,
                                      enum ssh_digest_e hash_type)
{
    ssh_signature sig;
    int rc;

    if (ssh_key_type_plain(pubkey->type) != type) {
        SSH_LOG(SSH_LOG_WARN,
                "Incompatible public key provided (%d) expecting (%d)",
                type,
                pubkey->type);
        return NULL;
    }

    sig = ssh_signature_new();
    if (sig == NULL) {
        return NULL;
    }

    sig->type = type;
    sig->type_c = ssh_key_signature_to_char(type, hash_type);
    sig->hash_type = hash_type;

    switch(type) {
        case SSH_KEYTYPE_DSS:
            rc = pki_signature_from_dsa_blob(pubkey, sig_blob, sig);
            if (rc != SSH_OK) {
                goto error;
            }
            break;
        case SSH_KEYTYPE_RSA:
        case SSH_KEYTYPE_RSA1:
            rc = pki_signature_from_rsa_blob(pubkey, sig_blob, sig);
            if (rc != SSH_OK) {
                goto error;
            }
            break;
        case SSH_KEYTYPE_ED25519:
        case SSH_KEYTYPE_SK_ED25519:
            rc = pki_signature_from_ed25519_blob(sig, sig_blob);
            if (rc != SSH_OK){
                goto error;
            }
            break;
        case SSH_KEYTYPE_ECDSA_P256:
        case SSH_KEYTYPE_ECDSA_P384:
        case SSH_KEYTYPE_ECDSA_P521:
        case SSH_KEYTYPE_ECDSA_P256_CERT01:
        case SSH_KEYTYPE_ECDSA_P384_CERT01:
        case SSH_KEYTYPE_ECDSA_P521_CERT01:
        case SSH_KEYTYPE_SK_ECDSA:
        case SSH_KEYTYPE_SK_ECDSA_CERT01:
#ifdef HAVE_OPENSSL_ECC
            rc = pki_signature_from_ecdsa_blob(pubkey, sig_blob, sig);
            if (rc != SSH_OK) {
                goto error;
            }
            break;
#endif
        default:
        case SSH_KEYTYPE_UNKNOWN:
            SSH_LOG(SSH_LOG_WARN, "Unknown signature type");
            goto error;
    }

    return sig;

error:
    ssh_signature_free(sig);
    return NULL;
}

static const EVP_MD *pki_digest_to_md(enum ssh_digest_e hash_type)
{
    const EVP_MD *md = NULL;

    switch (hash_type) {
    case SSH_DIGEST_SHA256:
        md = EVP_sha256();
        break;
    case SSH_DIGEST_SHA384:
        md = EVP_sha384();
        break;
    case SSH_DIGEST_SHA512:
        md = EVP_sha512();
        break;
    case SSH_DIGEST_SHA1:
        md = EVP_sha1();
        break;
    case SSH_DIGEST_AUTO:
        md = NULL;
        break;
    default:
        SSH_LOG(SSH_LOG_TRACE, "Unknown hash algorithm for type: %d",
                hash_type);
        return NULL;
    }

    return md;
}

static EVP_PKEY *pki_key_to_pkey(ssh_key key)
{
    EVP_PKEY *pkey = NULL;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    int rc = 0;
#endif

    switch (key->type) {
    case SSH_KEYTYPE_DSS:
    case SSH_KEYTYPE_DSS_CERT01:
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        if (key->dsa == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "NULL key->dsa");
            goto error;
        }
        pkey = EVP_PKEY_new();
        if (pkey == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "Out of memory");
            return NULL;
        }

        EVP_PKEY_set1_DSA(pkey, key->dsa);
        break;
#endif /* OPENSSL_VERSION_NUMBER */
    case SSH_KEYTYPE_RSA:
    case SSH_KEYTYPE_RSA1:
    case SSH_KEYTYPE_RSA_CERT01:
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        if (key->rsa == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "NULL key->rsa");
            goto error;
        }
        pkey = EVP_PKEY_new();
        if (pkey == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "Out of memory");
            return NULL;
        }

        EVP_PKEY_set1_RSA(pkey, key->rsa);
        break;
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * Remove this #else part from here
 */
#else
        if (key->key == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "NULL key->key");
            goto error;
        }
        rc = EVP_PKEY_up_ref(key->key);
        if (rc != 1) {
            SSH_LOG(SSH_LOG_TRACE, "Failed to reference EVP_PKEY");
            return NULL;
        }
        pkey = key->key;
        break;
#endif /* OPENSSL_VERSION_NUMBER */
    case SSH_KEYTYPE_ECDSA_P256:
    case SSH_KEYTYPE_ECDSA_P384:
    case SSH_KEYTYPE_ECDSA_P521:
    case SSH_KEYTYPE_ECDSA_P256_CERT01:
    case SSH_KEYTYPE_ECDSA_P384_CERT01:
    case SSH_KEYTYPE_ECDSA_P521_CERT01:
    case SSH_KEYTYPE_SK_ECDSA:
    case SSH_KEYTYPE_SK_ECDSA_CERT01:
# if defined(HAVE_OPENSSL_ECC)
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
        if (key->ecdsa == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "NULL key->ecdsa");
            goto error;
        }
        pkey = EVP_PKEY_new();
        if (pkey == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "Out of memory");
            return NULL;
        }

        EVP_PKEY_set1_EC_KEY(pkey, key->ecdsa);
        break;
#endif /* OPENSSL_VERSION_NUMBER */
# endif
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER >= 0x30000000L
 */
#if 0
        if (key->key == NULL) {
            SSH_LOG(SSH_LOG_TRACE, "NULL key->key");
            goto error;
        }
        rc = EVP_PKEY_uo_ref(key->key);
        if (rc != 1) {
            SSH_LOG(SSH_LOG_TRACE, "Failed to reference EVP_PKEY");
            return NULL;
        }
        pkey = key->key;
        break;
#endif /* OPENSSL_VERSION_NUMBER */
    case SSH_KEYTYPE_ED25519:
    case SSH_KEYTYPE_ED25519_CERT01:
    case SSH_KEYTYPE_SK_ED25519:
    case SSH_KEYTYPE_SK_ED25519_CERT01:
# if defined(HAVE_OPENSSL_ED25519)
        if (ssh_key_is_private(key)) {
            if (key->ed25519_privkey == NULL) {
                SSH_LOG(SSH_LOG_TRACE, "NULL key->ed25519_privkey");
                goto error;
            }
            /* In OpenSSL, the input is the private key seed only, which means
             * the first half of the SSH private key (the second half is the
             * public key). Both keys have the same length (32 bytes) */
            pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL,
                    (const uint8_t *)key->ed25519_privkey,
                    ED25519_KEY_LEN);
        } else {
            if (key->ed25519_pubkey == NULL) {
                SSH_LOG(SSH_LOG_TRACE, "NULL key->ed25519_pubkey");
                goto error;
            }
            pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL,
                    (const uint8_t *)key->ed25519_pubkey,
                    ED25519_KEY_LEN);
        }
        if (pkey == NULL) {
            SSH_LOG(SSH_LOG_TRACE,
                    "Failed to create ed25519 EVP_PKEY: %s",
                    ERR_error_string(ERR_get_error(), NULL));
            return NULL;
        }
        break;
#endif
    case SSH_KEYTYPE_UNKNOWN:
    default:
        SSH_LOG(SSH_LOG_TRACE, "Unknown private key algorithm for type: %d",
                key->type);
        goto error;
    }

    return pkey;

error:
    EVP_PKEY_free(pkey);
    return NULL;
}

/**
 * @internal
 *
 * @brief Sign the given input data. The digest to be signed is calculated
 * internally as necessary.
 *
 * @param[in]   privkey     The private key to be used for signing.
 * @param[in]   hash_type   The digest algorithm to be used.
 * @param[in]   input       The data to be signed.
 * @param[in]   input_len   The length of the data to be signed.
 *
 * @return  a newly allocated ssh_signature or NULL on error.
 */
ssh_signature pki_sign_data(const ssh_key privkey,
                            enum ssh_digest_e hash_type,
                            const unsigned char *input,
                            size_t input_len)
{
    const EVP_MD *md = NULL;
    EVP_MD_CTX *ctx = NULL;
    EVP_PKEY *pkey = NULL;

    unsigned char *raw_sig_data = NULL;
    size_t raw_sig_len;

    ssh_signature sig = NULL;

    int rc;

    if (privkey == NULL || !ssh_key_is_private(privkey) || input == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Bad parameter provided to "
                               "pki_sign_data()");
        return NULL;
    }

    /* Check if public key and hash type are compatible */
    rc = pki_key_check_hash_compatible(privkey, hash_type);
    if (rc != SSH_OK) {
        return NULL;
    }

#ifndef HAVE_OPENSSL_ED25519
    if (privkey->type == SSH_KEYTYPE_ED25519 ||
        privkey->type == SSH_KEYTYPE_ED25519_CERT01)
    {
        return pki_do_sign_hash(privkey, input, input_len, hash_type);
    }
#endif

    /* Set hash algorithm to be used */
    md = pki_digest_to_md(hash_type);
    if (md == NULL) {
        if (hash_type != SSH_DIGEST_AUTO) {
            return NULL;
        }
    }

    /* Setup private key EVP_PKEY */
    pkey = pki_key_to_pkey(privkey);
    if (pkey == NULL) {
        return NULL;
    }

    /* Allocate buffer for signature */
    raw_sig_len = (size_t)EVP_PKEY_size(pkey);
    raw_sig_data = (unsigned char *)malloc(raw_sig_len);
    if (raw_sig_data == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Out of memory");
        goto out;
    }

    /* Create the context */
    ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        SSH_LOG(SSH_LOG_TRACE, "Out of memory");
        goto out;
    }

    /* Sign the data */
    rc = EVP_DigestSignInit(ctx, NULL, md, NULL, pkey);
    if (rc != 1){
        SSH_LOG(SSH_LOG_TRACE,
                "EVP_DigestSignInit() failed: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

#ifdef HAVE_OPENSSL_EVP_DIGESTSIGN
    rc = EVP_DigestSign(ctx, raw_sig_data, &raw_sig_len, input, input_len);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "EVP_DigestSign() failed: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }
#else
    rc = EVP_DigestSignUpdate(ctx, input, input_len);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "EVP_DigestSignUpdate() failed: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

    rc = EVP_DigestSignFinal(ctx, raw_sig_data, &raw_sig_len);
    if (rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "EVP_DigestSignFinal() failed: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }
#endif

#ifdef DEBUG_CRYPTO
        ssh_log_hexdump("Generated signature", raw_sig_data, raw_sig_len);
#endif

    /* Allocate and fill output signature */
    sig = ssh_signature_new();
    if (sig == NULL) {
        goto out;
    }

    sig->raw_sig = ssh_string_new(raw_sig_len);
    if (sig->raw_sig == NULL) {
        ssh_signature_free(sig);
        sig = NULL;
        goto out;
    }

    rc = ssh_string_fill(sig->raw_sig, raw_sig_data, raw_sig_len);
    if (rc < 0) {
        ssh_signature_free(sig);
        sig = NULL;
        goto out;
    }

    sig->type = privkey->type;
    sig->hash_type = hash_type;
    sig->type_c = ssh_key_signature_to_char(privkey->type, hash_type);

out:
    if (ctx != NULL) {
        EVP_MD_CTX_free(ctx);
    }
    if (raw_sig_data != NULL) {
        explicit_bzero(raw_sig_data, raw_sig_len);
    }
    SAFE_FREE(raw_sig_data);
    EVP_PKEY_free(pkey);
    return sig;
}

/**
 * @internal
 *
 * @brief Verify the signature of a given input. The digest of the input is
 * calculated internally as necessary.
 *
 * @param[in]   signature   The signature to be verified.
 * @param[in]   pubkey      The public key used to verify the signature.
 * @param[in]   input       The signed data.
 * @param[in]   input_len   The length of the signed data.
 *
 * @return  SSH_OK if the signature is valid; SSH_ERROR otherwise.
 */
int pki_verify_data_signature(ssh_signature signature,
                              const ssh_key pubkey,
                              const unsigned char *input,
                              size_t input_len)
{
    const EVP_MD *md = NULL;
    EVP_MD_CTX *ctx = NULL;
    EVP_PKEY *pkey = NULL;

    unsigned char *raw_sig_data = NULL;
    unsigned int raw_sig_len;

    /* Function return code
     * Do not change this variable throughout the function until the signature
     * is successfully verified!
     */
    int rc = SSH_ERROR;
    int ok;

    if (pubkey == NULL || ssh_key_is_private(pubkey) || input == NULL ||
        signature == NULL || (signature->raw_sig == NULL
#ifndef HAVE_OPENSSL_ED25519
        && signature->ed25519_sig == NULL
#endif
        ))
    {
        SSH_LOG(SSH_LOG_TRACE, "Bad parameter provided to "
                               "pki_verify_data_signature()");
        return SSH_ERROR;
    }

    /* Check if public key and hash type are compatible */
    ok = pki_key_check_hash_compatible(pubkey, signature->hash_type);
    if (ok != SSH_OK) {
        return SSH_ERROR;
    }

#ifndef HAVE_OPENSSL_ED25519
    if (pubkey->type == SSH_KEYTYPE_ED25519 ||
        pubkey->type == SSH_KEYTYPE_ED25519_CERT01 ||
        pubkey->type == SSH_KEYTYPE_SK_ED25519 ||
        pubkey->type == SSH_KEYTYPE_SK_ED25519_CERT01)
    {
        return pki_ed25519_verify(pubkey, signature, input, input_len);
    }
#endif

    /* Get the signature to be verified */
    raw_sig_data = ssh_string_data(signature->raw_sig);
    raw_sig_len = ssh_string_len(signature->raw_sig);
    if (raw_sig_data == NULL) {
        return SSH_ERROR;
    }

    /* Set hash algorithm to be used */
    md = pki_digest_to_md(signature->hash_type);
    if (md == NULL) {
        if (signature->hash_type != SSH_DIGEST_AUTO) {
            return SSH_ERROR;
        }
    }

    /* Setup public key EVP_PKEY */
    pkey = pki_key_to_pkey(pubkey);
    if (pkey == NULL) {
        return SSH_ERROR;
    }

    /* Create the context */
    ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to create EVP_MD_CTX: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

    /* Verify the signature */
    ok = EVP_DigestVerifyInit(ctx, NULL, md, NULL, pkey);
    if (ok != 1){
        SSH_LOG(SSH_LOG_TRACE,
                "EVP_DigestVerifyInit() failed: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

#ifdef HAVE_OPENSSL_EVP_DIGESTVERIFY
    ok = EVP_DigestVerify(ctx, raw_sig_data, raw_sig_len, input, input_len);
#else
    ok = EVP_DigestVerifyUpdate(ctx, input, input_len);
    if (ok != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "EVP_DigestVerifyUpdate() failed: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

    ok = EVP_DigestVerifyFinal(ctx, raw_sig_data, raw_sig_len);
#endif
    if (ok != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Signature invalid: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto out;
    }

    SSH_LOG(SSH_LOG_TRACE, "Signature valid");
    rc = SSH_OK;

out:
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return rc;
}

int ssh_key_size(ssh_key key)
{
    int bits = 0;
    EVP_PKEY *pkey = NULL;

    switch (key->type) {
    case SSH_KEYTYPE_DSS:
    case SSH_KEYTYPE_DSS_CERT01:
    case SSH_KEYTYPE_RSA:
    case SSH_KEYTYPE_RSA_CERT01:
    case SSH_KEYTYPE_RSA1:
    case SSH_KEYTYPE_ECDSA_P256:
    case SSH_KEYTYPE_ECDSA_P256_CERT01:
    case SSH_KEYTYPE_ECDSA_P384:
    case SSH_KEYTYPE_ECDSA_P384_CERT01:
    case SSH_KEYTYPE_ECDSA_P521:
    case SSH_KEYTYPE_ECDSA_P521_CERT01:
    case SSH_KEYTYPE_SK_ECDSA:
    case SSH_KEYTYPE_SK_ECDSA_CERT01:
        pkey = pki_key_to_pkey(key);
        if (pkey == NULL) {
            return SSH_ERROR;
        }
        bits = EVP_PKEY_bits(pkey);
        EVP_PKEY_free(pkey);
        return bits;
    case SSH_KEYTYPE_ED25519:
    case SSH_KEYTYPE_ED25519_CERT01:
    case SSH_KEYTYPE_SK_ED25519:
    case SSH_KEYTYPE_SK_ED25519_CERT01:
        /* ed25519 keys have fixed size */
        return 255;
    case SSH_KEYTYPE_UNKNOWN:
    default:
        return SSH_ERROR;
    }
}

#ifdef HAVE_OPENSSL_ED25519
int pki_key_generate_ed25519(ssh_key key)
{
    int evp_rc;
    EVP_PKEY_CTX *pctx = NULL;
    EVP_PKEY *pkey = NULL;
    size_t privkey_len = ED25519_KEY_LEN;
    size_t pubkey_len = ED25519_KEY_LEN;

    if (key == NULL) {
        return SSH_ERROR;
    }

    pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    if (pctx == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to create ed25519 EVP_PKEY_CTX: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto error;
    }

    evp_rc = EVP_PKEY_keygen_init(pctx);
    if (evp_rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to initialize ed25519 key generation: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto error;
    }

    evp_rc = EVP_PKEY_keygen(pctx, &pkey);
    if (evp_rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to generate ed25519 key: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto error;
    }

    key->ed25519_privkey = malloc(ED25519_KEY_LEN);
    if (key->ed25519_privkey == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to allocate memory for ed25519 private key");
        goto error;
    }

    key->ed25519_pubkey = malloc(ED25519_KEY_LEN);
    if (key->ed25519_pubkey == NULL) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to allocate memory for ed25519 public key");
        goto error;
    }

    evp_rc = EVP_PKEY_get_raw_private_key(pkey, (uint8_t *)key->ed25519_privkey,
                                          &privkey_len);
    if (evp_rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to get ed25519 raw private key: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto error;
    }

    evp_rc = EVP_PKEY_get_raw_public_key(pkey, (uint8_t *)key->ed25519_pubkey,
                                         &pubkey_len);
    if (evp_rc != 1) {
        SSH_LOG(SSH_LOG_TRACE,
                "Failed to get ed25519 raw public key: %s",
                ERR_error_string(ERR_get_error(), NULL));
        goto error;
    }

    EVP_PKEY_CTX_free(pctx);
    EVP_PKEY_free(pkey);
    return SSH_OK;

error:
    if (pctx != NULL) {
        EVP_PKEY_CTX_free(pctx);
    }
    if (pkey != NULL) {
        EVP_PKEY_free(pkey);
    }
    SAFE_FREE(key->ed25519_privkey);
    SAFE_FREE(key->ed25519_pubkey);

    return SSH_ERROR;
}
#else
ssh_signature pki_do_sign_hash(const ssh_key privkey,
                               const unsigned char *hash,
                               size_t hlen,
                               enum ssh_digest_e hash_type)
{
    ssh_signature sig = NULL;
    int rc;

    sig = ssh_signature_new();
    if (sig == NULL) {
        return NULL;
    }

    sig->type = privkey->type;
    sig->type_c = ssh_key_signature_to_char(privkey->type, hash_type);
    sig->hash_type = hash_type;

    switch(privkey->type) {
        case SSH_KEYTYPE_ED25519:
            rc = pki_ed25519_sign(privkey, sig, hash, hlen);
            if (rc != SSH_OK) {
                ssh_signature_free(sig);
                return NULL;
            }
            break;
        default:
            ssh_signature_free(sig);
            return NULL;
    }

    return sig;
}
#endif /* HAVE_OPENSSL_ED25519 */

#ifdef WITH_PKCS11_URI
/**
 * @internal
 *
 * @brief Populate the public/private ssh_key from the engine with
 * PKCS#11 URIs as the look up.
 *
 * @param[in]   uri_name    The PKCS#11 URI
 * @param[in]   nkey        The ssh-key context for
 *                          the key loaded from the engine.
 * @param[in]   key_type    The type of the key used. Public/Private.
 *
 * @return  SSH_OK if ssh-key is valid; SSH_ERROR otherwise.
 */
int pki_uri_import(const char *uri_name,
                     ssh_key *nkey,
                     enum ssh_key_e key_type)
{
    ENGINE *engine = NULL;
    EVP_PKEY *pkey = NULL;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    RSA *rsa = NULL;
#endif
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * Move HAVE_OPENSSL_ECC #ifdef into #if above
 */
#ifdef HAVE_OPENSSL_ECC
    EC_KEY *ecdsa = NULL;
#else
    void *ecdsa = NULL;
#endif
    ssh_key key = NULL;
    enum ssh_keytypes_e type = SSH_KEYTYPE_UNKNOWN;

    /* Do the init only once */
    engine = pki_get_engine();
    if (engine == NULL) {
        SSH_LOG(SSH_LOG_WARN, "Failed to initialize engine");
        goto fail;
    }

    switch (key_type) {
    case SSH_KEY_PRIVATE:
        pkey = ENGINE_load_private_key(engine, uri_name, NULL, NULL);
        if (pkey == NULL) {
            SSH_LOG(SSH_LOG_WARN,
                    "Could not load key: %s",
                    ERR_error_string(ERR_get_error(),NULL));
            goto fail;
        }
        break;
    case SSH_KEY_PUBLIC:
        pkey = ENGINE_load_public_key(engine, uri_name, NULL, NULL);
        if (pkey == NULL) {
            SSH_LOG(SSH_LOG_WARN,
                    "Could not load key: %s",
                    ERR_error_string(ERR_get_error(),NULL));
            goto fail;
        }
        break;
    default:
        SSH_LOG(SSH_LOG_WARN,
                "Invalid key type: %d", key_type);
        goto fail;
    }

    key = ssh_key_new();
    if (key == NULL) {
        goto fail;
    }

    switch (EVP_PKEY_base_id(pkey)) {
    case EVP_PKEY_RSA:
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        rsa = EVP_PKEY_get1_RSA(pkey);
        if (rsa == NULL) {
            SSH_LOG(SSH_LOG_WARN,
                    "Parsing pub key: %s",
                    ERR_error_string(ERR_get_error(),NULL));
            goto fail;
        }
#endif /* OPENSSL_VERSION_NUMBER */
        type = SSH_KEYTYPE_RSA;
        break;
    case EVP_PKEY_EC:
#ifdef HAVE_OPENSSL_ECC
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
        ecdsa = EVP_PKEY_get1_EC_KEY(pkey);
        if (ecdsa == NULL) {
            SSH_LOG(SSH_LOG_WARN,
                    "Parsing pub key: %s",
                    ERR_error_string(ERR_get_error(), NULL));
            goto fail;
        }

        /* pki_privatekey_type_from_string always returns P256 for ECDSA
         * keys, so we need to figure out the correct type here */
        type = pki_key_ecdsa_to_key_type(ecdsa);
#else
        type = pki_key_ecdsa_to_key_type(pkey);
#endif /* OPENSSL_VERSION_NUMBER */
        if (type == SSH_KEYTYPE_UNKNOWN) {
            SSH_LOG(SSH_LOG_WARN, "Invalid pub key.");
            goto fail;
        }

        break;
#endif
    default:
        SSH_LOG(SSH_LOG_WARN, "Unknown or invalid public key type %d",
                EVP_PKEY_base_id(pkey));
        goto fail;
    }

    key->key = pkey;
    key->type = type;
    key->type_c = ssh_key_type_to_char(type);
    key->flags = SSH_KEY_FLAG_PUBLIC | SSH_KEY_FLAG_PKCS11_URI;
    if (key_type == SSH_KEY_PRIVATE) {
        key->flags |= SSH_KEY_FLAG_PRIVATE;
    }
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    key->rsa = rsa;
#endif
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * Move line key->ecdsa into #if above
 */
    key->ecdsa = ecdsa;
#ifdef HAVE_OPENSSL_ECC
    if (is_ecdsa_key_type(key->type)) {
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * #if OPENSSL_VERSION_NUMBER < 0x30000000L
 */
#if 1
        key->ecdsa_nid = pki_key_ecdsa_to_nid(key->ecdsa);
#else
        key->ecdsa_nid = pki_key_ecdsa_to_nid(key->key);
#endif /* OPENSSL_VERSION_NUMBER */
    }
#endif

    *nkey = key;

    return SSH_OK;

fail:
    EVP_PKEY_free(pkey);
    ssh_key_free(key);
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    RSA_free(rsa);
#endif
/* TODO Change to new API when the OpenSSL will support export of uncompressed EC keys
 * https://github.com/openssl/openssl/pull/16624
 * Move HAVE_OPENSSL_ECC #ifdef into #if above
 */
#ifdef HAVE_OPENSSL_ECC
    EC_KEY_free(ecdsa);
#endif

    return SSH_ERROR;
}
#endif /* WITH_PKCS11_URI */

#endif /* _PKI_CRYPTO_H */
