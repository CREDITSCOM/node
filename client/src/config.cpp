/* Send blaming letters to @yrtimd */
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>

#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/utility/setup/settings_parser.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <base58.h>

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

#include <cscrypto/cscrypto.hpp>
#include "config.hpp"

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

const std::string BLOCK_NAME_PARAMS = "params";
const std::string BLOCK_NAME_SIGNAL_SERVER = "signal_server";
const std::string BLOCK_NAME_HOST_INPUT = "host_input";
const std::string BLOCK_NAME_HOST_OUTPUT = "host_output";
const std::string BLOCK_NAME_HOST_ADDRESS = "host_address";
const std::string BLOCK_NAME_POOL_SYNC = "pool_sync";
const std::string BLOCK_NAME_API = "api";

const std::string PARAM_NAME_NODE_TYPE = "node_type";
const std::string PARAM_NAME_BOOTSTRAP_TYPE = "bootstrap_type";
const std::string PARAM_NAME_HOSTS_FILENAME = "hosts_filename";
const std::string PARAM_NAME_USE_IPV6 = "ipv6";
const std::string PARAM_NAME_MAX_NEIGHBOURS = "max_neighbours";
const std::string PARAM_NAME_CONNECTION_BANDWIDTH = "connection_bandwidth";
const std::string PARAM_NAME_OBSERVER_WAIT_TIME = "observer_wait_time";
const std::string PARAM_NAME_CONVEYER_SEND_CACHE = "conveyer_send_cache_value";

const std::string PARAM_NAME_IP = "ip";
const std::string PARAM_NAME_PORT = "port";

const std::string PARAM_NAME_POOL_SYNC_ONE_REPLY_BLOCK = "one_reply_block";
const std::string PARAM_NAME_POOL_SYNC_FAST_MODE = "fast_mode";
const std::string PARAM_NAME_POOL_SYNC_POOLS_COUNT = "block_pools_count";
const std::string PARAM_NAME_POOL_SYNC_ROUND_COUNT = "request_repeat_round_count";
const std::string PARAM_NAME_POOL_SYNC_PACKET_COUNT = "neighbour_packets_count";
const std::string PARAM_NAME_POOL_SYNC_SEQ_VERIF_FREQ = "sequences_verification_frequency";

const std::string PARAM_NAME_API_PORT = "port";
const std::string PARAM_NAME_AJAX_PORT = "ajax_port";
const std::string PARAM_NAME_EXECUTOR_PORT = "executor_port";
const std::string PARAM_NAME_APIEXEC_PORT = "apiexec_port";
const std::string PARAM_NAME_EXECUTOR_SEND_TIMEOUT = "executor_send_timeout";
const std::string PARAM_NAME_EXECUTOR_RECEIVE_TIMEOUT = "executor_receive_timeout";
const std::string PARAM_NAME_SERVER_SEND_TIMEOUT = "server_send_timeout";
const std::string PARAM_NAME_SERVER_RECEIVE_TIMEOUT = "server_receive_timeout";
const std::string PARAM_NAME_AJAX_SERVER_SEND_TIMEOUT = "ajax_server_send_timeout";
const std::string PARAM_NAME_AJAX_SERVER_RECEIVE_TIMEOUT = "ajax_server_receive_timeout";
const std::string PARAM_NAME_EXECUTOR_IP = "executor_ip";
const std::string PARAM_NAME_EXECUTOR_CMDLINE = "executor_command";

const std::string ARG_NAME_CONFIG_FILE = "config-file";
const std::string ARG_NAME_DB_PATH = "db-path";
const std::string ARG_NAME_PUBLIC_KEY_FILE = "public-key-file";
const std::string ARG_NAME_PRIVATE_KEY_FILE = "private-key-file";
const std::string ARG_NAME_ENCRYPT_KEY_FILE = "encryptkey";
const std::string ARG_NAME_RECREATE_INDEX = "recreate-index";

const std::string PARAM_NAME_ALWAYS_EXECUTE_CONTRACTS = "always_execute_contracts";

const uint32_t MIN_PASSWORD_LENGTH = 3;
const uint32_t MAX_PASSWORD_LENGTH = 128;

const std::map<std::string, NodeType> NODE_TYPES_MAP = {{"client", NodeType::Client}, {"router", NodeType::Router}};
const std::map<std::string, BootstrapType> BOOTSTRAP_TYPES_MAP = {{"signal_server", BootstrapType::SignalServer}, {"list", BootstrapType::IpList}};

static const size_t DEFAULT_NODE_KEY_ID = 0;
static const double kTimeoutSeconds = 5;

static EndpointData readEndpoint(const boost::property_tree::ptree& config, const std::string& propName) {
    const boost::property_tree::ptree& epTree = config.get_child(propName);

    EndpointData result;

    if (epTree.count(PARAM_NAME_IP)) {
        result.ipSpecified = true;
        result.ip = ip::make_address(epTree.get<std::string>(PARAM_NAME_IP));
    }
    else {
        result.ipSpecified = false;
    }

    result.port = epTree.get<Port>(PARAM_NAME_PORT);

    return result;
}

EndpointData EndpointData::fromString(const std::string& str) {
    static std::regex ipv4Regex("^([0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3})\\:([0-9]{1,5})$");
    static std::regex ipv6Regex("^\\[([0-9a-z\\:\\.]+)\\]\\:([0-9]{1,5})$");

    std::smatch match;
    EndpointData result;

    if (std::regex_match(str, match, ipv4Regex)) {
        result.ip = ip::make_address_v4(match[1]);
    }
    else if (std::regex_match(str, match, ipv6Regex)) {
        result.ip = ip::make_address_v6(match[1]);
    }
    else {
        throw std::invalid_argument(str);
    }

    result.port = static_cast<uint16_t>(std::stoul(match[2]));

    return result;
}

template <typename MapType>
typename MapType::mapped_type getFromMap(const std::string& pName, const MapType& map) {
    auto it = map.find(pName);

    if (it != map.end()) {
        return it->second;
    }

    throw boost::property_tree::ptree_bad_data("Bad param value", pName);
}

static inline std::string getArgFromCmdLine(const po::variables_map& vm, const std::string& name, const std::string& defVal) {
    return vm.count(name) ? vm[name].as<std::string>() : defVal;
}

static inline void writeFile(const std::string name, const std::string data) {
    std::ofstream file(name);
    file << data;
    file.close();
}

void Config::dumpJSONKeys(const std::string& fName) const {
    auto sk = privateKey_.access();

    auto pk58 = EncodeBase58(publicKey_.data(), publicKey_.data() + publicKey_.size());
    auto sk58 = EncodeBase58(sk.data(), sk.data() + sk.size());

    std::ofstream f(fName);
    f << "{\"key\":{\"public\":\"" << pk58 << "\",\"private\":\"" << sk58 << "\"}}";

    cscrypto::fillWithZeros(const_cast<char*>(pk58.data()), pk58.size());
    cscrypto::fillWithZeros(const_cast<char*>(sk58.data()), sk58.size());
}

void Config::swap(Config& config) {
    Config temp = std::move(config);
    config = std::move(*this);
    (*this) = std::move(temp);
}

Config Config::read(po::variables_map& vm) {
    Config result = readFromFile(getArgFromCmdLine(vm, ARG_NAME_CONFIG_FILE, DEFAULT_PATH_TO_CONFIG));

    result.recreateIndex_ = vm.count(ARG_NAME_RECREATE_INDEX);
    result.pathToDb_ = getArgFromCmdLine(vm, ARG_NAME_DB_PATH, DEFAULT_PATH_TO_DB);

    return result;
}

#ifndef _WIN32
int getch() {
    int ch;
    struct termios t_old, t_new;

    tcgetattr(STDIN_FILENO, &t_old);
    t_new = t_old;
    t_new.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t_new);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &t_old);
    return ch;
}

static int _getch() {
    return getch();
}

static int _kbhit() {
    static const int STDIN = 0;
    static bool initialized = false;

    if (! initialized) {
        // Use termios to turn off line buffering
        termios term;
        tcgetattr(STDIN, &term);
        term.c_lflag &= ~ICANON;
        tcsetattr(STDIN, TCSANOW, &term);
        setbuf(stdin, NULL);
        initialized = true;
    }

    int bytesWaiting;
    ioctl(STDIN, FIONREAD, &bytesWaiting);
    return bytesWaiting;
}
#endif

template <typename T>
static bool readPasswordFromCin(T& mem) {
    auto ptr = mem.data();
    unsigned char ch = 0;

#ifdef _WIN32
    const char BACKSPACE = 8;
    const char RETURN = 13;

    DWORD con_mode;
    DWORD dwRead;

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    GetConsoleMode(hIn, &con_mode);
    SetConsoleMode(hIn, con_mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));

    while (ReadConsoleA(hIn, &ch, 1, &dwRead, NULL) && ch != RETURN) {
#else
    const char BACKSPACE = 127;
    const char RETURN = 10;

    while ((ch = getch()) != RETURN) {
#endif
        if (ch == BACKSPACE) {
            if (ptr != mem.data()) {
                *(ptr--) = '\0';
                std::cout << "\b \b" << std::flush;
            }
        }
        else if ((size_t)(ptr - mem.data()) < (mem.size() - 1) && ch >= 32 && ch <= 126) {
            *(ptr++) = ch;
            std::cout << '*' << std::flush;
        }
        else
            break;
    }

    *ptr = '\0';
    std::cout << std::endl;

    const auto sz = strlen(mem.data());
    if (!sz)
        return false;

    return ferror(stdin) == 0;
}

static bool encryptWithPassword(const cscrypto::PrivateKey& sk, std::vector<uint8_t>& skBytes) {
    bool pGood = false;
    cscrypto::MemGuard<char, 256> pass;

    while (!pGood) {
        std::cout << "Enter password: " << std::flush;
        if (!readPasswordFromCin(pass)) {
            return false;
        }
        const auto pLen = strlen(pass.data());

        if (pLen < MIN_PASSWORD_LENGTH) {
            std::cout << "Password is too short, minimum length is " << MIN_PASSWORD_LENGTH << std::endl;
            continue;
        }
        else if (pLen > MAX_PASSWORD_LENGTH) {
            std::cout << "Password is too long, maximum length is " << MAX_PASSWORD_LENGTH << std::endl;
            continue;
        }

        std::cout << "Confirm password: " << std::flush;
        cscrypto::MemGuard<char, 256> passConf;
        if (!readPasswordFromCin(passConf))
            return false;

        if (strcmp(pass.data(), passConf.data()) != 0) {
            std::cout << "Passwords do not match" << std::endl;
            continue;
        }

        pGood = true;
    }

    skBytes = sk.getEncrypted(pass.data(), std::strlen(pass.data()));
    return true;
}

static bool getBufFromFile(std::string& buf) {
    std::string pathToFile;
    std::fstream f;
    while (!f.is_open()) {
        std::cout << "\nEnter path to file (will be used as cipher key): " << std::flush;
        std::cin >> pathToFile;
        f.open(pathToFile, std::fstream::binary | std::fstream::in);
        if (!f.is_open()) {
            std::cout << "Can't open file " << pathToFile
                      << ", try another? (y/n): " << std::flush;
            char choice = '\0';
            std::cin >> choice;
            if (choice != 'y') {
                return false;
            }
        }
    }
    std::stringstream ss;
    ss << f.rdbuf();
    buf = ss.str();
    return true;
}

static bool encryptWithFile(const cscrypto::PrivateKey& sk, std::vector<uint8_t>& skBytes) {
    std::string buf;
    if (!getBufFromFile(buf)) {
        return false;
    }
    skBytes = sk.getEncrypted(buf.data(), buf.size());
    return true;
}

static bool usePassword() {
    char choice = '\0';
    while (choice != 'p' && choice != 'f') {
        std::cout << "\nTo use password for encrytion/decryption press \"p\", to use file press \"f\": "
                  << std::flush;
        std::cin >> choice;
        std::cout << std::endl;
    }
    if (choice == 'p') {
        return true;
    }
    return false;
}

static bool getEncryptedPrivateBytes(const cscrypto::PrivateKey& sk, std::vector<uint8_t>& skBytes) {
    if (!std::cin.good()) {
        return false;
    }
    if (usePassword()) {
        return encryptWithPassword(sk, skBytes);
    }
    return encryptWithFile(sk, skBytes);
}

static bool findWordInDictionary(const char* word, size_t& index) {
    using cscrypto::mnemonic::langs::en;
    bool found = false;
    for (size_t i = 0; i < en.size(); ++i) {
        if (std::strcmp(word, en[i]) == 0) {
            found = true;
            index = i;
        }
    }
    return found;
}

bool Config::enterWithSeed() {
    std::cout << "Type seed phrase (24 words devided with spaces):" << std::endl << std::flush;
    std::string seedPhrase;
    std::getline(std::cin, seedPhrase);
    std::stringstream ss(seedPhrase);
    cscrypto::mnemonic::WordList words;
    bool allValid = true;

    for (auto it = words.begin(); it != words.end(); ++it) {
        if (!ss) {
            allValid = false;
            break;
        }
        std::string word;
        ss >> word;
        size_t index;
        if (!findWordInDictionary(word.c_str(), index)) {
            allValid = false;
            break;
        }
        *it = cscrypto::mnemonic::langs::en[index];
    }

    if (!allValid) {
        cslog() << "Invalid seed phrase";
        return false;
    }
    auto ms = cscrypto::mnemonic::wordsToMasterSeed(words);
    auto keys = cscrypto::keys_derivation::deriveKeyPair(ms, DEFAULT_NODE_KEY_ID);
    publicKey_ = keys.first;
    privateKey_ = keys.second;
    return true;
}

void Config::showKeys(const std::string& pk58) {
    double secondsPassed = 0;
    double prevSec = 0;
    std::cout << "To show your keys not encrypted press \"s\"." << std::endl;
    std::cout << "Seconds left:" << std::endl;
    std::clock_t start = std::clock();
    while (secondsPassed < kTimeoutSeconds) {
        secondsPassed = (double)(std::clock() - start) / CLOCKS_PER_SEC;
        if (prevSec < secondsPassed) {
            std::cout << static_cast<int>(kTimeoutSeconds - secondsPassed) << "\r" << std::flush;
            prevSec = secondsPassed + 1;
        }
        if (_kbhit()) {
            if (_getch() == 's') {
                std::cout << "\n\nPress any key to continue...\n" << std::endl;
                auto sk = privateKey_.access();
                std::string sk58tmp = EncodeBase58(sk.data(), sk.data() + sk.size());
                std::cout << "PublicKey: " << pk58 << " PrivateKey: " << sk58tmp << std::flush;
                cscrypto::fillWithZeros(sk58tmp.data(), sk58tmp.size());
                _getch();
                std::cout << "\r" << std::string(cscrypto::kPrivateKeySize * 5, 'x') << std::endl
                          << std::flush;
                break;
            }
        }
    }
}

void Config::changePasswordOption(const std::string& pathToSk) {
    std::cout << "To change password press \"p\".\n" << std::flush;
    std::cout << "Seconds left:" << std::endl;
    double secondsPassed = 0;
    double prevSec = 0;
    std::clock_t start = std::clock();
    while (secondsPassed < kTimeoutSeconds) {
        secondsPassed = (double)(std::clock() - start) / CLOCKS_PER_SEC;
        if (prevSec < secondsPassed) {
            std::cout << static_cast<int>(kTimeoutSeconds - secondsPassed) << "\r" << std::flush;
            prevSec = secondsPassed + 1;
        }
        if (_kbhit()) {
            if (_getch() == 'p') {
                std::cout << "Encrypting the private key file with new password..." << std::endl;
                std::vector<uint8_t> skBytes;
                const bool encSucc = getEncryptedPrivateBytes(privateKey_, skBytes);
                if (encSucc) {
                    std::string sk58tmp = EncodeBase58(skBytes.data(), skBytes.data() + skBytes.size());
                    writeFile(pathToSk, sk58tmp);
                    cscrypto::fillWithZeros(const_cast<char*>(sk58tmp.data()), sk58tmp.size());
                    cslog() << "Key in " << pathToSk << " has been encrypted successfully";
                } else {
                    cslog() << "Not encrypting the private key file due to errors";
                }
                break;
            }
        }
    }
}

/*private*/
bool Config::readKeys(const std::string& pathToPk, const std::string& pathToSk, const bool encrypt) {
    // First read private
    std::ifstream skFile(pathToSk);
    std::string pk58;
    bool callShowKeys = false;

    if (skFile.is_open()) {
        std::string sk58;
        std::vector<uint8_t> sk;

        std::getline(skFile, sk58);
        skFile.close();
        DecodeBase58(sk58, sk);

        bool encFlag = false;
        if (sk.size() < cscrypto::kPrivateKeySize) {
            cserror() << "Bad private key file in " << pathToSk;
            return false;
        }
        else if (sk.size() > cscrypto::kPrivateKeySize) {
            encFlag = true;
            callShowKeys = true;
        }

        if (encFlag) {  // Check the encryption flag
            std::cout << "The key file seems to be encrypted" << std::endl;

            while (!privateKey_) {
                if (!std::cin.good())
                    return false;
                if (usePassword()) {
                    std::cout << "Enter password: " << std::flush;
                    cscrypto::MemGuard<char, 256> pass;
                    if (!readPasswordFromCin(pass)) {
                        return false;
                    }
                    std::cout << "Trying to open file..." << std::endl;
                    privateKey_ = cscrypto::PrivateKey::readFromEncrypted(sk, pass.data(), std::strlen(pass.data()));
                }
                else {
                    std::string buf;
                    if (!getBufFromFile(buf)) {
                        return false;
                    }
                    privateKey_ = cscrypto::PrivateKey::readFromEncrypted(sk, buf.data(), buf.size());
                }

                if (!privateKey_) {
                    std::cout << "Incorrect password (or corrupted file)" << std::endl;
                }
            }
            changePasswordOption(pathToSk);
        }
        else {
            privateKey_ = cscrypto::PrivateKey::readFromBytes(sk);

            if (!privateKey_) {
                cserror() << "Bad private key in " << pathToSk;
                return false;
            }

            cscrypto::fillWithZeros(sk.data(), sk.size());
            cscrypto::fillWithZeros(sk58.data(), sk58.size());

            if (encrypt) {
                callShowKeys = true;
                std::cout << "Encrypting the private key file..." << std::endl;
                std::vector<uint8_t> skBytes;
                const bool encSucc = getEncryptedPrivateBytes(privateKey_, skBytes);
                if (encSucc) {
                    std::string sk58tmp = EncodeBase58(skBytes.data(), skBytes.data() + skBytes.size());
                    writeFile(pathToSk, sk58tmp);
                    cscrypto::fillWithZeros(const_cast<char*>(sk58tmp.data()), sk58tmp.size());
                    cslog() << "Key in " << pathToSk << " has been encrypted successfully";
                }
                else {
                    cslog() << "Not encrypting the private key file due to errors";
                }
            }
        }

        publicKey_ = cscrypto::getMatchingPublic(privateKey_);
        pk58 = EncodeBase58(publicKey_.data(), publicKey_.data() + publicKey_.size());
    }
    else {
        // No private key detected
        for (;;) {
            std::cout << "No suitable keys were found. Type \"g\" to generate or \"q\" to quit." << std::endl;
            char flag;
            std::cin >> flag;

            if (flag == 'g') {
                std::vector<uint8_t> skBytes;
                auto ms = cscrypto::keys_derivation::generateMasterSeed();
                auto keys = cscrypto::keys_derivation::deriveKeyPair(ms, DEFAULT_NODE_KEY_ID);
                privateKey_ = keys.second;
                publicKey_ = keys.first;

                std::cout << "\nSave this phrase to restore your keys in future, and press any key to continue:" << std::endl;
                auto words = cscrypto::mnemonic::masterSeedToWords(ms);
                for (auto w : words) {
                    std::cout << w << " ";
                }
                std::cout << std::flush;
                _getch();
                std::cout << "\r" << std::string(ms.size() * 10, 'x') << std::endl << std::flush;

                std::cout << "Choose your private key file encryption type (enter number):" << std::endl;
                std::cout << "[1] Encrypt with password (recommended)" << std::endl;
                std::cout << "[2] No encryption" << std::endl;
                std::cout << "[q] Quit" << std::endl;

                char sChoice = '\0';
                while (sChoice != '1' && sChoice != '2' && sChoice != 'q') {
                    std::cout << "Enter choice: " << std::flush;
                    std::cin >> sChoice;
                    if (!std::cin.good())
                        return false;
                }

                if (sChoice == 'q')
                    return false;
                else if (sChoice == '1') {
                    callShowKeys = true;
                    if (!getEncryptedPrivateBytes(privateKey_, skBytes))
                        return false;
                }
                else {
                    auto sk = privateKey_.access();
                    skBytes.assign(sk.data(), sk.data() + sk.size());
                }

                pk58 = EncodeBase58(publicKey_.data(), publicKey_.data() + publicKey_.size());
                std::string sk58 = EncodeBase58(skBytes.data(), skBytes.data() + skBytes.size());

                writeFile(pathToPk, pk58);
                writeFile(pathToSk, sk58);

                // Just in case...
                cscrypto::fillWithZeros(const_cast<char*>(sk58.data()), sk58.size());
                cscrypto::fillWithZeros(const_cast<uint8_t*>(skBytes.data()), skBytes.size());

                cslog() << "Keys generated";
                break;
            }
            else if (flag == 'q')
                return false;
        }
    }

    // Okay so by now we have both public and private key fields filled up
    std::ifstream pkFile(pathToPk);
    bool pkGood = false;

    if (pkFile.is_open()) {
        std::string pkFileCont;
        std::getline(pkFile, pkFileCont);
        pkFile.close();

        pkGood = (pkFileCont == pk58);
    }

    if (!pkGood) {
        std::cout << "The PUBLIC key file not found or doesn't contain a valid key (matching the provided private key). Type \"f\" to rewrite the PUBLIC key file." << std::endl;
        char flag;
        std::cin >> flag;
        if (flag == 'f')
            writeFile(pathToPk, pk58);
        else
            return false;
    }

    if (callShowKeys) {
        showKeys(pk58);
    }

    return true;
}

/*public*/
bool Config::readKeys(const po::variables_map& vm) {
    return readKeys(getArgFromCmdLine(vm, ARG_NAME_PUBLIC_KEY_FILE, DEFAULT_PATH_TO_PUBLIC_KEY),
        getArgFromCmdLine(vm, ARG_NAME_PRIVATE_KEY_FILE, DEFAULT_PATH_TO_PRIVATE_KEY), vm.count(ARG_NAME_ENCRYPT_KEY_FILE));
}

Config Config::readFromFile(const std::string& fileName) {
    Config result;

    boost::property_tree::ptree config;

    try {
        auto ext = boost::filesystem::extension(fileName);
        boost::algorithm::to_lower(ext);
        if (ext == "json") {
            boost::property_tree::read_json(fileName, config);
        }
        else if (ext == "xml") {
            boost::property_tree::read_xml(fileName, config);
        }
        else {
            boost::property_tree::read_ini(fileName, config);
        }

        result.inputEp_ = readEndpoint(config, BLOCK_NAME_HOST_INPUT);

        if (config.count(BLOCK_NAME_HOST_OUTPUT)) {
            result.outputEp_ = readEndpoint(config, BLOCK_NAME_HOST_OUTPUT);

            result.twoSockets_ = true; /*(result.outputEp_.ip != result.inputEp_.ip ||
                                         result.outputEp_.port != result.inputEp_.port);*/
        }
        else {
            result.twoSockets_ = false;
        }

        const boost::property_tree::ptree& params = config.get_child(BLOCK_NAME_PARAMS);

        result.ipv6_ = !(params.count(PARAM_NAME_USE_IPV6) && params.get<std::string>(PARAM_NAME_USE_IPV6) == "false");

        result.maxNeighbours_ = params.count(PARAM_NAME_MAX_NEIGHBOURS) ? params.get<uint32_t>(PARAM_NAME_MAX_NEIGHBOURS) : DEFAULT_MAX_NEIGHBOURS;
        if (result.maxNeighbours_ > DEFAULT_MAX_NEIGHBOURS) {
            result.maxNeighbours_ = DEFAULT_MAX_NEIGHBOURS; // see neighbourhood.hpp, some containers are of static size
        }

        result.connectionBandwidth_ = params.count(PARAM_NAME_CONNECTION_BANDWIDTH) ? params.get<uint64_t>(PARAM_NAME_CONNECTION_BANDWIDTH) : DEFAULT_CONNECTION_BANDWIDTH;
        result.observerWaitTime_ = params.count(PARAM_NAME_OBSERVER_WAIT_TIME) ? params.get<uint64_t>(PARAM_NAME_OBSERVER_WAIT_TIME) : DEFAULT_OBSERVER_WAIT_TIME;
        result.conveyerSendCacheValue_ = params.count(PARAM_NAME_CONVEYER_SEND_CACHE) ? params.get<size_t>(PARAM_NAME_CONVEYER_SEND_CACHE) : DEFAULT_CONVEYER_SEND_CACHE_VALUE;

        result.nType_ = getFromMap(params.get<std::string>(PARAM_NAME_NODE_TYPE), NODE_TYPES_MAP);

        if (config.count(BLOCK_NAME_HOST_ADDRESS)) {
            result.hostAddressEp_ = readEndpoint(config, BLOCK_NAME_HOST_ADDRESS);
            result.symmetric_ = false;
        }
        else {
            result.symmetric_ = true;
        }

        result.bType_ = getFromMap(params.get<std::string>(PARAM_NAME_BOOTSTRAP_TYPE), BOOTSTRAP_TYPES_MAP);

        if (result.bType_ == BootstrapType::SignalServer || result.nType_ == NodeType::Router) {
            result.signalServerEp_ = readEndpoint(config, BLOCK_NAME_SIGNAL_SERVER);
        }

        if (result.bType_ == BootstrapType::IpList) {
            const auto hostsFileName = params.get<std::string>(PARAM_NAME_HOSTS_FILENAME);

            std::string line;

            std::ifstream hostsFile;
            hostsFile.exceptions(std::ifstream::failbit);
            hostsFile.open(hostsFileName);
            hostsFile.exceptions(std::ifstream::goodbit);

            while (getline(hostsFile, line)) {
                if (!line.empty()) {
                    result.bList_.push_back(EndpointData::fromString(line));
                }
            }

            if (result.bList_.empty()) {
                throw std::length_error("No hosts specified");
            }
        }

        if (params.count(PARAM_NAME_ALWAYS_EXECUTE_CONTRACTS) > 0) {
            result.alwaysExecuteContracts_ = params.get<bool>(PARAM_NAME_ALWAYS_EXECUTE_CONTRACTS);
        }

        result.setLoggerSettings(config);
        result.readPoolSynchronizerData(config);
        result.readApiData(config);
        result.good_ = true;
    }
    catch (boost::property_tree::ini_parser_error& e) {
        cserror() << "Couldn't read config file \"" << fileName << "\": " << e.what();
        result.good_ = false;
    }
    catch (boost::property_tree::ptree_bad_data& e) {
        cserror() << e.what() << ": " << e.data<std::string>();
    }
    catch (boost::property_tree::ptree_error& e) {
        cserror() << "Errors in config file: " << e.what();
    }
    catch (std::invalid_argument& e) {
        cserror() << "Parsing error at \"" << e.what() << "\".";
    }
    catch (std::ifstream::failure& e) {
        cserror() << "Cannot open file: " << e.what();
    }
    catch (std::exception& e) {
        cserror() << e.what();
    }
    catch (...) {
        cserror() << "Errors in config file";
    }

    return result;
}

void Config::setLoggerSettings(const boost::property_tree::ptree& config) {
    boost::property_tree::ptree settings;
    auto core = config.get_child_optional("Core");
    if (core) {
        settings.add_child("Core", *core);
    }
    auto sinks = config.get_child_optional("Sinks");
    if (sinks) {
        for (const auto& val : *sinks) {
            settings.add_child(boost::property_tree::ptree::path_type("Sinks." + val.first, '/'), val.second);
        }
    }
    for (const auto& item : config) {
        if (item.first.find("Sinks.") == 0)
            settings.add_child(boost::property_tree::ptree::path_type(item.first, '/'), item.second);
    }
    std::stringstream ss;
    boost::property_tree::write_ini(ss, settings);
    loggerSettings_ = boost::log::parse_settings(ss);
}

void Config::readPoolSynchronizerData(const boost::property_tree::ptree& config) {
    const std::string& block = BLOCK_NAME_POOL_SYNC;

    if (!config.count(block)) {
        return;
    }

    const boost::property_tree::ptree& data = config.get_child(block);

    checkAndSaveValue(data, block, PARAM_NAME_POOL_SYNC_ONE_REPLY_BLOCK, poolSyncData_.oneReplyBlock);
    checkAndSaveValue(data, block, PARAM_NAME_POOL_SYNC_FAST_MODE, poolSyncData_.isFastMode);
    checkAndSaveValue(data, block, PARAM_NAME_POOL_SYNC_POOLS_COUNT, poolSyncData_.blockPoolsCount);
    checkAndSaveValue(data, block, PARAM_NAME_POOL_SYNC_ROUND_COUNT, poolSyncData_.requestRepeatRoundCount);
    checkAndSaveValue(data, block, PARAM_NAME_POOL_SYNC_PACKET_COUNT, poolSyncData_.neighbourPacketsCount);
    checkAndSaveValue(data, block, PARAM_NAME_POOL_SYNC_SEQ_VERIF_FREQ, poolSyncData_.sequencesVerificationFrequency);
}

void Config::readApiData(const boost::property_tree::ptree& config) {
    if (!config.count(BLOCK_NAME_API)) {
        return;
    }

    const boost::property_tree::ptree& data = config.get_child(BLOCK_NAME_API);

    checkAndSaveValue(data, BLOCK_NAME_API, PARAM_NAME_API_PORT, apiData_.port);
    checkAndSaveValue(data, BLOCK_NAME_API, PARAM_NAME_AJAX_PORT, apiData_.ajaxPort);
    checkAndSaveValue(data, BLOCK_NAME_API, PARAM_NAME_EXECUTOR_PORT, apiData_.executorPort);
    checkAndSaveValue(data, BLOCK_NAME_API, PARAM_NAME_EXECUTOR_SEND_TIMEOUT, apiData_.executorSendTimeout);
    checkAndSaveValue(data, BLOCK_NAME_API, PARAM_NAME_EXECUTOR_RECEIVE_TIMEOUT, apiData_.executorReceiveTimeout);
    checkAndSaveValue(data, BLOCK_NAME_API, PARAM_NAME_SERVER_SEND_TIMEOUT, apiData_.serverSendTimeout);
    checkAndSaveValue(data, BLOCK_NAME_API, PARAM_NAME_SERVER_RECEIVE_TIMEOUT, apiData_.serverReceiveTimeout);
    checkAndSaveValue(data, BLOCK_NAME_API, PARAM_NAME_AJAX_SERVER_SEND_TIMEOUT, apiData_.ajaxServerSendTimeout);
    checkAndSaveValue(data, BLOCK_NAME_API, PARAM_NAME_AJAX_SERVER_RECEIVE_TIMEOUT, apiData_.ajaxServerReceiveTimeout);
    checkAndSaveValue(data, BLOCK_NAME_API, PARAM_NAME_APIEXEC_PORT, apiData_.apiexecPort);

    if (data.count(PARAM_NAME_EXECUTOR_IP) > 0) {
        apiData_.executorHost = data.get<std::string>(PARAM_NAME_EXECUTOR_IP);
    }
    if (data.count(PARAM_NAME_EXECUTOR_CMDLINE) > 0) {
        apiData_.executorCmdLine = data.get<std::string>(PARAM_NAME_EXECUTOR_CMDLINE);
    }
}

template <typename T>
bool Config::checkAndSaveValue(const boost::property_tree::ptree& data, const std::string& block, const std::string& param, T& value) {
    if (data.count(param)) {
        const int readValue = std::is_same_v<T, bool> ? data.get<bool>(param) : data.get<int>(param);
        const auto max = cs::getMax(value);
        const auto min = cs::getMin(value);

        if (readValue > max || readValue < min) {
            std::cout << "[warning] Config.ini> Please, check the block: [" << block << "], so that param: [" << param << "],  will be: [" << cs::numeric_cast<int>(min) << ", "
                      << cs::numeric_cast<int>(max) << "]" << std::endl;
            return false;
        }

        value = cs::numeric_cast<T>(readValue);
        return true;
    }

    return false;
}

bool operator==(const EndpointData& lhs, const EndpointData& rhs) {
#if !defined(BOOST_ASIO_NO_DEPRECATED)
    boost::system::error_code ec;
    auto result = lhs.ip.to_string(ec) == rhs.ip.to_string(ec);
#else
    auto result = lhs.ip.to_string() == rhs.ip.to_string();
#endif
    return result &&
           lhs.port == rhs.port &&
           lhs.ipSpecified == rhs.ipSpecified;
}

bool operator!=(const EndpointData& lhs, const EndpointData& rhs) {
    return !(lhs == rhs);
}

bool operator==(const PoolSyncData& lhs, const PoolSyncData& rhs) {
    return lhs.oneReplyBlock == rhs.oneReplyBlock &&
           lhs.isFastMode == rhs.isFastMode &&
           lhs.blockPoolsCount == rhs.blockPoolsCount &&
           lhs.requestRepeatRoundCount && rhs.requestRepeatRoundCount &&
           lhs.neighbourPacketsCount && rhs.neighbourPacketsCount &&
           lhs.sequencesVerificationFrequency && rhs.sequencesVerificationFrequency;
}

bool operator!=(const PoolSyncData& lhs, const PoolSyncData& rhs) {
    return !(lhs == rhs);
}

bool operator==(const ApiData& lhs, const ApiData& rhs) {
    return lhs.port == rhs.port &&
           lhs.ajaxPort == rhs.ajaxPort &&
           lhs.executorPort == rhs.executorPort &&
           lhs.apiexecPort == rhs.apiexecPort &&
           lhs.executorSendTimeout == rhs.executorSendTimeout &&
           lhs.executorReceiveTimeout == rhs.executorReceiveTimeout &&
           lhs.serverSendTimeout == rhs.serverSendTimeout &&
           lhs.serverReceiveTimeout == rhs.serverReceiveTimeout &&
           lhs.ajaxServerSendTimeout == rhs.ajaxServerSendTimeout &&
           lhs.ajaxServerReceiveTimeout == rhs.ajaxServerReceiveTimeout &&
           lhs.executorHost == rhs.executorHost &&
           lhs.executorCmdLine == rhs.executorCmdLine;
}

bool operator!=(const ApiData& lhs, const ApiData& rhs) {
    return !(lhs == rhs);
}

// logger settings not checked
bool operator==(const Config& lhs, const Config& rhs) {
    return lhs.good_ == rhs.good_ &&
           lhs.inputEp_ == rhs.inputEp_ &&
           lhs.twoSockets_ == rhs.twoSockets_ &&
           lhs.outputEp_ == rhs.outputEp_ &&
           lhs.nType_ == rhs.nType_ &&
           lhs.ipv6_ == rhs.ipv6_ &&
           lhs.maxNeighbours_ == rhs.maxNeighbours_ &&
           lhs.connectionBandwidth_ == rhs.connectionBandwidth_ &&
           lhs.symmetric_ == rhs.symmetric_ &&
           lhs.hostAddressEp_ == rhs.hostAddressEp_ &&
           lhs.bType_ == rhs.bType_ &&
           lhs.signalServerEp_ == rhs.signalServerEp_ &&
           lhs.bList_ == rhs.bList_ &&
           lhs.pathToDb_ == rhs.pathToDb_ &&
           lhs.publicKey_ == rhs.publicKey_ &&
           lhs.privateKey_ == rhs.privateKey_ &&
           lhs.poolSyncData_ == rhs.poolSyncData_ &&
           lhs.apiData_ == rhs.apiData_ &&
           lhs.alwaysExecuteContracts_ == rhs.alwaysExecuteContracts_ &&
           lhs.recreateIndex_ == rhs.recreateIndex_ &&
           lhs.observerWaitTime_ == rhs.observerWaitTime_ &&
           lhs.conveyerSendCacheValue_ == rhs.conveyerSendCacheValue_;
}

bool operator!=(const Config& lhs, const Config& rhs) {
    return !(lhs == rhs);
}
