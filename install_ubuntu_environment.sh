#!/bin/bash

# install Cmake
git clone https://gitlab.kitware.com/cmake/cmake.git
cd cmake
./bootstrap --prefix=/usr/local
make -j4
cd..

#install boost_1_68_0
wget https://dl.bintray.com/boostorg/release/1.68.0/source/boost_1_68_0.tar.gz
tar xsvf boost_1_68_0.tar.gz 
cd boost_1_68_0/
sudo apt-get update
sudo apt-get install g++ python-dev autotools-dev libicu-dev build-essential libbz2-dev
./bootstrap.sh --prefix=/usr/local
sudo ./b2 -j4 install
cd ..

#install flex and bison
sudo apt-get install flex bison

#install autoconf and automake
sudo apt-get install autoconf automake
