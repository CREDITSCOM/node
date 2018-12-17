#ifndef CSCRYPTO_PRIVATE_KEY
#define CSCRYPTO_PRIVATE_KEY

#include "cscrypto/memoryprotection.hpp"
#include "cscrypto/cryptotypes.hpp"

namespace cscrypto {

using PrivateKeyGuard = MemAccessGuard<cscrypto::Byte, kPrivateKeySize>;

class PrivateKey {
public:
  PrivateKey();
  ~PrivateKey() { clear(); }

  PrivateKey(const PrivateKey&);
  PrivateKey(PrivateKey&&);
  PrivateKey& operator=(const PrivateKey&);
  PrivateKey& operator=(PrivateKey&&);

  PrivateKeyGuard access() const;
  operator bool() const { return mem_; }

  static PrivateKey readFromBytes(const std::vector<Byte>&);
  static PrivateKey readFromEncrypted(const std::vector<Byte>&, const char* passwd);

  std::vector<Byte> getEncrypted(const char* passwd) const;
  static PrivateKey generateWithPair(PublicKey&);

private:
  void clear();

  void* mem_;
  uint32_t* ctr_;
};

} // namespace cscrypto
#endif // CSCRYPTO_PRIVATE_KEY
