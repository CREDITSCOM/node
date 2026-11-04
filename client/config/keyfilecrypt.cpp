#include "keyfilecrypt.hpp"

#include <array>
#include <cstring>

namespace cs {
namespace keyfile {

namespace {
using FileKey = cscrypto::MemGuard<cscrypto::Byte, crypto_aead_chacha20poly1305_KEYBYTES>;

// cscrypto's salt (cscrypto/src/privatekey.cpp, getKeyFromPassword)
constexpr std::array<cscrypto::Byte, crypto_pwhash_SALTBYTES> kSalt = {0x49, 0x44, 0x49, 0x20, 0x4e, 0x41, 0x48, 0x55,
                                                                       0x59, 0x20, 0x42, 0x4c, 0x59, 0x41, 0x54, 0x27};

// cscrypto's derivation with its identifiers pinned: Argon2id v1.3 and the argon2id SENSITIVE
// limits are the values of crypto_pwhash_ALG_DEFAULT and crypto_pwhash_OPSLIMIT/MEMLIMIT_SENSITIVE
// it uses. False when the key memory or the derivation's memory cannot be had.
bool deriveKey(FileKey& key, const char* passwd, std::size_t pswdLen) {
    return key && crypto_pwhash(key.data(), key.size(), passwd, pswdLen, kSalt.data(), crypto_pwhash_argon2id_OPSLIMIT_SENSITIVE,
                                crypto_pwhash_argon2id_MEMLIMIT_SENSITIVE, crypto_pwhash_ALG_ARGON2ID13) == 0;
}
}  // namespace

cscrypto::Bytes encryptWithNonce(const cscrypto::PrivateKey& key, const char* passwd, std::size_t pswdLen, const cscrypto::Byte* nonce) {
    cscrypto::Bytes result;
    if (!key || nonce == nullptr) {
        return result;
    }

    FileKey fileKey;
    if (!deriveKey(fileKey, passwd, pswdLen)) {
        return result;
    }

    cscrypto::Bytes out(kEncryptedSize);
    std::memcpy(out.data(), nonce, kNonceSize);
    auto sk = key.access();
    unsigned long long sealedLen = 0;
    if (crypto_aead_chacha20poly1305_encrypt(out.data() + kNonceSize, &sealedLen, sk.data(), sk.size(), nullptr, 0, nullptr, out.data(),
                                             fileKey.data()) != 0 ||
        sealedLen != kSealedSize) {
        sodium_memzero(out.data(), out.size());
        return result;
    }

    result.swap(out);
    return result;
}

cscrypto::Bytes encrypt(const cscrypto::PrivateKey& key, const char* passwd, std::size_t pswdLen) {
    std::array<cscrypto::Byte, kNonceSize> nonce;
    randombytes_buf(nonce.data(), nonce.size());
    return encryptWithNonce(key, passwd, pswdLen, nonce.data());
}

cscrypto::PrivateKey decrypt(const cscrypto::Bytes& bytes, const char* passwd, std::size_t pswdLen) {
    if (bytes.size() != kEncryptedSize) {
        return cscrypto::PrivateKey();
    }

    FileKey fileKey;
    if (!deriveKey(fileKey, passwd, pswdLen)) {
        return cscrypto::PrivateKey();
    }

    cscrypto::MemGuard<cscrypto::Byte, cscrypto::kPrivateKeySize> sk;
    if (!sk) {
        return cscrypto::PrivateKey();
    }
    // the output is exactly 64 bytes, which libsodium also zeroes on a bad tag
    unsigned long long skLen = 0;
    if (crypto_aead_chacha20poly1305_decrypt(sk.data(), &skLen, nullptr, bytes.data() + kNonceSize, kSealedSize, nullptr, 0, bytes.data(),
                                             fileKey.data()) != 0 ||
        skLen != cscrypto::kPrivateKeySize) {
        return cscrypto::PrivateKey();
    }
    return cscrypto::PrivateKey::readFromBytes(sk);
}

}  // namespace keyfile
}  // namespace cs
