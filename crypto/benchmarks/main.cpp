#include <benchmark/benchmark.h>
#include <cscrypto/cscrypto.h>
#include <ed25519.h>
#include <functional>

using namespace cscrypto;

//
// Key pair
//

static void bm_gen_keypair(benchmark::State& state)
{
    static int i = 0;
    
	for (auto _ : state)
	{
        std::function<int(void)> func = [&]()
            {
                KeyPair keyPair = generateKeyPair();
                
                return i++;
            };
        
		benchmark::DoNotOptimize( func() );
	}
}
BENCHMARK(bm_gen_keypair);

static void bm_ed25519_seed(benchmark::State& state)
{
    static int i = 0;
    
    for (auto _ : state)
    {
        std::function<int(void)> func = [&]()
        {
            byte seed[32] = {};
            ed25519_create_seed(seed);
            
            return i++;
        };
        
        benchmark::DoNotOptimize( func() );
    }
}
BENCHMARK(bm_ed25519_seed);

static void bm_ed25519_gen_kp(benchmark::State& state)
{
    static int i = 0;
    
    byte seed[32] = {};
    ed25519_create_seed(seed);
    
    PublicKey publicKey;
    PrivateKey privateKey;
    
    for (auto _ : state)
    {
        std::function<int(void)> func = [&]()
        {
            ed25519_create_keypair(publicKey.data(), privateKey.data(), seed);
            
            return i++;
        };
        
        benchmark::DoNotOptimize( func() );
    }
}
BENCHMARK(bm_ed25519_gen_kp);

static void bm_fopen(benchmark::State& state)
{
    static int i = 0;
    
    byte seed[32] = {};
    
    for (auto _ : state)
    {
        std::function<int(void)> func = [&]()
        {

#ifndef WIN32
            state.PauseTiming();
            
            FILE *f = fopen("/dev/urandom", "rb");
            
            state.ResumeTiming();
            
            if ( f != NULL )
                fread(seed, 1, 32, f);
            
            state.PauseTiming();
            
            if ( f != NULL )
                fclose(f);
            
            state.ResumeTiming();

#endif // WIN32

            return i++;
        };
        
        benchmark::DoNotOptimize( func() );
    }
}
BENCHMARK(bm_fopen);

//
// Hash
//

static void bm_blake2s( benchmark::State& state )
{
    static int i = 0;

    for ( auto _ : state )
    {
        auto func = [&]()
        {
            state.PauseTiming();

            using bytes_t = std::vector<uint8_t>;
            bytes_t bytes;
            bytes.resize( 1024 );

            std::generate( std::begin(bytes), std::end(bytes), [](){ return rand(); } );

            state.ResumeTiming();

            Hash hash = blake2s( bytes.data(), bytes.size() );

            return i++;
        };

        benchmark::DoNotOptimize( func() );
    }
}
BENCHMARK( bm_blake2s );

//
// Sign/Verify
//

static void bm_sign( benchmark::State& state )
{
    static int i = 0;

    for ( auto _ : state )
    {
        auto func = [&]()
        {
            state.PauseTiming();

            using bytes_t = std::vector<uint8_t>;
            bytes_t bytes;
            bytes.resize( 1024 );

            std::generate( std::begin( bytes ), std::end( bytes ), []() { return rand(); } );

            Hash hash = blake2s( bytes.data(), bytes.size() );

            KeyPair kp = generateKeyPair();

            state.ResumeTiming();

            Signature signature = sign(hash, kp);

            return i++;
        };

        benchmark::DoNotOptimize( func() );
    }
}
BENCHMARK( bm_sign );

static void bm_verify( benchmark::State& state )
{
    static int i = 0;

    for ( auto _ : state )
    {
        auto func = [&]()
        {
            state.PauseTiming();

            using bytes_t = std::vector<uint8_t>;
            bytes_t bytes;
            bytes.resize( 1024 );

            std::generate( std::begin( bytes ), std::end( bytes ), []() { return rand(); } );

            Hash hash = blake2s( bytes.data(), bytes.size() );

            KeyPair kp = generateKeyPair();

            Signature signature = sign( hash, kp );

            state.ResumeTiming();

            verify(kp.publicKey, hash, signature);

            return i++;
        };

        benchmark::DoNotOptimize( func() );
    }
}
BENCHMARK( bm_verify );

BENCHMARK_MAIN();