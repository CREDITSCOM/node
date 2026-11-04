#include "keyfilecrypt.hpp"

#include <cstring>

namespace cs {
namespace keyfile {

namespace {
using FileKey = cscrypto::MemGuard<cscrypto::Byte, crypto_aead_chacha20poly1305_KEYBYTES>;

// the same derivation as cscrypto's, with the file's own salt
bool deriveKey(FileKey& key, const char* passwd, std::size_t pswdLen, const cscrypto::Byte* salt) {
    return key && crypto_pwhash(key.data(), key.size(), passwd, pswdLen, salt, crypto_pwhash_OPSLIMIT_SENSITIVE,
                                crypto_pwhash_MEMLIMIT_SENSITIVE, crypto_pwhash_ALG_DEFAULT) == 0;
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
        return cscrypto::PrivateKey::readFromEncrypted(bytes, passwd, pswdLen);
    }
    if (bytes.size() != kSaltedEncryptedSize) {
        return cscrypto::PrivateKey();
    }

    const cscrypto::Byte* salt = bytes.data();
    const cscrypto::Byte* nonce = salt + kSaltSize;
    const cscrypto::Byte* sealed = nonce + kNonceSize;

    FileKey fileKey;
    if (!deriveKey(fileKey, passwd, pswdLen, salt)) {
        return cscrypto::PrivateKey();
    }

    cscrypto::MemGuard<cscrypto::Byte, cscrypto::kPrivateKeySize> sk;
    if (!sk) {
        return cscrypto::PrivateKey();
    }
    unsigned long long skLen = 0;
    if (crypto_aead_chacha20poly1305_decrypt(sk.data(), &skLen, nullptr, sealed, kSealedSize, nullptr, 0, nonce, fileKey.data()) != 0 ||
        skLen != cscrypto::kPrivateKeySize) {
        return cscrypto::PrivateKey();
    }
    return cscrypto::PrivateKey::readFromBytes(sk);
}

}  // namespace keyfile
}  // namespace cs
