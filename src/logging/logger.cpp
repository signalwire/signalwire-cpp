#include "signalwire/logging.hpp"

// Logger is header-only. This file exists so the static library
// always has at least one translation unit from this directory.

namespace signalwire {

// Force the singleton to be instantiated in this TU so that
// SIGNALWIRE_LOG_LEVEL / SIGNALWIRE_LOG_MODE environment variables
// are read early.
static Logger& logger_init_ = Logger::instance();

} // namespace signalwire
