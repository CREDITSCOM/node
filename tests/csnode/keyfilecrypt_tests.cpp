#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <base58.h>
#include <cscrypto/cscrypto.hpp>

#include <keyfilecrypt.hpp>

// Each derivation is the node's Argon2id with 1 GiB of memory: the tests keep their number low.

namespace {
// a NodePrivate.txt encrypted by the node's previous code (cscrypto's PrivateKey::getEncrypted),
// with the key below and the password "node password"
const char* kLegacyFile = "2goaN1R5N6WKdz9baKJXWovLDccavzzWR25JKeHj9goSRWFtPVC6qRuqjS74e7hvND4hxDJtv3XeZgLQJ6NPoMAy4YWnUxFUghgaizwodzbU4Zaw6wo55fB5i";
const char* kLegacyPublic = "FAe4sisG95oZ42w7buUn5qEE4TAnfTTFPiguZUHmhiF";
const std::string kLegacyPassword = "node password";

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

TEST_F(KeyFileCrypt, SizesOfTheTwoFormats) {
    EXPECT_EQ(cs::keyfile::kLegacyEncryptedSize, 88u);
    EXPECT_EQ(cs::keyfile::kSaltedEncryptedSize, 104u);
}

// a file written by older versions opens as before
TEST_F(KeyFileCrypt, LegacyFileOpens) {
    const auto bytes = decode(kLegacyFile);
    ASSERT_EQ(bytes.size(), cs::keyfile::kLegacyEncryptedSize);
    auto sk = cs::keyfile::decrypt(bytes, kLegacyPassword.data(), kLegacyPassword.size());
    ASSERT_TRUE(sk);
    EXPECT_EQ(publicOf(sk), kLegacyPublic);
}

// a new encryption: 104 bytes, a random salt and nonce each time, opens with its password only,
// and not with a changed salt
TEST_F(KeyFileCrypt, NewFormatRoundTripWrongPasswordChangedSalt) {
    const std::string pw = "round trip ~ 42";
    const auto a = cs::keyfile::encrypt(sk_, pw.data(), pw.size());
    const auto b = cs::keyfile::encrypt(sk_, pw.data(), pw.size());
    ASSERT_EQ(a.size(), cs::keyfile::kSaltedEncryptedSize);
    ASSERT_EQ(b.size(), cs::keyfile::kSaltedEncryptedSize);
    EXPECT_NE(cscrypto::Bytes(a.begin(), a.begin() + cs::keyfile::kSaltSize), cscrypto::Bytes(b.begin(), b.begin() + cs::keyfile::kSaltSize));
    EXPECT_NE(a, b);

    // the base58 text the node writes and reads back
    const std::string text = EncodeBase58(a.data(), a.data() + a.size());
    auto sk = cs::keyfile::decrypt(decode(text), pw.data(), pw.size());
    ASSERT_TRUE(sk);
    EXPECT_EQ(cscrypto::getMatchingPublic(sk), pk_);

    const std::string wrong = "round trip ~ 43";
    EXPECT_FALSE(cs::keyfile::decrypt(a, wrong.data(), wrong.size()));

    auto badSalt = a;
    badSalt[3] ^= 1;
    EXPECT_FALSE(cs::keyfile::decrypt(badSalt, pw.data(), pw.size()));
}

// another length is not an encrypted key: refused before any derivation
TEST_F(KeyFileCrypt, OtherLengthsRefused) {
    const std::string pw = "pw";
    for (std::size_t n : {0u, 8u, 64u, 72u, 87u, 89u, 96u, 103u, 105u, 128u}) {
        cscrypto::Bytes bytes(n, 1);
        EXPECT_FALSE(cs::keyfile::decrypt(bytes, pw.data(), pw.size())) << n;
    }
}

// the old writer's output is still read (the file is not converted)
TEST_F(KeyFileCrypt, LegacyWriterOutputOpens) {
    const std::string pw = "legacy";
    const auto enc = sk_.getEncrypted(pw.data(), pw.size());
    ASSERT_EQ(enc.size(), cs::keyfile::kLegacyEncryptedSize);
    auto sk = cs::keyfile::decrypt(enc, pw.data(), pw.size());
    ASSERT_TRUE(sk);
    EXPECT_EQ(cscrypto::getMatchingPublic(sk), pk_);
}
