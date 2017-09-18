/* Copyright (C) 2007, 2008, 2009 Thorsten Kukuk
   Author: Thorsten Kukuk <kukuk@thkukuk.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypt-private.h"
#include "crypt-obsolete.h"

#define CRYPT_GENSALT_OUTPUT_SIZE       (7 + 22 + 1)

/* Static buffer used by crypt() and bigcrypt().  */
static struct crypt_data nr_crypt_ctx;

struct hashfn
{
  const char *prefix;
  char *(*crypt) (const char *key, const char *salt, char *data, size_t size);
  char *(*gensalt) (unsigned long count,
                    const char *input, int input_size,
                    char *output, int output_size);
};

/* This table should always begin with the algorithm that should be used
   for new encryptions.  */
static const struct hashfn tagged_hashes[] = {
  /* bcrypt */
  { "$2a$", crypt_bcrypt_rn, gensalt_bcrypt_a_rn },
  { "$2b$", crypt_bcrypt_rn, gensalt_bcrypt_b_rn },
  { "$2x$", crypt_bcrypt_rn, gensalt_bcrypt_x_rn },
  { "$2y$", crypt_bcrypt_rn, gensalt_bcrypt_y_rn },

  /* legacy hashes */
#if ENABLE_WEAK_HASHES
  { "$1$", crypt_md5_rn, gensalt_md5_rn },
#endif
  { "$5$", crypt_sha256_rn, gensalt_sha256_rn },
  { "$6$", crypt_sha512_rn, gensalt_sha512_rn },
  { 0, 0, 0 }
};

#if ENABLE_WEAK_HASHES
/* BSD-style extended DES */
static const struct hashfn bsdi_extended_hash = {
  "_", crypt_des_xbsd_rn, gensalt_des_xbsd_rn
};

/* Traditional DES or bigcrypt-style extended DES */
static const struct hashfn traditional_hash = {
  "", crypt_des_trd_or_big_rn, gensalt_des_trd_rn
};

static int
is_des_salt_char (char c)
{
  return ((c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') ||
          c == '.' || c == '/');
}
#endif /* ENABLE_WEAK_HASHES */

static const struct hashfn *
get_hashfn (const char *salt)
{
  if (salt[0] == '$')
    {
      const struct hashfn *h;
      for (h = tagged_hashes; h->prefix; h++)
        if (!strncmp (salt, h->prefix, strlen (h->prefix)))
          return h;
      return NULL;
    }
#if ENABLE_WEAK_HASHES
  else if (salt[0] == '_')
    return &bsdi_extended_hash;
  else if (salt[0] == '\0' ||
           (is_des_salt_char (salt[0]) && is_des_salt_char (salt[1])))
    return &traditional_hash;
#endif
  else
    return NULL;
}

/* For historical reasons, crypt and crypt_r are not expected ever
   to return NULL.  This function generates a "failure token" in the
   output buffer, which is guaranteed not to be equal to any valid
   password hash, or to the salt(+hash) string; thus, a subsequent
   blind attempt to authenticate someone by comparing the output to
   a previously recorded hash string will fail, even if that string
   is itself one of these "failure tokens".  */

static void
make_failure_token (const char *salt, char *output, int size)
{
  if (size < 3)
    return;

  output[0] = '*';
  output[1] = '0';
  output[2] = '\0';

  if (salt[0] == '*' && salt[1] == '0')
    output[1] = '1';
}

static char *
do_crypt_rn (const char *key, const char *salt, char *data, int size)
{
  if (size <= 0)
    {
      errno = ERANGE;
      return NULL;
    }
  const struct hashfn *h = get_hashfn (salt);
  if (!h)
    {
      /* Unrecognized hash algorithm */
      errno = EINVAL;
      return NULL;
    }
  return h->crypt (key, salt, data, (size_t)size);
}

static char *
do_crypt_ra (const char *key, const char *salt, void **data, int *size)
{
  const struct hashfn *h = get_hashfn (salt);
  if (!h)
    {
      /* Unrecognized hash algorithm */
      errno = EINVAL;
      return NULL;
    }

  if (!*data)
    {
      *data = malloc (sizeof (struct crypt_data));
      if (!*data)
        return NULL;
      *size = sizeof (struct crypt_data);
    }

  if (*size <= 0)
    {
      errno = ERANGE;
      return NULL;
    }

  return h->crypt (key, salt, *data, (size_t)*size);
}

char *
crypt_rn (const char *key, const char *salt, void *data, int size)
{
  char *retval = do_crypt_rn (key, salt, data, size);
  if (!retval)
    make_failure_token (salt, data, size);
  return retval;
}

char *
crypt_ra (const char *key, const char *salt, void **data, int *size)
{
  char *retval = do_crypt_ra (key, salt, data, size);
  if (!retval)
    make_failure_token (salt, *data, *size);
  return retval;
}

char *
crypt_r (const char *key, const char *salt, struct crypt_data *data)
{
  char *retval = crypt_rn (key, salt, (char *) data, sizeof (*data));
  if (!retval)
    return (char *)data; /* return the failure token */
  return retval;
}
#if COMPAT_crypt_r__glibc
default_symbol(crypt_r, crypt_r);
#endif

/* 'crypt' and 'crypt_r' exist in two symbol versions because the
   definition of 'struct crypt_data' has changed.  We don't need any
   special case code here to make the new library work with programs
   that use the old definition, but programs that use the new
   definition will not work with the old library.  */
#if COMPAT_crypt_r__glibc
strong_alias(crypt_r, crypt_r__glibc);
compat_symbol(crypt_r, crypt_r__glibc);
#endif

char *
crypt (const char *key, const char *salt)
{
  return crypt_r (key, salt, &nr_crypt_ctx);
}
#if COMPAT_crypt__glibc
default_symbol(crypt, crypt);
#endif

#if COMPAT_crypt__glibc
strong_alias(crypt, crypt__glibc);
compat_symbol(crypt, crypt__glibc);
#endif

#if COMPAT_fcrypt
strong_alias (crypt, fcrypt);
compat_symbol (fcrypt, fcrypt);
#endif

char *
crypt_gensalt_rn (const char *prefix, unsigned long count,
                  const char *input, int size, char *output,
                  int output_size)
{
  const struct hashfn *h;

  /* This may be supported on some platforms in the future */
  if (!input)
    {
      errno = EINVAL;
      return NULL;
    }

  h = get_hashfn (prefix);
  if (!h)
    {
      errno = EINVAL;
      return NULL;
    }
  return h->gensalt (count, input, size, output, output_size);
}

char *
crypt_gensalt_ra (const char *prefix, unsigned long count,
                  const char *input, int size)
{
  char *output = malloc (CRYPT_GENSALT_OUTPUT_SIZE);
  if (!output)
    return output;

  return crypt_gensalt_rn (prefix, count, input, size, output,
                           CRYPT_GENSALT_OUTPUT_SIZE);
}

char *
crypt_gensalt (const char *prefix, unsigned long count,
               const char *input, int size)
{
  static char output[CRYPT_GENSALT_OUTPUT_SIZE];

  return crypt_gensalt_rn (prefix, count,
                           input, size, output, sizeof (output));
}

#if COMPAT_bigcrypt
/* Obsolete interface - not to be used in new code.  This function is
   the same as crypt, but it forces the use of the Digital Unix
   "bigcrypt" hash, which is nearly as weak as traditional DES.
   Because it is obsolete, we have not added a reentrant version.  */
char *
bigcrypt (const char *key, const char *salt)
{
  char *retval = crypt_des_big_rn
    (key, salt, (char *)&nr_crypt_ctx, sizeof nr_crypt_ctx);
  if (retval)
    return retval;
  make_failure_token (salt, (char *)&nr_crypt_ctx, sizeof nr_crypt_ctx);
  return (char *)&nr_crypt_ctx;
}
compat_symbol (bigcrypt, bigcrypt);
#endif