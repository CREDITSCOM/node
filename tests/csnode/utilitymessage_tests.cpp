#include "gtest/gtest.h"

#include <limits>

#include <cscrypto/cscrypto.hpp>
#include <csnode/odatastream.hpp>
#include <csnode/utilitymessage.hpp>

using cs::isUtilityRoundInWindow;
using cs::readUtilityMessage;
using Status = cs::UtilityMessage::Status;

namespace {
struct Signer {
    cs::PublicKey pub;
    cs::PrivateKey priv;
};

// keys live in sodium's guarded memory: the library must be initialised first
Signer signer() {
    EXPECT_TRUE(cscrypto::cryptoInit());
    Signer s;
    s.priv = cscrypto::generateKeyPair(s.pub);
    return s;
}

cs::PublicKey key(int i) {
    cs::PublicKey k{};
    k[0] = static_cast<uint8_t>(i);
    k[31] = static_cast<uint8_t>(0xA0 + i);
    return k;
}

// the payload as Node::getUtilityMessage reads it: round, order, key in the node's stream encoding
cs::Bytes payload(cs::RoundNumber round, cs::Byte order, const cs::PublicKey& k) {
    cs::Bytes bytes;
    cs::ODataStream out(bytes);
    out << round << order << k;
    return bytes;
}

// never over an empty vector: generateSignature asserts on a null pointer
cs::Signature sign(const Signer& s, const cs::Bytes& bytes) {
    return cscrypto::generateSignature(s.priv, bytes.data(), bytes.size());
}
}  // namespace

TEST(UtilityMessage, PayloadIsRoundOrderAndKey) {
    EXPECT_EQ(cs::kUtilityPayloadSize, 41u);
    EXPECT_EQ(payload(1, cs::kUtilityRelease, key(1)).size(), cs::kUtilityPayloadSize);
    EXPECT_EQ(cs::kUtilityRelease, 0);
    EXPECT_EQ(cs::kUtilitySeal, 1);
}

TEST(UtilityMessage, ASignedReleaseIsRead) {
    const auto s = signer();
    const auto bytes = payload(500, cs::kUtilityRelease, key(1));
    const auto m = readUtilityMessage(bytes, sign(s, bytes), s.pub);
    ASSERT_EQ(m.status, Status::Ok);
    EXPECT_EQ(m.round, 500u);
    EXPECT_EQ(m.order, cs::kUtilityRelease);
    EXPECT_EQ(m.key, key(1));
}

TEST(UtilityMessage, ASignedSealIsRead) {
    const auto s = signer();
    const auto bytes = payload(9001, cs::kUtilitySeal, key(2));
    const auto m = readUtilityMessage(bytes, sign(s, bytes), s.pub);
    ASSERT_EQ(m.status, Status::Ok);
    EXPECT_EQ(m.round, 9001u);
    EXPECT_EQ(m.order, cs::kUtilitySeal);
    EXPECT_EQ(m.key, key(2));
}

// the message that any peer could send since af4de3f8
TEST(UtilityMessage, AnotherKeysSignatureIsRefused) {
    const auto s = signer();
    const auto other = signer();
    const auto bytes = payload(500, cs::kUtilitySeal, key(1));
    const auto m = readUtilityMessage(bytes, sign(other, bytes), s.pub);
    EXPECT_EQ(m.status, Status::BadSignature);
    // nothing is read from a refused message
    EXPECT_EQ(m.round, 0u);
    EXPECT_EQ(m.order, 0);
    EXPECT_EQ(m.key, cs::PublicKey{});
    // the same bytes are an order for whoever trusts the other key
    EXPECT_EQ(readUtilityMessage(bytes, sign(other, bytes), other.pub).status, Status::Ok);
}

// the signature covers the round, the order and every byte of the key
TEST(UtilityMessage, APayloadAlteredAfterSigningIsRefused) {
    const auto s = signer();
    const auto bytes = payload(500, cs::kUtilitySeal, key(1));
    const auto sig = sign(s, bytes);
    ASSERT_EQ(readUtilityMessage(bytes, sig, s.pub).status, Status::Ok);
    EXPECT_EQ(readUtilityMessage(payload(501, cs::kUtilitySeal, key(1)), sig, s.pub).status, Status::BadSignature);
    EXPECT_EQ(readUtilityMessage(payload(500, cs::kUtilityRelease, key(1)), sig, s.pub).status, Status::BadSignature);
    EXPECT_EQ(readUtilityMessage(payload(500, cs::kUtilitySeal, key(2)), sig, s.pub).status, Status::BadSignature);
    for (size_t i = 0; i < bytes.size(); ++i) {
        auto flipped = bytes;
        flipped[i] ^= 1;
        EXPECT_EQ(readUtilityMessage(flipped, sig, s.pub).status, Status::BadSignature) << "byte " << i;
    }
}

// a signature of the right size by the right key, but not over the payload
TEST(UtilityMessage, ASignatureOverOtherBytesIsRefused) {
    const auto s = signer();
    const auto bytes = payload(500, cs::kUtilitySeal, key(1));
    // the order and the key without the round, as a sender that lets the round be restamped would sign
    const cs::Bytes tail(bytes.begin() + sizeof(cs::RoundNumber), bytes.end());
    EXPECT_EQ(readUtilityMessage(bytes, sign(s, tail), s.pub).status, Status::BadSignature);
    // all but the last byte
    const cs::Bytes head(bytes.begin(), bytes.end() - 1);
    EXPECT_EQ(readUtilityMessage(bytes, sign(s, head), s.pub).status, Status::BadSignature);
    // the packet's encoding of the payload (its size first), as a sender that signs the packet would
    cs::Bytes packet;
    cs::ODataStream out(packet);
    out << bytes;
    EXPECT_EQ(readUtilityMessage(bytes, sign(s, packet), s.pub).status, Status::BadSignature);
    // the key alone
    const auto k = key(1);
    const cs::Bytes keyOnly(k.begin(), k.end());
    EXPECT_EQ(readUtilityMessage(bytes, sign(s, keyOnly), s.pub).status, Status::BadSignature);
}

// signed by the right key over what is there, so that it is the format that refuses it
TEST(UtilityMessage, ATruncatedPayloadIsRefused) {
    const auto s = signer();
    const auto bytes = payload(500, cs::kUtilitySeal, key(1));
    const size_t cuts[] = {bytes.size() - 1, sizeof(cs::RoundNumber) + 1, sizeof(cs::RoundNumber), 1};
    for (const size_t n : cuts) {
        const cs::Bytes cut(bytes.begin(), bytes.begin() + n);
        const auto m = readUtilityMessage(cut, sign(s, cut), s.pub);
        EXPECT_EQ(m.status, Status::BadFormat) << n << " bytes";
        EXPECT_EQ(m.key, cs::PublicKey{}) << n << " bytes";
    }
}

TEST(UtilityMessage, APaddedPayloadIsRefused) {
    const auto s = signer();
    auto bytes = payload(500, cs::kUtilitySeal, key(1));
    bytes.push_back(0);
    EXPECT_EQ(readUtilityMessage(bytes, sign(s, bytes), s.pub).status, Status::BadFormat);
    bytes.resize(bytes.size() + cs::kUtilityPayloadSize, 0);
    EXPECT_EQ(readUtilityMessage(bytes, sign(s, bytes), s.pub).status, Status::BadFormat);
}

// an empty vector has no data pointer: refused before the signature is looked at
TEST(UtilityMessage, AnEmptyPayloadIsRefused) {
    const auto s = signer();
    const auto bytes = payload(500, cs::kUtilitySeal, key(1));
    EXPECT_EQ(readUtilityMessage(cs::Bytes{}, sign(s, bytes), s.pub).status, Status::BadFormat);
    EXPECT_EQ(readUtilityMessage(cs::Bytes{}, cs::Signature{}, s.pub).status, Status::BadFormat);
}

TEST(UtilityMessage, AnEmptySignatureIsRefused) {
    const auto s = signer();
    const auto bytes = payload(500, cs::kUtilitySeal, key(1));
    EXPECT_EQ(readUtilityMessage(bytes, cs::Signature{}, s.pub).status, Status::BadSignature);
    // and a zero signer, the starter key a node had from 414c5c47 to af4de3f8, verifies nothing
    EXPECT_EQ(readUtilityMessage(bytes, sign(s, bytes), cs::PublicKey{}).status, Status::BadSignature);
}

// an order the node does not know is read as sent (Node::getUtilityMessage logs it), but only signed
TEST(UtilityMessage, AnUnknownOrderIsPassedThrough) {
    const auto s = signer();
    const cs::Byte orders[] = {2, 0x7F, 0xFF};
    for (const cs::Byte order : orders) {
        const auto bytes = payload(500, order, key(1));
        const auto m = readUtilityMessage(bytes, sign(s, bytes), s.pub);
        ASSERT_EQ(m.status, Status::Ok) << int(order);
        EXPECT_EQ(m.order, order);
        EXPECT_EQ(m.key, key(1));
        EXPECT_EQ(readUtilityMessage(bytes, sign(signer(), bytes), s.pub).status, Status::BadSignature) << int(order);
    }
}

// Release of the zero key is "release all" (not implemented), Seal of it is invalid: both are the
// caller's to tell apart, from a signed message only
TEST(UtilityMessage, AZeroKeyIsPassedThrough) {
    const auto s = signer();
    const cs::Byte orders[] = {cs::kUtilityRelease, cs::kUtilitySeal};
    for (const cs::Byte order : orders) {
        const auto bytes = payload(500, order, cs::Zero::key);
        const auto m = readUtilityMessage(bytes, sign(s, bytes), s.pub);
        ASSERT_EQ(m.status, Status::Ok) << int(order);
        EXPECT_EQ(m.order, order);
        EXPECT_EQ(m.key, cs::Zero::key);
        EXPECT_EQ(readUtilityMessage(bytes, sign(signer(), bytes), s.pub).status, Status::BadSignature) << int(order);
    }
}

// the rounds a signed message is obeyed for: its own, any later one (a node behind the network, or
// restarted with no round yet) and up to a stop request's age before it
TEST(UtilityMessage, CurrentAndFutureRoundsAreInTheWindow) {
    EXPECT_TRUE(isUtilityRoundInWindow(500, 500));
    EXPECT_TRUE(isUtilityRoundInWindow(501, 500));
    EXPECT_TRUE(isUtilityRoundInWindow(9001, 500));
    EXPECT_TRUE(isUtilityRoundInWindow(500, 0));
    EXPECT_TRUE(isUtilityRoundInWindow(0, 0));
}

TEST(UtilityMessage, OlderRoundsAreNot) {
    EXPECT_EQ(cs::kUtilityRoundsBehind, 100u);
    EXPECT_EQ(cs::kUtilityRoundsBehind, cs::MaxRoundDeltaInStopRequest);
    EXPECT_TRUE(isUtilityRoundInWindow(400, 500));
    EXPECT_FALSE(isUtilityRoundInWindow(399, 500));
    EXPECT_FALSE(isUtilityRoundInWindow(0, 500));
    EXPECT_TRUE(isUtilityRoundInWindow(0, 100));
    EXPECT_FALSE(isUtilityRoundInWindow(0, 101));
}

TEST(UtilityMessage, NoOverflowNearTheTop) {
    const auto max = std::numeric_limits<cs::RoundNumber>::max();
    EXPECT_TRUE(isUtilityRoundInWindow(max, max));
    EXPECT_TRUE(isUtilityRoundInWindow(max, 0));
    EXPECT_TRUE(isUtilityRoundInWindow(max - 100, max));
    EXPECT_FALSE(isUtilityRoundInWindow(max - 101, max));
    EXPECT_FALSE(isUtilityRoundInWindow(0, max));
}
