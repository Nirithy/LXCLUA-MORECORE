/**
 * @file crc.h
 * @brief CRC32 calculation functions.
 */

#if !defined(__CRC_H__)
#define __CRC_H__

#if defined(__cplusplus)
extern "C"
{
#endif

/**
 * @brief Macro to update a CRC32 value with a single octet.
 * @param octet The byte to add.
 * @param crc The current CRC value.
 * @return The updated CRC value.
 */
#define UPDC32(octet, crc)\
  (unsigned int)((crc_32_tab[(((unsigned int)(crc)) ^ ((unsigned char)(octet))) & 0xff] ^ (((unsigned int)(crc)) >> 8)))

/**
 * @brief Calculates the CRC32 checksum of a buffer.
 * @param data Pointer to the data buffer.
 * @param length Length of the data buffer in bytes.
 * @return The calculated CRC32 checksum.
 */
unsigned int naga_crc32(unsigned char* data, unsigned int length);

/**
 * @brief Calculates the CRC32 checksum of four integers.
 * @param data Pointer to an array of at least 4 unsigned integers.
 * @return The calculated CRC32 checksum.
 */
unsigned int naga_crc32int(unsigned int *data);

/**
 * @brief Runs self-tests for the CRC32 implementation.
 * @return 1 if tests pass, 0 otherwise.
 */
unsigned char naga_crc32_selftests ();
	
/** @brief The CRC32 lookup table. */
extern unsigned int crc_32_tab[];
	
#if defined(__cplusplus)
}
#endif

#endif
