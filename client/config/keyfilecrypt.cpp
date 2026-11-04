#include "keyfilecrypt.hpp"

#include <array>
#include <cstring>

namespace cs {
namespace keyfile {

namespace {
using FileKey = cscrypto::MemGuard<cscrypto::Byte, crypto_aead_chacha20poly1305_KEYBYTES>;

// the salt of the legacy format, as cscrypto's PrivateKey::getEncrypted uses it
constexpr std::array<cscrypto::Byte, kSaltSize> kLegacySalt = {0x49, 0x44, 0x49, 0x20, 0x4e, 0x41, 0x48, 0x55,
                                                                0x59, 0x20, 0x42, 0x4c, 0x59, 0x41, 0x54, 0x27};

// Argon2id v1.3 with the SENSITIVE limits, pinned (the values of the generic crypto_pwhash
// defaults that both formats were written with); false when the key memory or the derivation's
// memory cannot be had
bool deriveKey(FileKey& key, const char* passwd, std::size_t pswdLen, const cscrypto::Byte* salt) {
    return key && crypto_pwhash(key.data(), key.size(), passwd, pswdLen, salt, crypto_pwhash_argon2id_OPSLIMIT_SENSITIVE,
                                crypto_pwhash_argon2id_MEMLIMIT_SENSITIVE, crypto_pwhash_ALG_ARGON2ID13) == 0;
}

// nonce || ciphertext and tag, opened with the key derived from salt
cscrypto::PrivateKey open(const cscrypto::Byte* salt, const cscrypto::Byte* nonce, const char* passwd, std::size_t pswdLen) {
    const cscrypto::Byte* sealed = nonce + kNonceSize;

    FileKey fileKey;
    if (!deriveKey(fileKey, passwd, pswdLen, salt)) {
        return cscrypto::PrivateKey();
    }

    cscrypto::MemGuard<cscrypto::Byte, cscrypto::kPrivateKeySize> sk;
    if (!sk) {
        return cscrypto::PrivateKey();
    }
    // the output is exactly kSealedSize - ABYTES = 64 bytes: libsodium zeroes that many on a bad tag
    unsigned long long skLen = 0;
    if (crypto_aead_chacha20poly1305_decrypt(sk.data(), &skLen, nullptr, sealed, kSealedSize, nullptr, 0, nonce, fileKey.data()) != 0 ||
        skLen != cscrypto::kPrivateKeySize) {
        return cscrypto::PrivateKey();
    }
    return cscrypto::PrivateKey::readFromBytes(sk);
}
}  // namespace

cscrypto::Bytes encrypt(const cscrypto::PrivateKey& key, const char* passwd, std::size_t pswdLen) {
    cscrypto::Bytes result;
    if (!key) {
        return result;
    }

    cscrypto::Bytes out(kSaltedEncryptedSize);
    cscrypto::Byte* salt = out.data();
    cscrypto::Byte* nonce = salt + kSaltSize;
    cscrypto::Byte* sealed = nonce + kNonceSize;
    randombytes_buf(salt, kSaltSize);
    randombytes_buf(nonce, kNonceSize);

    FileKey fileKey;
    if (!deriveKey(fileKey, passwd, pswdLen, salt)) {
        return result;
    }

    auto sk = key.access();
    unsigned long long sealedLen = 0;
    if (crypto_aead_chacha20poly1305_encrypt(sealed, &sealedLen, sk.data(), sk.size(), nullptr, 0, nullptr, nonce, fileKey.data()) != 0 ||
        sealedLen != kSealedSize) {
        sodium_memzero(out.data(), out.size());
        return result;
    }

    result.swap(out);
    return result;
}

cscrypto::PrivateKey decrypt(const cscrypto::Bytes& bytes, const char* passwd, std::size_t pswdLen) {
    if (bytes.size() == kLegacyEncryptedSize) {
        return open(kLegacySalt.data(), bytes.data(), passwd, pswdLen);
    }
    if (bytes.size() == kSaltedEncryptedSize) {
        return open(bytes.data(), bytes.data() + kSaltSize, passwd, pswdLen);
    }
    return cscrypto::PrivateKey();
}

}  // namespace keyfile
}  // namespace cs
