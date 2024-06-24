<img src="https://raw.githubusercontent.com/CREDITSCOM/Documentation/master/Src/Logo_Credits_horizontal_black.png" align="center">

[Documentation](https://developers.credits.com/en/Articles/Platform) \|
[Guides](https://developers.credits.com/en/Articles/Guides) \|
[News](https://credits.com/en/Home/News)

[![Twitter](https://img.shields.io/twitter/follow/creditscom.svg?label=Follow&style=social)](https://twitter.com/intent/follow?screen_name=creditscom)
[![AGPL License](https://img.shields.io/github/license/CREDITSCOM/node.svg?color=green&label=License&style=plastic)](LICENSE)
![Version](https://img.shields.io/github/tag/CREDITSCOM/node.svg?label=Version&style=plastic)
[![Build Status](http://89.111.33.166:8080/buildStatus/icon?job=lin-client-node&build=27)](http://89.111.33.166:8080/view/release-pipeline/job/lin-client-node/lastBuild/)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=CREDITSCOM_node&metric=alert_status)](https://sonarcloud.io/dashboard?id=CREDITSCOM_node)
# Credits Node
A node is a client-side application that is installed on the user equipment.
The Node processes and stores transactions, executes and confirms smart contract rules requests processing from third-party systems and provides data when requested.
Written on C++.

## Version
Current node version 4.2.532.0

<h2>What is Credits Node?</h2>
<p>Credits Node is the main module that provide an opportunity to run a node and participate in CREDITS blockhain network. The node performs processing and storage of transactions, execution and confirmation of the terms of smart contracts, processing requests from third-party systems, and provides data upon request. Each node on the network has the same functionality.
The node consists of the following elements:</p>
<ul>
<li>API</li> 
<li>Desision-making module(Solver)</li> 
<li>Storage(CSDB)</li> 
<li>Transport protocol</li> 
</ul>

<h2>Build dependencies</h2>
<ul>
<li><a href="https://www.boost.org/users/history/version_1_70_0.html">Boost 1.70</a> or newest static prebuild</li>
<li>Compiler with C++17 support</li>
<li><a href="https://cmake.org/download/">Cmake 3.11</a> or newest</li>
<li> Requirements fo building <a href="https://thrift.apache.org/docs/install/">Apache Thrift</a></li>
<li>The building Berkeley DB distribution uses the Free Software Foundation's autoconf and libtool tools to build on UNIX platforms.</li>

On Windows:<br/>

It is necessary to run in the terminal, which sets the environment variables for building a Visual Studio project

>```sh
>git clone https://github.com/CREDITSCOM/node.git
>cd node
>git submodule update --init --recursive
>mkdir build
>cd build
>cmake -DCMAKE_BUILD_TYPE=Release -A x64 ..
>cmake --build . --target ALL_BUILD --config Release
On Linux:<br/>
>```sh
>git clone https://github.com/CREDITSCOM/node.git
>cd node
>git submodule update --init --recursive
>mkdir build
>cd build
>cmake -DCMAKE_BUILD_TYPE=Release ..
>make -j4

<h4>If necessary, full step-by-step instruction for Linux:</h4>

Update repos and install required package
>```sh
>sudo apt update
>apt upgrade -y
>sudo apt install build-essential libssl-dev autoconf -y
>sudo apt install flex bison libtool -y

Create build directory
>```sh
>mkdir csbuild
>d csbuild

Update gcc g++ (there is no need to update in version of Ubuntu 20 and later)
>```sh
>sudo apt install gcc-8 g++-8 -y
>sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-8 80 --slave /usr/bin/g++ g++ /usr/bin/g++-8 --slave /usr/bin/gcov gcov /usr/bin/gcov-8
>gcc --version
>gcc (Ubuntu 8.4.0-1ubuntu1~18.04) 8.4.0
>Copyright (C) 2018 Free Software Foundation, Inc.
>This is free software; see the source for copying conditions.  There is NO
>warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE

Update cmake to latest version
>```sh
>wget -c https://github.com/Kitware/CMake/releases/download/v3.17.0/cmake-3.17.0.tar.gz
>tar zxfv cmake-3.17.0.tar.gz
>cd cmake-3.17.0
>./bootstrap
>make
>sudo make install
>cd ..

Install boost
>```sh
>wget -c $https://dl.bintray.com/boostorg/release/1.72.0/source/boost_1_72_0.tar.gz
>tar zxfv boost_1_72_0.tar.gz
>cd boost_1_72_0
>./bootstrap.sh
>./b2 --build-type=complete link=static threading=multi $runtime-link=static --layout=tagged install --prefix=../boost
>export PATH=~/csbuild/boost:$PATH
>export BOOST_ROOT=~/csbuild/boost
>cd ..

Build node
>```sh
>git clone https://github.com/CREDITSCOM/node.git
>cd node
>git submodule update --init --recursive
>mkdir build
>cd build
>cmake -DCMAKE_BUILD_TYPE=Release ..
>make -j2

<h2>System requirements:</h2>
<h4>Minimum system requirements:</h4>
Operating system: Windows® 7 / Windows® 8 / Windows® 10 64-bit (with the last update package)
Processor (CPU): with frequency of 1 GHz (or faster) with PAE, NX and SSE2 support;
Memory (RAM): 8 Gb
HDD: 600Gb
Internet connection: 3 Mbit/s.
<h4>Recommended system requirements:</h4>

Operating system: Windows® 7 / Windows® 8 / Windows® 10 64-bit (with the last update package)
Processor (CPU): Intel® Core ™ i3 or AMD Phenom ™ X3 8650
Memory (RAM): 8 Gb
HDD: 3 Tb
Internet connection: 5 Mbit/s.

<h2>Contribution</h2>
<p>Thank you for considering to help out with the source code! We welcome contributions from anyone on the internet, and are grateful for even the smallest of fixes!
If you'd like to contribute to Credits Node, please fork, fix, commit and send a pull request for the maintainers to review and merge into the main code base. If you wish to submit more complex changes though, please check up with the core devs first on our <a href="https://developers.credits.com/">Developers portal</a> and <a href="https://github.com/CREDITSCOM/Documentation/blob/master/Contribution.md"> Contribution file</a> to ensure those changes are in line with the general philosophy of the project and/or get some early feedback which can make both your efforts much lighter as well as our review and merge procedures quick and simple.
Please make sure your contributions adhere to our coding guidelines:</p>
<ul>
<li>Code must adhere to the <a href="https://github.com/CREDITSCOM/node/blob/master/codingstyle.md">Credits coding style</a></li>

<li>Pull requests need to be based on and opened against the master branch</li>
<li>Commit messages should be prefixed with the package(s) they modify</li>
</ul>
<h3>Resources</h3>

<a href="https://credits.com//">CREDITS Website</a>

<a href="https://github.com/CREDITSCOM/DOCUMENTATION">Documentation</a>

<a href="https://github.com/CREDITSCOM/Documentation/blob/master/WhitePaper%20CREDITS%20Eng.pdf">Whitepaper</a>

<a href="https://github.com/CREDITSCOM/Documentation/blob/master/TechnicalPaper%20CREDITS%20Eng.pdf">Technical paper</a>

<a href="https://developers.credits.com/">Developers portal</a>

<h3>Community links</h3>
   <a href="https://t.me/creditstechnical"><<img src ="https://simpleicons.org/icons/telegram.svg" height=40 widht=40 ></a>
   <a href="https://t.me/creditscom"><img src ="https://simpleicons.org/icons/telegram.svg" height=40 widht=40 ></a>
   <a href="https://twitter.com/creditscom"><img src ="https://simpleicons.org/icons/twitter.svg" height=40 widht=40 ></a>
   <a href="https://www.reddit.com/r/CreditsOfficial/"><img src ="https://simpleicons.org/icons/reddit.svg" height=40 widht=40></a> 
   <a href="https://medium.com/@credits"><img src="https://simpleicons.org/icons/medium.svg" height=40 widht=40></a>
   <a href="https://www.instagram.com/credits_com/"><img src="https://simpleicons.org/icons/facebook.svg" height=40 widht=40></a>
   <a href="https://www.facebook.com/creditscom"><img src="https://simpleicons.org/icons/instagram.svg" height=40 widht=40></a>
   <a href="https://www.youtube.com/channel/UC7kjX_jgauCqmf_a4fqLGOQ"><img src="https://simpleicons.org/icons/youtube.svg" height=40 widht=40></a>
