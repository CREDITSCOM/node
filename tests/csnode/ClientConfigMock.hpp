//
// Created by User on 01.10.2018.
//

#ifndef PROJECT_MOCKCLIENTCONFIG_HPP
#define PROJECT_MOCKCLIENTCONFIG_HPP

#include <gmock/gmock.h>
#include <vector>
#include <string>
#include "config.hpp"

class MockConfig : public Config {
public:
  MOCK_CONST_METHOD0(getInputEndpoint, const EndpointData&());
  MOCK_CONST_METHOD0(getOutputEndpoint, const EndpointData&());

  MOCK_CONST_METHOD0(getSignalServerEndpoint, const EndpointData&());

  MOCK_CONST_METHOD0(getBootstrapType, BootstrapType());
  MOCK_CONST_METHOD0(getNodeType, NodeType());
  MOCK_CONST_METHOD0(getIpList, const std::vector<EndpointData>&());

  MOCK_CONST_METHOD0(getMyPublicKey, const PublicKey&());
  MOCK_CONST_METHOD0(getPathToDB, const std::string&());

  MOCK_METHOD0(isGood, bool());

  MOCK_CONST_METHOD0(useIPv6, bool());
  MOCK_CONST_METHOD0(hasTwoSockets, bool());

  MOCK_CONST_METHOD0(isSymmetric, bool());
  MOCK_CONST_METHOD0(getAddressEndpoint, const EndpointData&());


};

#endif //PROJECT_MOCKCLIENTCONFIG_HPP
