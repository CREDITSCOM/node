#pragma once

#include <boost/asio.hpp>

struct EndpointData final {
	EndpointData()
		: ipSpecified(false)
		, port(0)
	{}
	bool ipSpecified;
	short unsigned port;
	boost::asio::ip::address ip;

	static EndpointData fromString(const std::string&);
};