#include <lib/system/logger.hpp>

#include <iostream>

#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/value_extraction.hpp>
#include <boost/log/core.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/filter_parser.hpp>
#include <boost/log/utility/setup/formatter_parser.hpp>
#include <boost/log/utility/setup/from_settings.hpp>

namespace logger {
namespace {
void initCommon(const logging::settings& settings) {
    logging::add_common_attributes();

    logging::core::get()->add_global_attribute(
        "Channel", logging::attributes::constant<std::string>("default"));

    logging::register_simple_formatter_factory<severity_level, char>(logging::trivial::tag::severity::get_name());
    logging::register_simple_formatter_factory<std::string, char>("Channel");
    logging::register_simple_filter_factory<severity_level>(logging::trivial::tag::severity::get_name());
    logging::register_simple_filter_factory<std::string>("Channel");

    try {
        logging::init_from_settings(settings);
    } catch (const std::exception& e) {
        std::cerr << "[logger] init_from_settings threw: " << e.what() << std::endl;
        std::cerr << "[logger] (log filter / format parsing failed; node will exit)" << std::endl;
        throw;
    } catch (...) {
        std::cerr << "[logger] init_from_settings threw an unknown exception" << std::endl;
        throw;
    }
}
}  // namespace

void initialize(const logging::settings& settings) {
    initCommon(settings);
}

void initialize(const logging::settings& settings,
                const std::map<std::string, severity_level>& channelLevels) {
    initCommon(settings);
    if (channelLevels.empty()) {
        return;
    }

    severity_level defaultLevel = logging::trivial::info;
    if (auto it = channelLevels.find("default"); it != channelLevels.end()) {
        defaultLevel = it->second;
    }

    logging::core::get()->set_filter(
        [levels = channelLevels, defaultLevel](const logging::attribute_value_set& attrs) {
            severity_level sev = logging::trivial::info;
            auto sevAttr = attrs[logging::trivial::severity];
            if (sevAttr) {
                sev = sevAttr.get();
            }

            std::string channel = "default";
            auto chAttr = attrs["Channel"].extract<std::string>();
            if (chAttr) {
                channel = chAttr.get();
            }

            auto it = levels.find(channel);
            const severity_level threshold = (it != levels.end()) ? it->second : defaultLevel;
            return sev >= threshold;
        });
}

void cleanup() {
    logging::core::get()->remove_all_sinks();
}
}  // namespace logger
