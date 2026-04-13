#include "logger.hpp"

#include <spdlog/spdlog.h>

std::shared_ptr<spdlog::logger> logger = spdlog::default_logger();
