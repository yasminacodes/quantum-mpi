#include <memory>
#include <mutex>

#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

std::shared_ptr<spdlog::logger> logger;

namespace {

void initialize_cunqa_client_logger()
{
    if (logger) {
        return;
    }

    auto existing_logger = spdlog::get("quantum_mpi_cunqa_client");

    if (existing_logger) {
        logger = existing_logger;
        return;
    }

    logger = spdlog::stdout_color_mt("quantum_mpi_cunqa_client");
    logger->set_level(spdlog::level::warn);
}

} // namespace

__attribute__((constructor)) static void quantum_mpi_initialize_logger_symbol()
{
    static std::once_flag logger_once;
    std::call_once(logger_once, initialize_cunqa_client_logger);
}

void quantum_mpi_ensure_cunqa_logger_ready()
{
    quantum_mpi_initialize_logger_symbol();
}
