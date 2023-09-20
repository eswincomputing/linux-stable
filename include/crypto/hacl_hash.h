/* 
 * Copyright (c) 2016-2022 INRIA, CMU and Microsoft Corporation
 * Copyright (c) 2022-2023 HACL* Contributors
 * Copyright (c) 2023 Cryspen
 */


#ifndef __Hacl_Hash_H
#define __Hacl_Hash_H

#include "hacl_lib.h"

#define Spec_Hash_Definitions_SHA2_224 0
#define Spec_Hash_Definitions_SHA2_256 1
#define Spec_Hash_Definitions_SHA2_384 2
#define Spec_Hash_Definitions_SHA2_512 3
#define Spec_Hash_Definitions_SHA1 4
#define Spec_Hash_Definitions_MD5 5
#define Spec_Hash_Definitions_Blake2S 6
#define Spec_Hash_Definitions_Blake2B 7
#define Spec_Hash_Definitions_SHA3_256 8
#define Spec_Hash_Definitions_SHA3_224 9
#define Spec_Hash_Definitions_SHA3_384 10
#define Spec_Hash_Definitions_SHA3_512 11
#define Spec_Hash_Definitions_Shake128 12
#define Spec_Hash_Definitions_Shake256 13

typedef uint8_t Spec_Hash_Definitions_hash_alg;

#define Hacl_Streaming_Types_Success 0
#define Hacl_Streaming_Types_InvalidAlgorithm 1
#define Hacl_Streaming_Types_InvalidLength 2
#define Hacl_Streaming_Types_MaximumLengthExceeded 3


uint32_t Hacl_Hash_Definitions_word_len(Spec_Hash_Definitions_hash_alg a);

uint32_t Hacl_Hash_Definitions_block_len(Spec_Hash_Definitions_hash_alg a);

uint32_t Hacl_Hash_Definitions_hash_word_len(Spec_Hash_Definitions_hash_alg a);

uint32_t Hacl_Hash_Definitions_hash_len(Spec_Hash_Definitions_hash_alg a);

typedef uint8_t *Hacl_Hash_Definitions_hash_t;




typedef uint8_t Hacl_Streaming_Types_error_code;

typedef struct Hacl_Streaming_MD_state_32_s
{
  uint32_t *block_state;
  uint8_t *buf;
  uint64_t total_len;
}
Hacl_Streaming_MD_state_32;

typedef struct Hacl_Streaming_MD_state_64_s
{
  uint64_t *block_state;
  uint8_t *buf;
  uint64_t total_len;
}
Hacl_Streaming_MD_state_64;



static const
uint32_t
Hacl_Impl_SHA2_Generic_h224[8U] =
  {
    (uint32_t)0xc1059ed8U, (uint32_t)0x367cd507U, (uint32_t)0x3070dd17U, (uint32_t)0xf70e5939U,
    (uint32_t)0xffc00b31U, (uint32_t)0x68581511U, (uint32_t)0x64f98fa7U, (uint32_t)0xbefa4fa4U
  };

static const
uint32_t
Hacl_Impl_SHA2_Generic_h256[8U] =
  {
    (uint32_t)0x6a09e667U, (uint32_t)0xbb67ae85U, (uint32_t)0x3c6ef372U, (uint32_t)0xa54ff53aU,
    (uint32_t)0x510e527fU, (uint32_t)0x9b05688cU, (uint32_t)0x1f83d9abU, (uint32_t)0x5be0cd19U
  };

static const
uint64_t
Hacl_Impl_SHA2_Generic_h384[8U] =
  {
    (uint64_t)0xcbbb9d5dc1059ed8U, (uint64_t)0x629a292a367cd507U, (uint64_t)0x9159015a3070dd17U,
    (uint64_t)0x152fecd8f70e5939U, (uint64_t)0x67332667ffc00b31U, (uint64_t)0x8eb44a8768581511U,
    (uint64_t)0xdb0c2e0d64f98fa7U, (uint64_t)0x47b5481dbefa4fa4U
  };

static const
uint64_t
Hacl_Impl_SHA2_Generic_h512[8U] =
  {
    (uint64_t)0x6a09e667f3bcc908U, (uint64_t)0xbb67ae8584caa73bU, (uint64_t)0x3c6ef372fe94f82bU,
    (uint64_t)0xa54ff53a5f1d36f1U, (uint64_t)0x510e527fade682d1U, (uint64_t)0x9b05688c2b3e6c1fU,
    (uint64_t)0x1f83d9abfb41bd6bU, (uint64_t)0x5be0cd19137e2179U
  };

static const
uint32_t
Hacl_Impl_SHA2_Generic_k224_256[64U] =
  {
    (uint32_t)0x428a2f98U, (uint32_t)0x71374491U, (uint32_t)0xb5c0fbcfU, (uint32_t)0xe9b5dba5U,
    (uint32_t)0x3956c25bU, (uint32_t)0x59f111f1U, (uint32_t)0x923f82a4U, (uint32_t)0xab1c5ed5U,
    (uint32_t)0xd807aa98U, (uint32_t)0x12835b01U, (uint32_t)0x243185beU, (uint32_t)0x550c7dc3U,
    (uint32_t)0x72be5d74U, (uint32_t)0x80deb1feU, (uint32_t)0x9bdc06a7U, (uint32_t)0xc19bf174U,
    (uint32_t)0xe49b69c1U, (uint32_t)0xefbe4786U, (uint32_t)0x0fc19dc6U, (uint32_t)0x240ca1ccU,
    (uint32_t)0x2de92c6fU, (uint32_t)0x4a7484aaU, (uint32_t)0x5cb0a9dcU, (uint32_t)0x76f988daU,
    (uint32_t)0x983e5152U, (uint32_t)0xa831c66dU, (uint32_t)0xb00327c8U, (uint32_t)0xbf597fc7U,
    (uint32_t)0xc6e00bf3U, (uint32_t)0xd5a79147U, (uint32_t)0x06ca6351U, (uint32_t)0x14292967U,
    (uint32_t)0x27b70a85U, (uint32_t)0x2e1b2138U, (uint32_t)0x4d2c6dfcU, (uint32_t)0x53380d13U,
    (uint32_t)0x650a7354U, (uint32_t)0x766a0abbU, (uint32_t)0x81c2c92eU, (uint32_t)0x92722c85U,
    (uint32_t)0xa2bfe8a1U, (uint32_t)0xa81a664bU, (uint32_t)0xc24b8b70U, (uint32_t)0xc76c51a3U,
    (uint32_t)0xd192e819U, (uint32_t)0xd6990624U, (uint32_t)0xf40e3585U, (uint32_t)0x106aa070U,
    (uint32_t)0x19a4c116U, (uint32_t)0x1e376c08U, (uint32_t)0x2748774cU, (uint32_t)0x34b0bcb5U,
    (uint32_t)0x391c0cb3U, (uint32_t)0x4ed8aa4aU, (uint32_t)0x5b9cca4fU, (uint32_t)0x682e6ff3U,
    (uint32_t)0x748f82eeU, (uint32_t)0x78a5636fU, (uint32_t)0x84c87814U, (uint32_t)0x8cc70208U,
    (uint32_t)0x90befffaU, (uint32_t)0xa4506cebU, (uint32_t)0xbef9a3f7U, (uint32_t)0xc67178f2U
  };

static const
uint64_t
Hacl_Impl_SHA2_Generic_k384_512[80U] =
  {
    (uint64_t)0x428a2f98d728ae22U, (uint64_t)0x7137449123ef65cdU, (uint64_t)0xb5c0fbcfec4d3b2fU,
    (uint64_t)0xe9b5dba58189dbbcU, (uint64_t)0x3956c25bf348b538U, (uint64_t)0x59f111f1b605d019U,
    (uint64_t)0x923f82a4af194f9bU, (uint64_t)0xab1c5ed5da6d8118U, (uint64_t)0xd807aa98a3030242U,
    (uint64_t)0x12835b0145706fbeU, (uint64_t)0x243185be4ee4b28cU, (uint64_t)0x550c7dc3d5ffb4e2U,
    (uint64_t)0x72be5d74f27b896fU, (uint64_t)0x80deb1fe3b1696b1U, (uint64_t)0x9bdc06a725c71235U,
    (uint64_t)0xc19bf174cf692694U, (uint64_t)0xe49b69c19ef14ad2U, (uint64_t)0xefbe4786384f25e3U,
    (uint64_t)0x0fc19dc68b8cd5b5U, (uint64_t)0x240ca1cc77ac9c65U, (uint64_t)0x2de92c6f592b0275U,
    (uint64_t)0x4a7484aa6ea6e483U, (uint64_t)0x5cb0a9dcbd41fbd4U, (uint64_t)0x76f988da831153b5U,
    (uint64_t)0x983e5152ee66dfabU, (uint64_t)0xa831c66d2db43210U, (uint64_t)0xb00327c898fb213fU,
    (uint64_t)0xbf597fc7beef0ee4U, (uint64_t)0xc6e00bf33da88fc2U, (uint64_t)0xd5a79147930aa725U,
    (uint64_t)0x06ca6351e003826fU, (uint64_t)0x142929670a0e6e70U, (uint64_t)0x27b70a8546d22ffcU,
    (uint64_t)0x2e1b21385c26c926U, (uint64_t)0x4d2c6dfc5ac42aedU, (uint64_t)0x53380d139d95b3dfU,
    (uint64_t)0x650a73548baf63deU, (uint64_t)0x766a0abb3c77b2a8U, (uint64_t)0x81c2c92e47edaee6U,
    (uint64_t)0x92722c851482353bU, (uint64_t)0xa2bfe8a14cf10364U, (uint64_t)0xa81a664bbc423001U,
    (uint64_t)0xc24b8b70d0f89791U, (uint64_t)0xc76c51a30654be30U, (uint64_t)0xd192e819d6ef5218U,
    (uint64_t)0xd69906245565a910U, (uint64_t)0xf40e35855771202aU, (uint64_t)0x106aa07032bbd1b8U,
    (uint64_t)0x19a4c116b8d2d0c8U, (uint64_t)0x1e376c085141ab53U, (uint64_t)0x2748774cdf8eeb99U,
    (uint64_t)0x34b0bcb5e19b48a8U, (uint64_t)0x391c0cb3c5c95a63U, (uint64_t)0x4ed8aa4ae3418acbU,
    (uint64_t)0x5b9cca4f7763e373U, (uint64_t)0x682e6ff3d6b2b8a3U, (uint64_t)0x748f82ee5defb2fcU,
    (uint64_t)0x78a5636f43172f60U, (uint64_t)0x84c87814a1f0ab72U, (uint64_t)0x8cc702081a6439ecU,
    (uint64_t)0x90befffa23631e28U, (uint64_t)0xa4506cebde82bde9U, (uint64_t)0xbef9a3f7b2c67915U,
    (uint64_t)0xc67178f2e372532bU, (uint64_t)0xca273eceea26619cU, (uint64_t)0xd186b8c721c0c207U,
    (uint64_t)0xeada7dd6cde0eb1eU, (uint64_t)0xf57d4f7fee6ed178U, (uint64_t)0x06f067aa72176fbaU,
    (uint64_t)0x0a637dc5a2c898a6U, (uint64_t)0x113f9804bef90daeU, (uint64_t)0x1b710b35131c471bU,
    (uint64_t)0x28db77f523047d84U, (uint64_t)0x32caab7b40c72493U, (uint64_t)0x3c9ebe0a15c9bebcU,
    (uint64_t)0x431d67c49c100d4cU, (uint64_t)0x4cc5d4becb3e42b6U, (uint64_t)0x597f299cfc657e2aU,
    (uint64_t)0x5fcb6fab3ad6faecU, (uint64_t)0x6c44198c4a475817U
  };

void Hacl_SHA2_Scalar32_sha256_init(uint32_t *hash);

void Hacl_SHA2_Scalar32_sha256_update_nblocks(uint32_t len, uint8_t *b, uint32_t *st);

void
Hacl_SHA2_Scalar32_sha256_update_last(
  uint64_t totlen,
  uint32_t len,
  uint8_t *b,
  uint32_t *hash
);

void Hacl_SHA2_Scalar32_sha256_finish(uint32_t *st, uint8_t *h);

void Hacl_SHA2_Scalar32_sha224_init(uint32_t *hash);

void
Hacl_SHA2_Scalar32_sha224_update_last(uint64_t totlen, uint32_t len, uint8_t *b, uint32_t *st);

void Hacl_SHA2_Scalar32_sha224_finish(uint32_t *st, uint8_t *h);

void Hacl_SHA2_Scalar32_sha512_init(uint64_t *hash);

void Hacl_SHA2_Scalar32_sha512_update_nblocks(uint32_t len, uint8_t *b, uint64_t *st);

void
Hacl_SHA2_Scalar32_sha512_update_last(
  FStar_UInt128_uint128 totlen,
  uint32_t len,
  uint8_t *b,
  uint64_t *hash
);

void Hacl_SHA2_Scalar32_sha512_finish(uint64_t *st, uint8_t *h);

void Hacl_SHA2_Scalar32_sha384_init(uint64_t *hash);

void Hacl_SHA2_Scalar32_sha384_update_nblocks(uint32_t len, uint8_t *b, uint64_t *st);

void
Hacl_SHA2_Scalar32_sha384_update_last(
  FStar_UInt128_uint128 totlen,
  uint32_t len,
  uint8_t *b,
  uint64_t *st
);

void Hacl_SHA2_Scalar32_sha384_finish(uint64_t *st, uint8_t *h);



typedef Hacl_Streaming_MD_state_32 Hacl_Streaming_SHA2_state_sha2_224;

typedef Hacl_Streaming_MD_state_32 Hacl_Streaming_SHA2_state_sha2_256;

typedef Hacl_Streaming_MD_state_64 Hacl_Streaming_SHA2_state_sha2_384;

typedef Hacl_Streaming_MD_state_64 Hacl_Streaming_SHA2_state_sha2_512;

/**
Allocate initial state for the SHA2_256 hash. The state is to be freed by
calling `free_256`.
*/
Hacl_Streaming_MD_state_32 *Hacl_Streaming_SHA2_create_in_256(void);

/**
Copies the state passed as argument into a newly allocated state (deep copy).
The state is to be freed by calling `free_256`. Cloning the state this way is
useful, for instance, if your control-flow diverges and you need to feed
more (different) data into the hash in each branch.
*/
Hacl_Streaming_MD_state_32 *Hacl_Streaming_SHA2_copy_256(Hacl_Streaming_MD_state_32 *s0);

/**
Reset an existing state to the initial hash state with empty data.
*/
void Hacl_Streaming_SHA2_init_256(Hacl_Streaming_MD_state_32 *s);

/**
Feed an arbitrary amount of data into the hash. This function returns 0 for
success, or 1 if the combined length of all of the data passed to `update_256`
(since the last call to `init_256`) exceeds 2^61-1 bytes.

This function is identical to the update function for SHA2_224.
*/
Hacl_Streaming_Types_error_code
Hacl_Streaming_SHA2_update_256(
  Hacl_Streaming_MD_state_32 *p,
  uint8_t *input,
  uint32_t input_len
);

/**
Write the resulting hash into `dst`, an array of 32 bytes. The state remains
valid after a call to `finish_256`, meaning the user may feed more data into
the hash via `update_256`. (The finish_256 function operates on an internal copy of
the state and therefore does not invalidate the client-held state `p`.)
*/
void Hacl_Streaming_SHA2_finish_256(Hacl_Streaming_MD_state_32 *p, uint8_t *dst);

/**
Free a state allocated with `create_in_256`.

This function is identical to the free function for SHA2_224.
*/
void Hacl_Streaming_SHA2_free_256(Hacl_Streaming_MD_state_32 *s);

/**
Hash `input`, of len `input_len`, into `dst`, an array of 32 bytes.
*/
void Hacl_Streaming_SHA2_hash_256(uint8_t *input, uint32_t input_len, uint8_t *dst);

Hacl_Streaming_MD_state_32 *Hacl_Streaming_SHA2_create_in_224(void);

void Hacl_Streaming_SHA2_init_224(Hacl_Streaming_MD_state_32 *s);

Hacl_Streaming_Types_error_code
Hacl_Streaming_SHA2_update_224(
  Hacl_Streaming_MD_state_32 *p,
  uint8_t *input,
  uint32_t input_len
);

/**
Write the resulting hash into `dst`, an array of 28 bytes. The state remains
valid after a call to `finish_224`, meaning the user may feed more data into
the hash via `update_224`.
*/
void Hacl_Streaming_SHA2_finish_224(Hacl_Streaming_MD_state_32 *p, uint8_t *dst);

void Hacl_Streaming_SHA2_free_224(Hacl_Streaming_MD_state_32 *p);

/**
Hash `input`, of len `input_len`, into `dst`, an array of 28 bytes.
*/
void Hacl_Streaming_SHA2_hash_224(uint8_t *input, uint32_t input_len, uint8_t *dst);

Hacl_Streaming_MD_state_64 *Hacl_Streaming_SHA2_create_in_512(void);

/**
Copies the state passed as argument into a newly allocated state (deep copy).
The state is to be freed by calling `free_512`. Cloning the state this way is
useful, for instance, if your control-flow diverges and you need to feed
more (different) data into the hash in each branch.
*/
Hacl_Streaming_MD_state_64 *Hacl_Streaming_SHA2_copy_512(Hacl_Streaming_MD_state_64 *s0);

void Hacl_Streaming_SHA2_init_512(Hacl_Streaming_MD_state_64 *s);

/**
Feed an arbitrary amount of data into the hash. This function returns 0 for
success, or 1 if the combined length of all of the data passed to `update_512`
(since the last call to `init_512`) exceeds 2^125-1 bytes.

This function is identical to the update function for SHA2_384.
*/
Hacl_Streaming_Types_error_code
Hacl_Streaming_SHA2_update_512(
  Hacl_Streaming_MD_state_64 *p,
  uint8_t *input,
  uint32_t input_len
);

/**
Write the resulting hash into `dst`, an array of 64 bytes. The state remains
valid after a call to `finish_512`, meaning the user may feed more data into
the hash via `update_512`. (The finish_512 function operates on an internal copy of
the state and therefore does not invalidate the client-held state `p`.)
*/
void Hacl_Streaming_SHA2_finish_512(Hacl_Streaming_MD_state_64 *p, uint8_t *dst);

/**
Free a state allocated with `create_in_512`.

This function is identical to the free function for SHA2_384.
*/
void Hacl_Streaming_SHA2_free_512(Hacl_Streaming_MD_state_64 *s);

/**
Hash `input`, of len `input_len`, into `dst`, an array of 64 bytes.
*/
void Hacl_Streaming_SHA2_hash_512(uint8_t *input, uint32_t input_len, uint8_t *dst);

Hacl_Streaming_MD_state_64 *Hacl_Streaming_SHA2_create_in_384(void);

void Hacl_Streaming_SHA2_init_384(Hacl_Streaming_MD_state_64 *s);

Hacl_Streaming_Types_error_code
Hacl_Streaming_SHA2_update_384(
  Hacl_Streaming_MD_state_64 *p,
  uint8_t *input,
  uint32_t input_len
);

/**
Write the resulting hash into `dst`, an array of 48 bytes. The state remains
valid after a call to `finish_384`, meaning the user may feed more data into
the hash via `update_384`.
*/
void Hacl_Streaming_SHA2_finish_384(Hacl_Streaming_MD_state_64 *p, uint8_t *dst);

void Hacl_Streaming_SHA2_free_384(Hacl_Streaming_MD_state_64 *p);

/**
Hash `input`, of len `input_len`, into `dst`, an array of 48 bytes.
*/
void Hacl_Streaming_SHA2_hash_384(uint8_t *input, uint32_t input_len, uint8_t *dst);





typedef struct Hacl_Impl_SHA2_Types_uint8_2p_s
{
  uint8_t *fst;
  uint8_t *snd;
}
Hacl_Impl_SHA2_Types_uint8_2p;

typedef struct Hacl_Impl_SHA2_Types_uint8_3p_s
{
  uint8_t *fst;
  Hacl_Impl_SHA2_Types_uint8_2p snd;
}
Hacl_Impl_SHA2_Types_uint8_3p;

typedef struct Hacl_Impl_SHA2_Types_uint8_4p_s
{
  uint8_t *fst;
  Hacl_Impl_SHA2_Types_uint8_3p snd;
}
Hacl_Impl_SHA2_Types_uint8_4p;

typedef struct Hacl_Impl_SHA2_Types_uint8_5p_s
{
  uint8_t *fst;
  Hacl_Impl_SHA2_Types_uint8_4p snd;
}
Hacl_Impl_SHA2_Types_uint8_5p;

typedef struct Hacl_Impl_SHA2_Types_uint8_6p_s
{
  uint8_t *fst;
  Hacl_Impl_SHA2_Types_uint8_5p snd;
}
Hacl_Impl_SHA2_Types_uint8_6p;

typedef struct Hacl_Impl_SHA2_Types_uint8_7p_s
{
  uint8_t *fst;
  Hacl_Impl_SHA2_Types_uint8_6p snd;
}
Hacl_Impl_SHA2_Types_uint8_7p;

typedef struct Hacl_Impl_SHA2_Types_uint8_8p_s
{
  uint8_t *fst;
  Hacl_Impl_SHA2_Types_uint8_7p snd;
}
Hacl_Impl_SHA2_Types_uint8_8p;

typedef struct Hacl_Impl_SHA2_Types_uint8_2x4p_s
{
  Hacl_Impl_SHA2_Types_uint8_4p fst;
  Hacl_Impl_SHA2_Types_uint8_4p snd;
}
Hacl_Impl_SHA2_Types_uint8_2x4p;

typedef struct Hacl_Impl_SHA2_Types_uint8_2x8p_s
{
  Hacl_Impl_SHA2_Types_uint8_8p fst;
  Hacl_Impl_SHA2_Types_uint8_8p snd;
}
Hacl_Impl_SHA2_Types_uint8_2x8p;


#define __Hacl_Hash_H_DEFINED
#endif
