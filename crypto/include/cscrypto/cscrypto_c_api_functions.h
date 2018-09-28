#ifndef CSCRYPTO_C_API_FUNCTIONS_H
#define CSCRYPTO_C_API_FUNCTIONS_H

#if defined(_MSC_VER)

//  Microsoft 
#define EXPORT __declspec(dllexport)
#define IMPORT __declspec(dllimport)

#elif defined(__GNUC__)

//  GCC
#define EXPORT __attribute__((visibility("default")))
#define IMPORT

#else

//  Do nothing and hope for the best?
#define EXPORT
#define IMPORT
#pragma warning Unknown dynamic link import/export semantics.

#endif

	EXPORT int cscrypto_generate_key_pair( unsigned char* public_key, unsigned char* private_key );
	
	EXPORT int cscrypto_public_key_to_address( const unsigned char* public_key, unsigned char* address );
	
	EXPORT int cscrypto_hash( unsigned char* data, unsigned int data_length, unsigned char* hash );
	
	EXPORT int cscrypto_sign( const unsigned char* hash, const unsigned char* public_key, const unsigned char* private_key, unsigned char* signature );
	
	EXPORT int cscrypto_verify( const unsigned char* hash, const unsigned char* public_key, const unsigned char* signature );

#endif // CSCRYPTO_C_API_H