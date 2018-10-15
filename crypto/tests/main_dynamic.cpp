#include <stdio.h>
#include <assert.h>
#include <iostream>
#include <string>

#include <cscrypto/cscrypto.h>

#include <gtest/gtest.h>

using namespace cscrypto;

// Awkward helper to load DLL/SO
namespace dll
{
#if defined(_MSC_VER) // Microsoft compiler

#include <windows.h>

#elif defined(__GNUC__) // GNU compiler

#include <dlfcn.h>

#else

#error define your copiler

#endif

  /*
  #define RTLD_LAZY   1
  #define RTLD_NOW    2
  #define RTLD_GLOBAL 4
  */

  void* LoadSharedLibrary(const char *pcDllname, int iMode = 2)
  {
    std::string sDllName = pcDllname;
#if defined(_MSC_VER) // Microsoft compiler
    sDllName += ".dll";
    return (void*)LoadLibrary(pcDllname);
#elif defined(__GNUC__) // GNU compiler
    sDllName += ".so";
    return dlopen(sDllName.c_str(), iMode);
#endif
  }
  void *GetFunction(void *Lib, const char *Fnname)
  {
#if defined(_MSC_VER) // Microsoft compiler
    return (void*)GetProcAddress((HINSTANCE)Lib, Fnname);
#elif defined(__GNUC__) // GNU compiler
    return dlsym(Lib, Fnname);
#endif
  }

  bool FreeSharedLibrary(void *hDLL)
  {
#if defined(_MSC_VER) // Microsoft compiler
    return FreeLibrary((HINSTANCE)hDLL) != 0;
#elif defined(__GNUC__) // GNU compiler
    return dlclose(hDLL);
#endif
  }
}

#include "cscrypto/cscrypto_c_api.h"

cscrypto_generate_key_pair_ptr cscrypto_generate_key_pair_func;
cscrypto_public_key_to_address_ptr cscrypto_public_key_to_address_func;
cscrypto_hash_ptr cscrypto_hash_func;
cscrypto_sign_ptr cscrypto_sign_func;
cscrypto_verify_ptr cscrypto_verify_func;

TEST( Blake2sTest, Test )
{
  char message[] = "The quick brown fox jumps over the lazy dog";

  unsigned char hash[CSCRYPTO_HASH_LENGTH] = {};

  cscrypto_hash_func( (unsigned char*)message, (unsigned int)strlen(message), hash );

  Bytes bytes( hash, hash + CSCRYPTO_HASH_LENGTH );
  std::string str = bytesToHexString( bytes );

  EXPECT_EQ(str, "606BEEEC743CCBEFF6CBCDF5D5302AA855C256C29B88C8ED331EA1A6BF3C8812");
}

TEST( SignVerifyTest, Test )
{
  char message[] = "The quick brown fox jumps over the lazy dog";

  // Generate key pair

  unsigned char publicKey[CSCRYPTO_PUBLIC_KEY_LENGTH] = {};
  unsigned char privateKey[CSCRYPTO_PRIVATE_KEY_LENGTH] = {};

  cscrypto_generate_key_pair_func(publicKey, privateKey);

  // Hash

  unsigned char hash[CSCRYPTO_HASH_LENGTH] = {};

  cscrypto_hash_func( (unsigned char*)message, (unsigned int)strlen( message ), hash );

  // Sign

  unsigned char signature[CSCRYPTO_SIGNATURE_LENGTH] = {};

  cscrypto_sign_func(hash, publicKey, privateKey, signature);

  // Verify

  int res = cscrypto_verify_func( hash, publicKey, signature );

  EXPECT_EQ( res, 0 );
}

int main(int argc, char** argv)
{
  void* dynamicLibrary = dll::LoadSharedLibrary("cscrypto_dynamicd");
  if ( dynamicLibrary == NULL )
  {
    std::cerr << "Shared library not found." << std::endl;
    getchar();
    return 1;
  }

#define LOAD_FUNC(N)	\
  N##_func = (N##_ptr)(dll::GetFunction(dynamicLibrary, #N))

  LOAD_FUNC( cscrypto_generate_key_pair );
  LOAD_FUNC( cscrypto_public_key_to_address );
  LOAD_FUNC( cscrypto_hash );
  LOAD_FUNC( cscrypto_sign );
  LOAD_FUNC( cscrypto_verify );

  int result = 0;
  {
    ::testing::InitGoogleTest(&argc, argv);
    result = RUN_ALL_TESTS();
  }

  dll::FreeSharedLibrary( dynamicLibrary );

  getchar();

  return result;
}
