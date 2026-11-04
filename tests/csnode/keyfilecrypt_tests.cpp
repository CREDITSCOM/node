#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <base58.h>
#include <cscrypto/cscrypto.hpp>

#include <keyfilecrypt.hpp>

// The key file keeps cscrypto's format: these tests compare the bytes with cscrypto's own writer
// and reader (PrivateKey::getEncrypted / readFromEncrypted, used by every earlier version). Each
// derivation is Argon2id with 1 GiB: the tests keep their number low.

namespace {
// a NodePrivate.txt encrypted by cscrypto's PrivateKey::getEncrypted (an earlier version), with the
// password "node password"
const char* kVectorFile = "2goaN1R5N6WKdz9baKJXWovLDccavzzWR25JKeHj9goSRWFtPVC6qRuqjS74e7hvND4hxDJtv3XeZgLQJ6NPoMAy4YWnUxFUghgaizwodzbU4Zaw6wo55fB5i";
const char* kVectorPublic = "FAe4sisG95oZ42w7buUn5qEE4TAnfTTFPiguZUHmhiF";
const std::string kVectorPassword = "node password";

cscrypto::Bytes decode(const std::string& s) {
    std::vector<uint8_t> out;
    DecodeBase58(s, out);
    return out;
}

std::string publicOf(const cscrypto::PrivateKey& sk) {
    auto pk = cscrypto::getMatchingPublic(sk);
    return EncodeBase58(pk.data(), pk.data() + pk.size());
}

class KeyFileCrypt : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(cscrypto::cryptoInit());
        sk_ = cscrypto::PrivateKey::generateWithPair(pk_);
    }

    cscrypto::PrivateKey sk_;
    cscrypto::PublicKey pk_;
};
}  // namespace

TEST_F(KeyFileCrypt, Size) {
    EXPECT_EQ(cs::keyfile::kEncryptedSize, 88u);
}

// a file cscrypto wrote opens, and encrypting its key again with the same password and nonce gives
// the same bytes: the derivation, the cipher and the layout are cscrypto's
TEST_F(KeyFileCrypt, ByteIdenticalToCscryptoVector) {
    const auto bytes = decode(kVectorFile);
    ASSERT_EQ(bytes.size(), cs::keyfile::kEncryptedSize);
    auto sk = cs::keyfile::decrypt(bytes, kVectorPassword.data(), kVectorPassword.size());
    ASSERT_TRUE(sk);
    EXPECT_EQ(publicOf(sk), kVectorPublic);

    const auto again = cs::keyfile::encryptWithNonce(sk, kVectorPassword.data(), kVectorPassword.size(), bytes.data());
    EXPECT_EQ(again, bytes);
    EXPECT_EQ(EncodeBase58(again.data(), again.data() + again.size()), kVectorFile);
}

// each way with cscrypto's code: what this node writes, cscrypto's reader opens (the reader of
// earlier versions), and what cscrypto writes, this node opens; a new nonce every time
TEST_F(KeyFileCrypt, CscryptoReadsOursAndWeReadCscrypto) {
    const std::string pw = "round trip ~ 42";
    const auto ours = cs::keyfile::encrypt(sk_, pw.data(), pw.size());
    ASSERT_EQ(ours.size(), cs::keyfile::kEncryptedSize);
    auto byCscrypto = cscrypto::PrivateKey::readFromEncrypted(ours, pw.data(), pw.size());
    ASSERT_TRUE(byCscrypto);
    EXPECT_EQ(cscrypto::getMatchingPublic(byCscrypto), pk_);

    const auto theirs = sk_.getEncrypted(pw.data(), pw.size());
    ASSERT_EQ(theirs.size(), cs::keyfile::kEncryptedSize);
    EXPECT_NE(cscrypto::Bytes(ours.begin(), ours.begin() + cs::keyfile::kNonceSize),
              cscrypto::Bytes(theirs.begin(), theirs.begin() + cs::keyfile::kNonceSize));
    const std::string text = EncodeBase58(theirs.data(), theirs.data() + theirs.size());
    auto byUs = cs::keyfile::decrypt(decode(text), pw.data(), pw.size());
    ASSERT_TRUE(byUs);
    EXPECT_EQ(cscrypto::getMatchingPublic(byUs), pk_);
}

TEST_F(KeyFileCrypt, WrongPasswordAndChangedByteRefused) {
    const auto bytes = decode(kVectorFile);
    const std::string wrong = "node passwore";
    EXPECT_FALSE(cs::keyfile::decrypt(bytes, wrong.data(), wrong.size()));

    auto changed = bytes;
    changed[cs::keyfile::kNonceSize + 10] ^= 1;  // the ciphertext: the tag does not verify
    EXPECT_FALSE(cs::keyfile::decrypt(changed, kVectorPassword.data(), kVectorPassword.size()));
}

// another length is not an encrypted key: refused before any derivation
TEST_F(KeyFileCrypt, OtherLengthsRefused) {
    const std::string pw = "pw";
    for (std::size_t n : {0u, 8u, 64u, 72u, 87u, 89u, 96u, 104u, 128u}) {
        cscrypto::Bytes bytes(n, 1);
        EXPECT_FALSE(cs::keyfile::decrypt(bytes, pw.data(), pw.size())) << n;
    }
}

// a failed derivation (here refused at once by Argon2id: no password bytes for a length of 5, as an
// allocation failure would be) is an error, never an empty file nor a crash, in both directions
TEST_F(KeyFileCrypt, FailedDerivationIsAnError) {
    EXPECT_TRUE(cs::keyfile::encrypt(sk_, nullptr, 5).empty());
    EXPECT_TRUE(cs::keyfile::encrypt(cscrypto::PrivateKey(), "pw", 2).empty());
    EXPECT_FALSE(cs::keyfile::decrypt(decode(kVectorFile), nullptr, 5));
}
