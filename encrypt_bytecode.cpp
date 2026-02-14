#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "aes.h"
#include "sha256.h"

static const char* nirithy_b64 = "9876543210zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA-_";

static char* nirithy_encode(const unsigned char* input, size_t input_len) {
    size_t out_len = 4 * ((input_len + 2) / 3);
    char* out = (char*)malloc(out_len + 1);
    if (!out) return NULL;

    size_t i, j;
    for (i = 0, j = 0; i < input_len;) {
        uint32_t octet_a = i < input_len ? input[i++] : 0;
        uint32_t octet_b = i < input_len ? input[i++] : 0;
        uint32_t octet_c = i < input_len ? input[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        out[j++] = nirithy_b64[(triple >> 18) & 0x3F];
        out[j++] = nirithy_b64[(triple >> 12) & 0x3F];
        out[j++] = nirithy_b64[(triple >> 6) & 0x3F];
        out[j++] = nirithy_b64[triple & 0x3F];
    }

    // Padding
    if (input_len % 3 == 1) {
        out[out_len - 1] = '=';
        out[out_len - 2] = '=';
    } else if (input_len % 3 == 2) {
        out[out_len - 1] = '=';
    }

    out[out_len] = '\0';
    return out;
}

static void nirithy_derive_key(uint64_t timestamp, uint8_t *key) {
  uint8_t input[32];
  uint8_t digest[SHA256_DIGEST_SIZE];

  memcpy(input, &timestamp, 8);
  memcpy(input + 8, "NirithySalt", 11);

  SHA256(input, 19, digest);
  memcpy(key, digest, 16);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_file> <output_file>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror("Error opening input file");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char *content = (unsigned char*)malloc(fsize);
    if (fread(content, 1, fsize, f) != (size_t)fsize) {
        fprintf(stderr, "Error reading file\n");
        fclose(f);
        return 1;
    }
    fclose(f);

    // Prepare encryption
    uint64_t timestamp = (uint64_t)time(NULL);
    uint8_t iv[16];
    // Simple pseudo-random IV for testing
    for(int i=0; i<16; i++) iv[i] = (uint8_t)rand();

    uint8_t key[16];
    nirithy_derive_key(timestamp, key);

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CTR_xcrypt_buffer(&ctx, content, (uint32_t)fsize);

    // Prepare binary payload: [Timestamp(8)][IV(16)][EncryptedContent]
    size_t payload_len = 8 + 16 + fsize;
    unsigned char *payload = (unsigned char*)malloc(payload_len);
    memcpy(payload, &timestamp, 8);
    memcpy(payload + 8, iv, 16);
    memcpy(payload + 24, content, fsize);

    // Base64 encode
    char *b64 = nirithy_encode(payload, payload_len);
    if (!b64) {
        fprintf(stderr, "Base64 encode failed\n");
        return 1;
    }

    f = fopen(argv[2], "wb");
    if (!f) {
        perror("Error opening output file");
        return 1;
    }
    fwrite("Nirithy==", 1, 9, f);
    fwrite(b64, 1, strlen(b64), f);
    fclose(f);

    free(content);
    free(payload);
    free(b64);

    printf("Encrypted file written to %s\n", argv[2]);
    return 0;
}
