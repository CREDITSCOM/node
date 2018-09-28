#include "cscrypto/cscrypto_c_api.h"
#include "cscrypto/cscrypto.h"
#include <cstring>

using namespace cscrypto;

extern "C"
{
  static_assert( CSCRYPTO_PUBLIC_KEY_LENGTH == PublicKey::sizeBytes, "Invalid public key size in C API" );
  static_assert( CSCRYPTO_PRIVATE_KEY_LENGTH == PrivateKey::sizeBytes, "Invalid private key size in C API" );
  static_assert( CSCRYPTO_SIGNATURE_LENGTH == Signature::sizeBytes, "Invalid signature size in C API" );
  static_assert( CSCRYPTO_HASH_LENGTH == Hash::sizeBytes, "Invalid hash size in C API" );

  int cscrypto_generate_key_pair(unsigned char* public_key, unsigned char* private_key)
  {
    KeyPair kp = generateKeyPair();

    memcpy(public_key, kp.publicKey.data(), kp.publicKey.size());
    memcpy(private_key, kp.privateKey.data(), kp.privateKey.size());

    return 0;
  }

  int cscrypto_public_key_to_address(const unsigned char* public_key, unsigned char* address)
  {
    Address a = toAddress( PublicKey{ public_key } );

    memcpy(address, a.data(), a.size());

    return 0;
  }

  int cscrypto_sign(const unsigned char* hash, const unsigned char* public_key, const unsigned char* private_key, unsigned char* signature)
  {
    Hash h{hash};
    KeyPair kp{ PublicKey{public_key}, PrivateKey{private_key} };

    Signature s = sign(h, kp);

    memcpy( signature, s.data(), s.size() );

    return 0;
  }

  int cscrypto_verify(const unsigned char* hash, const unsigned char* public_key,	const unsigned char* signature)
  {
    Hash h{ hash };
    PublicKey pk{ public_key };
    Signature s{ signature };

    bool ok = verify( pk, h, s );

    return ok ? 0 : 1;
  }

  int cscrypto_hash(unsigned char* data, unsigned int data_length, unsigned char* hash)
  {
    Hash h = blake2s(data, data_length);

    memcpy( hash, h.data(), h.size() );

    return 0;
  }
}
