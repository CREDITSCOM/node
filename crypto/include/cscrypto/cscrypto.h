#ifndef CSCRYPTO_H
#define CSCRYPTO_H

#include <stdint.h>
#include <array>
#include <vector>
#include <string>

//
// Library goal:
// Implement ECDSA most closely to how it Ethereum does, but take into account Credits technical specification (like use of EC 25519 for signing and blake2 for hashing).
//
// Differences to Ethereum
//
// (Refer to cpp-ethereum/libdevcrypto/Common.cpp)
//
// 1. Use of EC 25519 (as per Technical specs for Credits), Ethereum uses secp256k1 as EC
// 2. Generate key pair in a one function call (due to ed25519 limitations), Ethereum generates Secret first, then uses it to generate Public
// 3. Use blake2s hash function (as per Technical specs for Credits), Ethereum uses sha3 almost everywhere for hash
// 4. Use Publick Key for signing (due to ed25519 limitations), Ethereum doesn't use Public key for signing
// 5. Use 256 bit Public and 512 Secret (due to ed25519 limitations), Ethereum Public is 512, Secret is 256
//

namespace cscrypto
{
	using byte = uint8_t;
	using Bytes = std::vector<byte>;

	// NOTE: Code duplication with csdb
	inline char byteToHex(byte byte)
	{
		return (char)((byte < 10) ? (byte + '0') : (byte - 10 + 'A'));
	}

	inline byte hexToByte(char input)
	{
		if (input >= '0' && input <= '9')
			return input - '0';
		if (input >= 'A' && input <= 'F')
			return input - 'A' + 10;
		if (input >= 'a' && input <= 'f')
			return input - 'a' + 10;

		return 0;
	}

	template <class T>
	std::string bytesToHexString(const T& bytes)
	{
		const byte* data = bytes.data();
		size_t size = bytes.size();

		std::string res(size * 2, '\0');

		const byte* p = data;
		for (size_t i = 0; i < size; ++i, ++p)
		{
			res[i * 2] = byteToHex((*p >> 4) & 0x0F);
			res[i * 2 + 1] = byteToHex(*p & 0x0F);
		}

		return res;
	}

	inline Bytes hexStringToBytes(const std::string& hex)
	{
		size_t arrayLen = hex.size() / 2;
		Bytes bytes(arrayLen, 0);

		for (unsigned int i = 0; i < arrayLen; ++i)
		{
			bytes[i] = hexToByte( hex[2 * i] ) * 16 + hexToByte( hex[2 * i + 1] );
		}

		return bytes;
	}

	template <uint16_t BITS>
	struct FixedArray
	{
		static const uint16_t sizeBits = BITS;
		static const uint8_t sizeBytes = sizeBits / 8;

		typedef uint8_t byte;

		typedef std::array<byte, sizeBytes> Data;
		Data bytes = {};

		FixedArray() = default;
		explicit FixedArray(const Bytes& source)
		{
			size_t len = std::min((size_t)sizeBytes, source.size());

			bytes.fill(0);

			for (size_t i = 0; i < len; ++i)
				bytes[i] = source[i];
		}

		explicit FixedArray( const byte* source )
		{
			for ( size_t i = 0; i < sizeBytes; ++i )
				bytes[i] = source[i];
		}

		std::string toString() const
		{
			std::string digest = bytesToHexString(bytes);
			return digest;
		}

		typename Data::pointer data() { return bytes.data(); }
		typename Data::const_pointer data() const { return bytes.data(); }

		size_t size() const { return sizeBytes; }
	};

	using PublicKey = FixedArray<256>;
	using PrivateKey = FixedArray<512>;
	using Address = FixedArray<160>;
	using Signature = FixedArray<512>;
	using Hash = FixedArray<256>;

	struct KeyPair
	{
		PublicKey publicKey;
		PrivateKey privateKey;
	};

	Hash blake2s(const byte* data, size_t length);

	template <class T>
	Hash blake2s(const T& msg)
	{
		const byte* data = reinterpret_cast<const byte*>(msg.data());
		Hash result = blake2s(data, msg.size());
		return result;
	}

	KeyPair generateKeyPair();

	Address toAddress(const PublicKey& publicKey);

	Signature sign(const Hash& hash, const KeyPair& keyPair);

	bool verify(const PublicKey& publicKey, const Hash& hash, const Signature& signature);
}

#endif // CSCRYPTO_H
