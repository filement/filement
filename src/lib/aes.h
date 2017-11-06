struct aes_context
{
	uint32_t eK[60], dK[60];
	int Nr;
};

// WARNING: Number of rounds is currently fixed to 10.
void (aes_setup)(const unsigned char *key, int keylen, int num_rounds, struct aes_context *restrict skey);
#define aes_setup(key, length, ctx) aes_setup((key), (length), 10, (ctx))

int aes_encrypt(const unsigned char *pt, unsigned char *restrict ct, struct aes_context *restrict skey);
int aes_decrypt(const unsigned char *ct, unsigned char *restrict pt, struct aes_context *restrict skey);
