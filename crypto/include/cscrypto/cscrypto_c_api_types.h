#ifndef CSCRYPTO_C_API_TYPES_H
#define CSCRYPTO_C_API_TYPES_H

extern "C"
{
	enum cscrypto_constants
	{
		CSCRYPTO_PUBLIC_KEY_LENGTH = 32,
		CSCRYPTO_PRIVATE_KEY_LENGTH = 64,
		CSCRYPTO_HASH_LENGTH = 32,
		CSCRYPTO_SIGNATURE_LENGTH = 64
	};

	typedef int (*cscrypto_generate_key_pair_ptr)( unsigned char* public_key, unsigned char* private_key );
	
	typedef int (*cscrypto_public_key_to_address_ptr)( const unsigned char* public_key, unsigned char* address );
	
	typedef int (*cscrypto_hash_ptr)( unsigned char* data, unsigned int data_length, unsigned char* hash );
	
	typedef int (*cscrypto_sign_ptr)( const unsigned char* hash, const unsigned char* public_key, const unsigned char* private_key, unsigned char* signature );
	
	typedef int (*cscrypto_verify_ptr)( const unsigned char* hash, const unsigned char* public_key, const unsigned char* signature );
}

#endif // CSCRYPTO_C_API_TYPES_H