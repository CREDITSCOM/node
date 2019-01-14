If you have checked out for this branch from another one with different libsodium
and berkeleydb_prebuilt submodules do the following for correct work:

rm -rf third-party/berkeleydb_prebuilt (rmdir third-party/berkeleydb_prebuilt /s /q)
rm -rf third-party/libsodium (rmdir third-party/libsodium /s /q)
git submodule update --init
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release (or -DCMAKE_BUILD_TYPE=Debug) (for Windows -A x64) ..

During cmake execution libsodium and berkeleydb libs will be built and until cmake finish it's job
your working directory will be dirty.