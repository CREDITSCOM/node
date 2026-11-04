#ifndef CLIENT_CONFIG_KEYFILECRYPT_HPP
#define CLIENT_CONFIG_KEYFILECRYPT_HPP

#include <cstddef>

#include <cscrypto/cscrypto.hpp>

namespace cs {
namespace keyfile {

// The encrypted private key file (NodePrivate.txt), decoded from base58: the format of cscrypto's
// PrivateKey::getEncrypted / readFromEncrypted, which every node writes and reads, unchanged:
//
//   nonce (8) || crypto_aead_chacha20poly1305 (original construction, no associated data) of the
//   64-byte secret key: ciphertext + tag (64 + 16) = 88 bytes
//
// with the key derived by crypto_pwhash (Argon2id v1.3, OPSLIMIT_SENSITIVE, MEMLIMIT_SENSITIVE)
// and cscrypto's salt. The functions here write and read exactly those bytes; they differ from
// cscrypto's only in their checks: a failed allocation or derivation is an error (an empty result)
// and any other length is refused before decryption. 64 bytes is a plain key.
constexpr std::size_t kNonceSize = crypto_aead_chacha20poly1305_NPUBBYTES;
constexpr std::size_t kSealedSize = cscrypto::kPrivateKeySize + crypto_aead_chacha20poly1305_ABYTES;
constexpr std::size_t kEncryptedSize = kNonceSize + kSealedSize;  // 88

// a new encryption with a random nonce; empty on failure (no key, or the derivation failed)
cscrypto::Bytes encrypt(const cscrypto::PrivateKey& key, const char* passwd, std::size_t pswdLen);

// the same with the given nonce (kNonceSize bytes): for the tests, which compare the bytes with
// files written by cscrypto
cscrypto::Bytes encryptWithNonce(const cscrypto::PrivateKey& key, const char* passwd, std::size_t pswdLen, const cscrypto::Byte* nonce);

// an empty key on a wrong password, a changed file, another length or a failed derivation
cscrypto::PrivateKey decrypt(const cscrypto::Bytes& bytes, const char* passwd, std::size_t pswdLen);

}  // namespace keyfile
}  // namespace cs

#endif  // CLIENT_CONFIG_KEYFILECRYPT_HPP
