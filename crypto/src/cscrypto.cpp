#include "cscrypto/cscrypto.h"

#ifdef USE_CRYPTOPP
#include <blake2.h>
#else
#include <blake2.h>
#endif

#include <ed25519.h>
#include <cstring>

#include <mutex>

namespace cscrypto
{
  namespace detail
  {
    struct Context
    {
      byte seed[32] = {};

      std::mutex mutex;

      Context()
      {
      }

      // Disallow copy and assign
      Context(const Context&) = delete;
      Context& operator=(const Context&) = delete;
      Context(Context&&) = delete;
      Context& operator=(Context&&) = delete;
    };

    static Context& getContext()
    {
      static Context context;
      return context;
    }

    using ScopedLock = std::lock_guard<std::mutex>;
  }

  using namespace detail;

  Hash blake2s(const byte* data, size_t length)
  {

#ifdef USE_CRYPTOPP

    using namespace CryptoPP;

    static_assert(Hash256Bit::sizeBytes == BLAKE2s::DIGESTSIZE, "Hash256Bit size doesn't match Blake2s digest size");

    Hash256Bit result;

    BLAKE2s blake2s;
    blake2s.Update(data, length);
    blake2s.Final(result.data());

    return result;

#else

    static_assert( Hash::sizeBytes == BLAKE2S_OUTBYTES, "Hash256Bit size doesn't match Blake2s digest size");

    Hash result;

    blake2s(result.data(), BLAKE2S_OUTBYTES, data, length, NULL, 0);

#endif // #ifdef USE_CRYPTOPP

    return result;
  }

  KeyPair generateKeyPair()
  {
    // TODO: Provide more complex Private Key generation algorithm

    PublicKey publicKey;
    PrivateKey privateKey;

    Context& context = getContext();

    {
      ScopedLock lock(context.mutex);

      ed25519_create_seed(context.seed);
      ed25519_create_keypair(publicKey.data(), privateKey.data(), context.seed);
    }

    return { publicKey, privateKey };
  }

  /// Convert the given value into h160 (160-bit unsigned integer) using the right 20 bytes.
  using Hash160Bit = FixedArray<160>;
  inline Hash160Bit right160(const Hash& h256)
  {
    Hash160Bit ret;
    memcpy(ret.data(), h256.data() + 12, 20);
    return ret;
  }

  Address toAddress(const PublicKey& publicKey)
  {
    // Ethereum does right160(sha3(public))
    return Address{ right160(blake2s(publicKey)) };
  }

  Signature sign(const Hash& hash, const KeyPair& keyPair)
  {
    // Ethereum does complex thing

    Signature signature;

    Context& context = getContext();

    {
      ScopedLock lock(context.mutex);
      ed25519_sign(signature.data(), hash.data(), hash.size(), keyPair.publicKey.data(), keyPair.privateKey.data());
    }

    return signature;
  }

  bool verify(const PublicKey& publicKey, const Hash& hash, const Signature& signature)
  {
    Context& context = getContext();
    ScopedLock lock(context.mutex);

    return ed25519_verify(signature.data(), hash.data(), hash.size(), publicKey.data()) == 1;
  }
}
