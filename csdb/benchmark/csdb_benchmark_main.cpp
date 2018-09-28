#include <benchmark/benchmark.h>

static void bench(benchmark::State &state)
{
  for(auto _ : state)
  {
  }
}
BENCHMARK(bench);

BENCHMARK_MAIN();
