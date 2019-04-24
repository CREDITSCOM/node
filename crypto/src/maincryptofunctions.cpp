#include "cscrypto/maincryptofunctions.hpp"

#include <cassert>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324 4244 4267 4804)  // 'crypto_generichash_blake2b_state': structure was padded due to alignment specifier,
                                                // convertion from uin64_t to uin32_t, convertion from size_t to uint8_t
#endif
#include <blake2.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace cscrypto {

bool cryptoInit() {
    return sodium_init() >= 0;
}

Hash calculateHash(const Byte* data, size_t dataSize, const Byte* key, size_t keySize) {
    Hash hash;
    blake2s(hash.data(), BLAKE2S_OUTBYTES, data, dataSize, key, keySize);
    return hash;
}

PrivateKey generateKeyPair(PublicKey& publicKey) {
    PrivateKey privateKey = PrivateKey::generateWithPair(publicKey);
    return privateKey;
}

bool validateKeyPair(const PublicKey& publicKey, const PrivateKey& privateKey) {
    return publicKey == getMatchingPublic(privateKey);
}

PublicKey getMatchingPublic(const PrivateKey& privateKey) {
    PublicKey result;
    auto pkg = privateKey.access();
    crypto_sign_ed25519_sk_to_pk(result.data(), pkg.data());
    return result;
}

Signature generateSignature(const PrivateKey& privateKey, const Byte* data, size_t dataSize) {
    assert(data != nullptr);
    unsigned long long signatureLen;
    auto pkg = privateKey.access();

    Signature signature;
    crypto_sign_ed25519_detached(signature.data(), &signatureLen, data, dataSize, pkg.data());
    return signature;
}

bool verifySignature(const Signature& signature, const PublicKey& publicKey, const Byte* data, size_t dataSize) {
    assert(data != nullptr);
    return !crypto_sign_ed25519_verify_detached(signature.data(), data, dataSize, publicKey.data());
}

bool verifySignature(const Byte* signature, const Byte* publicKey, const Byte* data, size_t dataSize) {
    assert(signature != nullptr && publicKey != nullptr && data != nullptr);
    return !crypto_sign_ed25519_verify_detached(signature, data, dataSize, publicKey);
}

void fillBufWithRandomBytes(void* buffer, size_t size) {
    assert(buffer != nullptr);
    randombytes_buf(buffer, size);
}

void fillWithZeros(void* buffer, size_t size) {
    assert(buffer != nullptr);
    sodium_memzero(buffer, size);
}

}  // namespace cscrypto
