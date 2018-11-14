#include "stdafx.h"

#include "EndpointData.h"

EndpointData EndpointData::fromString(const std::string& str) {
	static std::regex ipv4Regex("^([0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3})\\:([0-9]{1,5})$");
	static std::regex ipv6Regex("^\\[([0-9a-z\\:\\.]+)\\]\\:([0-9]{1,5})$");

	std::smatch match;
	EndpointData result;

	if (std::regex_match(str, match, ipv4Regex))
		result.ip = boost::asio::ip::make_address_v4(match[1]);
	else if (std::regex_match(str, match, ipv6Regex))
		result.ip = boost::asio::ip::make_address_v6(match[1]);
	else
		throw std::invalid_argument(str);

	result.port = std::stoul(match[2]);

	return result;
}