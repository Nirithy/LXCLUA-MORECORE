#ifndef _AES_H_
#define _AES_H_

#include <stdint.h>

/**
 * @file aes.h
 * @brief AES algorithm implementation (ECB, CTR, CBC modes).
 */

// #define the macros below to 1/0 to enable/disable the mode of operation.
//
// CBC enables AES encryption in CBC-mode of operation.
// CTR enables encryption in counter-mode.
// ECB enables the basic ECB 16-byte block algorithm. All can be enabled simultaneously.

// The #ifndef-guard allows it to be configured before #include'ing or at compile time.
#ifndef CBC
  #define CBC 1
#endif

#ifndef ECB
  #define ECB 1
#endif

#ifndef CTR
  #define CTR 1
#endif


#define AES128 1
//#define AES192 1
//#define AES256 1

/** @brief Block length in bytes. AES is 128-bit block only. */
#define AES_BLOCKLEN 16

#if defined(AES256) && (AES256 == 1)
    #define AES_KEYLEN 32
    #define AES_keyExpSize 240
#elif defined(AES192) && (AES192 == 1)
    #define AES_KEYLEN 24
    #define AES_keyExpSize 208
#else
    #define AES_KEYLEN 16   /** @brief Key length in bytes. */
    #define AES_keyExpSize 176
#endif

/**
 * @brief AES context structure.
 */
struct AES_ctx
{
  uint8_t RoundKey[AES_keyExpSize]; /** @brief Expanded round keys. */
#if (defined(CBC) && (CBC == 1)) || (defined(CTR) && (CTR == 1))
  uint8_t Iv[AES_BLOCKLEN];         /** @brief Initialization Vector. */
#endif
};

/**
 * @brief Initializes an AES context with a given key.
 * @param ctx Pointer to the AES context.
 * @param key Pointer to the key.
 */
void AES_init_ctx(struct AES_ctx* ctx, const uint8_t* key);

#if (defined(CBC) && (CBC == 1)) || (defined(CTR) && (CTR == 1))
/**
 * @brief Initializes an AES context with a given key and IV.
 * @param ctx Pointer to the AES context.
 * @param key Pointer to the key.
 * @param iv Pointer to the Initialization Vector.
 */
void AES_init_ctx_iv(struct AES_ctx* ctx, const uint8_t* key, const uint8_t* iv);

/**
 * @brief Sets the IV in an existing AES context.
 * @param ctx Pointer to the AES context.
 * @param iv Pointer to the new Initialization Vector.
 */
void AES_ctx_set_iv(struct AES_ctx* ctx, const uint8_t* iv);
#endif

#if defined(ECB) && (ECB == 1)
/**
 * @brief Encrypts a single block using ECB mode.
 * @param ctx Pointer to the AES context.
 * @param buf Pointer to the data block (AES_BLOCKLEN bytes).
 * @note ECB is considered insecure for most uses.
 */
void AES_ECB_encrypt(struct AES_ctx* ctx, uint8_t* buf);

/**
 * @brief Decrypts a single block using ECB mode.
 * @param ctx Pointer to the AES context.
 * @param buf Pointer to the data block (AES_BLOCKLEN bytes).
 */
void AES_ECB_decrypt(struct AES_ctx* ctx, uint8_t* buf);

#endif // #if defined(ECB) && (ECB == 1)


#if defined(CBC) && (CBC == 1)
/**
 * @brief Encrypts a buffer using CBC mode.
 * @param ctx Pointer to the AES context.
 * @param buf Pointer to the data buffer.
 * @param length Length of the buffer in bytes (must be a multiple of AES_BLOCKLEN).
 */
void AES_CBC_encrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, uint32_t length);

/**
 * @brief Decrypts a buffer using CBC mode.
 * @param ctx Pointer to the AES context.
 * @param buf Pointer to the data buffer.
 * @param length Length of the buffer in bytes (must be a multiple of AES_BLOCKLEN).
 */
void AES_CBC_decrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, uint32_t length);

#endif // #if defined(CBC) && (CBC == 1)


#if defined(CTR) && (CTR == 1)
/**
 * @brief Encrypts/decrypts a buffer using CTR mode.
 * @param ctx Pointer to the AES context.
 * @param buf Pointer to the data buffer.
 * @param length Length of the buffer in bytes.
 * @note CTR mode is symmetrical; the same function is used for encryption and decryption.
 */
void AES_CTR_xcrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, uint32_t length);

#endif // #if defined(CTR) && (CTR == 1)


#endif //_AES_H_
