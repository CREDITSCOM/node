#include <stdio.h>
#include <assert.h>
#include <iostream>
#include <string>

#include <cscrypto/cscrypto.h>

#include <gtest/gtest.h>

using namespace cscrypto;

//
// Bytes <-> Hex string
//

TEST(HexToStringTest, Test)
{
	std::string str = "69217A3079908094E11121D042354A7C1F55B6482CA1A51E1B250DFD1ED0EEF9";

	Bytes bytes = hexStringToBytes(str);
	
	std::string res = bytesToHexString(bytes);

	EXPECT_EQ(str, res);
}

//
//	Blake2s
//

TEST(Blake2sTests, EmptyMessage)
{
	Bytes bytes = {};
	Hash h = blake2s(bytes);

	std::string str = h.toString();
	
	EXPECT_EQ(str, "69217A3079908094E11121D042354A7C1F55B6482CA1A51E1B250DFD1ED0EEF9");
}

TEST(Blake2sTests, SomeMessage)
{
	std::string src = "The quick brown fox jumps over the lazy dog";

	Bytes bytes;
	std::copy(src.begin(), src.end(), std::back_inserter(bytes));

	Hash h = blake2s(bytes);

	std::string str = h.toString();

	EXPECT_EQ(str, "606BEEEC743CCBEFF6CBCDF5D5302AA855C256C29B88C8ED331EA1A6BF3C8812");
}

//
//	Signature
//

class SignatureTest : public testing::Test
{
protected:

	KeyPair keyPair;
	std::string message;
	Hash hash;
	Signature signature;

	SignatureTest()
	{
		keyPair = generateKeyPair();

		message = "This is a message";
		hash = blake2s(message);

		signature = sign(hash, keyPair);
	}

	virtual void SetUp() override
	{
	}
};

TEST_F(SignatureTest, VerifyValidMessage)
{
	bool ok = verify(keyPair.publicKey, hash, signature);
	EXPECT_TRUE(ok);
}

TEST_F(SignatureTest, VerifyInvalidMessage)
{
	std::string message = "This is a wrong message";
	Hash hash = blake2s(message);

	bool ok = verify(keyPair.publicKey, hash, signature);
	EXPECT_FALSE(ok);
}

TEST_F(SignatureTest, VerifyUsingInvalidKeyPair)
{
	KeyPair keyPair = generateKeyPair();

	bool ok = verify(keyPair.publicKey, hash, signature);
	EXPECT_FALSE(ok);
}

//
//	Empty signature
//

class EmptySignatureTest : public testing::Test
{
protected:

	KeyPair keyPair = {};
	std::string message = {};
	Hash hash = {};
	Signature signature = {};

	EmptySignatureTest()
	{
		signature = sign(hash, keyPair);
	}
};

TEST_F(EmptySignatureTest, DISABLED_VerifyValidMessage)
{
	bool ok = verify(keyPair.publicKey, hash, signature);
	EXPECT_TRUE(ok);
}

TEST_F(EmptySignatureTest, DISABLED_VerifyInvalidMessage)
{
	std::string message = "This is a wrong message";
	Hash hash = blake2s(message);

	bool ok = verify(keyPair.publicKey, hash, signature);
	EXPECT_FALSE(ok);
}

TEST_F(EmptySignatureTest, VerifyUsingInvalidKeyPair)
{
	KeyPair keyPair = generateKeyPair();

	bool ok = verify(keyPair.publicKey, hash, signature);
	EXPECT_FALSE(ok);
}

//
//	Known values test
//

TEST(KnownValuesTest, Test)
{
	Bytes publicKeyBytes = hexStringToBytes("97BB18C86FAB754E3DE46A9B3330F550480EB525E3AAA19FEBC1F1AD8F6EF027");
	Bytes privateKeyBytes = hexStringToBytes("10185181807B431D077672DED634724271933313B6DF33803201E67B1B453674866548F1C590356A555A38571B484125B2CD51CD3EA50322655EECB3FBB617EA");

	KeyPair keyPair{ PublicKey{publicKeyBytes}, PrivateKey{privateKeyBytes} };

	Bytes signatureBytes = hexStringToBytes("AC6B142216199F6418677ECDB4735454B9EC51BFA3E5B180F6EF90FB671BA098BD28AC9CE6E65138B56EC631E5527BB2638E3D551BF0EADEAF6C80BCC827C20C");

	Signature signature;
	std::copy_n(std::make_move_iterator(signatureBytes.begin()), signature.sizeBytes, std::begin(signature.bytes));

	std::string message = "This is a message";
	Hash hash = blake2s(message);

	bool ok = verify(keyPair.publicKey, hash, signature);
	EXPECT_TRUE(ok);
}

int main(int argc, char** argv)
{
  int result = 0;
  {
    ::testing::InitGoogleTest(&argc, argv);
    result = RUN_ALL_TESTS();
  }

  getchar();

  return result;
}
