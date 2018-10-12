#include <lib/system/logger.hpp>

#include <boost/log/core.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/filter_parser.hpp>
#include <boost/log/utility/setup/from_settings.hpp>

//TODO: legacy, remove it. this flag can be emulated via attribute (eg. SkipLog) that can be processed in filter
thread_local bool trace = true;

namespace logger {
  void initialize(const logging::settings& settings) {
    logging::add_common_attributes();
    logging::core::get()->add_thread_attribute("Scope", logging::attributes::named_scope());
	  logging::register_simple_filter_factory<severity_level>(logging::trivial::tag::severity::get_name());
    logging::init_from_settings(settings);
  }

  void cleanup() {
    logging::core::get()->remove_all_sinks();
  }
} // namespace logger
