#include <lib/system/logger.hpp>

#include <boost/log/core.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/filter_parser.hpp>
#include <boost/log/utility/setup/formatter_parser.hpp>
#include <boost/log/utility/setup/from_settings.hpp>

namespace logger {
void initialize(const logging::settings& settings) {
  logging::add_common_attributes();

  // formatters
  logging::register_simple_formatter_factory<severity_level, char>(logging::trivial::tag::severity::get_name());
  // filters
  logging::register_simple_filter_factory<severity_level>(logging::trivial::tag::severity::get_name());

  logging::init_from_settings(settings);
}

void cleanup() {
  logging::core::get()->remove_all_sinks();
}
}  // namespace logger
