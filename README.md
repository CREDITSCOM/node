<img src="https://raw.githubusercontent.com/CREDITSCOM/Documentation/master/Src/Logo_Credits_horizontal_black.png" align="center">

[Documentation](https://developers.credits.com/en/Articles/Platform) \|
[Guides](https://developers.credits.com/en/Articles/Guides) \|
[News](https://credits.com/en/Home/News)

[![Twitter](https://img.shields.io/twitter/follow/creditscom.svg?label=Follow&style=social)](https://twitter.com/intent/follow?screen_name=creditscom)
[![AGPL License](https://img.shields.io/github/license/CREDITSCOM/node.svg?color=green&label=License&style=plastic)](LICENSE)
![Version](https://img.shields.io/github/tag/CREDITSCOM/node.svg?label=Versoin&style=plastic)
[![Build Status](https://161.156.96.18:8443/buildStatus/icon?job=client_linux_x64&lastBuild)](https://161.156.96.18:8443/job/client_linux_x64/lastBuild/)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=CREDITSCOM_node&metric=alert_status)](https://sonarcloud.io/dashboard?id=CREDITSCOM_node)
# Credits Node
A node is a client-side application that is installed on the user equipment.
The Node processes and stores transactions, executes and confirms smart contract rules requests processing from third-party systems and provides data when requested.
Written on C++.

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
<li><a href="https://www.boost.org/users/history/version_1_68_0.html">Boost 1.68</a> or newest static prebuild</li>
<li>Compiler with C++17 support</li>
<li><a href="https://cmake.org/download/">Cmake 3.11</a> or newest</li>
<li> Requirements fo building <a href="https://thrift.apache.org/docs/install/">Apache Thrift</a></li>
<li><a href="https://github.com/jedisct1/libsodium">libsodium</a></li>
<li>The building Berkeley DB distribution uses the Free Software Foundation's autoconf and libtool tools to build on UNIX platforms.</li>
   
</ul>

<h2>How to Build</h2>

On Windows<br/>

It is necessary to run in the terminal, which sets the environment variables for building a Visual Studio project

>```sh
>git clone https://github.com/CREDITSCOM/node.git
>cd node
>git submodule update --init --recursive
>mkdir build
>cd build
>cmake -DCMAKE_BUILD_TYPE=Release -A x64 ..
>cmake --build . --target ALL_BUILD --config Release

On Linux<br/>

>```sh
>git clone https://github.com/CREDITSCOM/node.git
>cd node
>git submodule update --init --recursive
>mkdir build
>cd build
>cmake -DCMAKE_BUILD_TYPE=Release ..
>make -j4

<h2>System requirements:</h2>
<h4>Minimum system requirements:</h4>
Operating system: Windows® 7 / Windows® 8 / Windows® 10 64-bit (with the last update package)
Processor (CPU): with frequency of 1 GHz (or faster) with PAE, NX and SSE2 support;
Memory (RAM): 4 Gb
HDD: 1 Tb
Internet connection: 3 Mbit/s
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
<li>Code must adhere to the <a href="https://google.github.io/styleguide/cppguide.html">Credits coding style</a></li>

<li>Pull requests need to be based on and opened against the master branch</li>
<li>Commit messages should be prefixed with the package(s) they modify</li>
</ul>
<h3>Resources</h3>

<a href="https://credits.com//">CREDITS Website</a>

<a href="https://github.com/CREDITSCOM/DOCUMENTATION">Documentation</a>

<a href="https://credits.com/Content/Docs/TechnicalWhitePaperCREDITSEng.pdf">Whitepaper</a>

<a href="https://credits.com/Content/Docs/TechnicalPaperENG.pdf">Technical paper</a>

<a href="https://developers.credits.com/">Developers portal</a>

<a href="http://forum.credits.com/">Credits forum</a>
<h3>Community links</h3>
   <a href="https://t.me/creditscom"><img src ="https://simpleicons.org/icons/telegram.svg" height=40 widht=40 ></a>
   <a href="https://twitter.com/creditscom"><img src ="https://simpleicons.org/icons/twitter.svg" height=40 widht=40 ></a>
   <a href="https://www.reddit.com/r/CreditsOfficial/"><img src ="https://simpleicons.org/icons/reddit.svg" height=40 widht=40></a> 
   <a href="https://medium.com/@credits"><img src="https://simpleicons.org/icons/medium.svg" height=40 widht=40></a>
   <a href="https://www.instagram.com/credits_com/"><img src="https://simpleicons.org/icons/facebook.svg" height=40 widht=40></a>
   <a href="https://www.facebook.com/creditscom"><img src="https://simpleicons.org/icons/instagram.svg" height=40 widht=40></a>
   <a href="https://www.youtube.com/channel/UC7kjX_jgauCqmf_a4fqLGOQ"><img src="https://simpleicons.org/icons/youtube.svg" height=40 widht=40></a>