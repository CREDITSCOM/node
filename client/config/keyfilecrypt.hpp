#ifndef CLIENT_CONFIG_KEYFILECRYPT_HPP
#define CLIENT_CONFIG_KEYFILECRYPT_HPP

#include <cstddef>

#include <cscrypto/cscrypto.hpp>

namespace cs {
namespace keyfile {

// The encrypted private key file (NodePrivate.txt), decoded from base58. The two formats are told
// apart by their length:
//
//   legacy (88 bytes):  nonce (8) || ciphertext + tag (64 + 16)
//                       written by older versions: cscrypto's PrivateKey::getEncrypted,
//                       key = Argon2id with a salt that is the same for every file. Read with the same derivation and cipher (here, so that a
//                       failed derivation is an error), never rewritten.
//   salted (104 bytes): salt (16) || nonce (8) || ciphertext + tag (64 + 16)
//                       written by this version: key = crypto_pwhash (Argon2id v1.3, the argon2id
//                       OPSLIMIT_SENSITIVE and MEMLIMIT_SENSITIVE) with a random salt per file, then the same
//                       crypto_aead_chacha20poly1305 (original construction, 8-byte nonce, no
//                       associated data) as the legacy format.
//
// Any other length is not an encrypted key (64 bytes is a plain key).
constexpr std::size_t kSaltSize = crypto_pwhash_SALTBYTES;
constexpr std::size_t kNonceSize = crypto_aead_chacha20poly1305_NPUBBYTES;
constexpr std::size_t kSealedSize = cscrypto::kPrivateKeySize + crypto_aead_chacha20poly1305_ABYTES;
constexpr std::size_t kLegacyEncryptedSize = kNonceSize + kSealedSize;              // 88
constexpr std::size_t kSaltedEncryptedSize = kSaltSize + kNonceSize + kSealedSize;  // 104

// the salted format for a new encryption; empty on failure (no key, or the derivation failed)
cscrypto::Bytes encrypt(const cscrypto::PrivateKey& key, const char* passwd, std::size_t pswdLen);

// either format by its length; an empty key on a wrong password, a changed file, another length
// or a failed derivation
cscrypto::PrivateKey decrypt(const cscrypto::Bytes& bytes, const char* passwd, std::size_t pswdLen);

}  // namespace keyfile
}  // namespace cs

#endif  // CLIENT_CONFIG_KEYFILECRYPT_HPP
