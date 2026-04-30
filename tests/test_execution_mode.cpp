// Cross-language parity tests for
// signalwire::core::logging_config::get_execution_mode and
// signalwire::utils::is_serverless_mode.
//
// Mirrors signalwire-python tests/unit/utils/test_execution_mode.py:
// every branch of the env-var detection ladder must match the same
// precedence and return the same canonical string.

#include "signalwire/core/logging_config.hpp"
#include "signalwire/utils/serverless.hpp"

#include <cstdlib>
#include <string>

using signalwire::core::logging_config::get_execution_mode;
using signalwire::utils::is_serverless_mode;

namespace {

// Every env var the function inspects.  Clear all of them per test so
// nothing that the developer's shell happens to export poisons the
// assertion.
const char* kExecEnvKeys[] = {
    "GATEWAY_INTERFACE",
    "AWS_LAMBDA_FUNCTION_NAME",
    "LAMBDA_TASK_ROOT",
    "FUNCTION_TARGET",
    "K_SERVICE",
    "GOOGLE_CLOUD_PROJECT",
    "AZURE_FUNCTIONS_ENVIRONMENT",
    "FUNCTIONS_WORKER_RUNTIME",
    "AzureWebJobsStorage",
};

void clear_exec_env() {
    for (const char* k : kExecEnvKeys) {
        ::unsetenv(k);
    }
}

}  // namespace

// --- get_execution_mode: every branch -------------------------------

TEST(execution_mode_default_is_server) {
    clear_exec_env();
    ASSERT_EQ(get_execution_mode(), std::string("server"));
    return true;
}

TEST(execution_mode_cgi_via_gateway_interface) {
    clear_exec_env();
    ::setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
    ASSERT_EQ(get_execution_mode(), std::string("cgi"));
    clear_exec_env();
    return true;
}

TEST(execution_mode_lambda_via_function_name) {
    clear_exec_env();
    ::setenv("AWS_LAMBDA_FUNCTION_NAME", "my-fn", 1);
    ASSERT_EQ(get_execution_mode(), std::string("lambda"));
    clear_exec_env();
    return true;
}

TEST(execution_mode_lambda_via_task_root) {
    clear_exec_env();
    ::setenv("LAMBDA_TASK_ROOT", "/var/task", 1);
    ASSERT_EQ(get_execution_mode(), std::string("lambda"));
    clear_exec_env();
    return true;
}

TEST(execution_mode_gcf_via_function_target) {
    clear_exec_env();
    ::setenv("FUNCTION_TARGET", "my_handler", 1);
    ASSERT_EQ(get_execution_mode(), std::string("google_cloud_function"));
    clear_exec_env();
    return true;
}

TEST(execution_mode_gcf_via_k_service) {
    clear_exec_env();
    ::setenv("K_SERVICE", "svc", 1);
    ASSERT_EQ(get_execution_mode(), std::string("google_cloud_function"));
    clear_exec_env();
    return true;
}

TEST(execution_mode_gcf_via_project) {
    clear_exec_env();
    ::setenv("GOOGLE_CLOUD_PROJECT", "proj", 1);
    ASSERT_EQ(get_execution_mode(), std::string("google_cloud_function"));
    clear_exec_env();
    return true;
}

TEST(execution_mode_azure_via_environment) {
    clear_exec_env();
    ::setenv("AZURE_FUNCTIONS_ENVIRONMENT", "Production", 1);
    ASSERT_EQ(get_execution_mode(), std::string("azure_function"));
    clear_exec_env();
    return true;
}

TEST(execution_mode_azure_via_worker_runtime) {
    clear_exec_env();
    ::setenv("FUNCTIONS_WORKER_RUNTIME", "node", 1);
    ASSERT_EQ(get_execution_mode(), std::string("azure_function"));
    clear_exec_env();
    return true;
}

TEST(execution_mode_azure_via_web_jobs_storage) {
    clear_exec_env();
    ::setenv("AzureWebJobsStorage", "DefaultEndpointsProtocol=https", 1);
    ASSERT_EQ(get_execution_mode(), std::string("azure_function"));
    clear_exec_env();
    return true;
}

// CGI must beat Lambda — cross-language precedence contract.
TEST(execution_mode_cgi_beats_lambda) {
    clear_exec_env();
    ::setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
    ::setenv("AWS_LAMBDA_FUNCTION_NAME", "my-fn", 1);
    ASSERT_EQ(get_execution_mode(), std::string("cgi"));
    clear_exec_env();
    return true;
}

TEST(execution_mode_lambda_beats_gcf) {
    clear_exec_env();
    ::setenv("AWS_LAMBDA_FUNCTION_NAME", "my-fn", 1);
    ::setenv("FUNCTION_TARGET", "h", 1);
    ASSERT_EQ(get_execution_mode(), std::string("lambda"));
    clear_exec_env();
    return true;
}

TEST(execution_mode_gcf_beats_azure) {
    clear_exec_env();
    ::setenv("FUNCTION_TARGET", "h", 1);
    ::setenv("AZURE_FUNCTIONS_ENVIRONMENT", "Production", 1);
    ASSERT_EQ(get_execution_mode(), std::string("google_cloud_function"));
    clear_exec_env();
    return true;
}

// --- is_serverless_mode --------------------------------------------

TEST(serverless_mode_server_is_false) {
    clear_exec_env();
    ASSERT_FALSE(is_serverless_mode());
    return true;
}

TEST(serverless_mode_lambda_is_true) {
    clear_exec_env();
    ::setenv("AWS_LAMBDA_FUNCTION_NAME", "my-fn", 1);
    ASSERT_TRUE(is_serverless_mode());
    clear_exec_env();
    return true;
}

// CGI is short-lived per request — counts as serverless.
TEST(serverless_mode_cgi_is_true) {
    clear_exec_env();
    ::setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
    ASSERT_TRUE(is_serverless_mode());
    clear_exec_env();
    return true;
}

TEST(serverless_mode_azure_is_true) {
    clear_exec_env();
    ::setenv("AZURE_FUNCTIONS_ENVIRONMENT", "Production", 1);
    ASSERT_TRUE(is_serverless_mode());
    clear_exec_env();
    return true;
}
