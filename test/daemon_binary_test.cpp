#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace
{

std::string requiredEnv(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
    {
        return {};
    }
    return value;
}

int runDaemon(const std::vector<std::string>& args,
              const std::filesystem::path& redirectPath,
              bool returnFromRun = false)
{
    const std::string daemon = requiredEnv("BIOSCONFIG_MANAGER_BIN");
    const std::string preload = requiredEnv("BIOSCONFIG_MANAGER_PRELOAD");
    EXPECT_FALSE(daemon.empty());
    EXPECT_FALSE(preload.empty());
    if (daemon.empty() || preload.empty())
    {
        return -1;
    }

    if (!redirectPath.empty())
    {
        std::filesystem::remove_all(redirectPath);
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        setenv("LD_PRELOAD", preload.c_str(), 1);
        // When the daemon is built with AddressSanitizer, the non-instrumented
        // preload library above loads before the ASan runtime, which otherwise
        // aborts on its link-order check. The daemon also exits via
        // std::exit(0) with long-lived globals still allocated, which would
        // trip LeakSanitizer. Both are expected for this binary, so relax the
        // checks for the child. The variable is ignored in non-ASan builds.
        setenv("ASAN_OPTIONS", "verify_asan_link_order=0:detect_leaks=0", 1);
        setenv("BIOSCONFIG_MANAGER_EXIT_US", "500000", 1);
        if (!redirectPath.empty())
        {
            setenv("BIOSCONFIG_MANAGER_REDIRECT_PATH",
                   redirectPath.string().c_str(), 1);
            setenv("BIOSCONFIG_MANAGER_EXIT_ON_THROW", "1", 1);
        }
        if (returnFromRun)
        {
            setenv("BIOSCONFIG_MANAGER_RETURN_FROM_RUN", "1", 1);
        }

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args)
        {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execv(daemon.c_str(), argv.data());
        _exit(127);
    }

    if (pid < 0)
    {
        return -1;
    }

    int status = 0;
    for (int i = 0; i < 30; ++i)
    {
        const pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid)
        {
            if (!redirectPath.empty())
            {
                std::filesystem::remove_all(redirectPath);
            }
            return status;
        }
        if (result < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (!redirectPath.empty())
            {
                std::filesystem::remove_all(redirectPath);
            }
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    if (!redirectPath.empty())
    {
        std::filesystem::remove_all(redirectPath);
    }
    return status;
}

void expectExitedSuccessfully(int status)
{
    ASSERT_NE(status, -1);
    ASSERT_TRUE(WIFEXITED(status)) << "status=" << status;
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

} // namespace

TEST(DaemonBinaryTest, RunsWithDefaultPersistPath)
{
    const std::filesystem::path redirectPath =
        std::filesystem::temp_directory_path() /
        ("bios_config_daemon_default_" + std::to_string(getpid()));

    expectExitedSuccessfully(runDaemon({"biosconfig-manager"}, redirectPath));
}

TEST(DaemonBinaryTest, RunsWithArgumentPersistPath)
{
    const std::filesystem::path persistPath =
        std::filesystem::temp_directory_path() /
        ("bios_config_daemon_arg_" + std::to_string(getpid()));

    expectExitedSuccessfully(
        runDaemon({"biosconfig-manager", persistPath.string()}, {}));
    std::filesystem::remove_all(persistPath);
}

TEST(DaemonBinaryTest, ReturnsAfterBusReportsNoPendingEvents)
{
    const std::filesystem::path persistPath =
        std::filesystem::temp_directory_path() /
        ("bios_config_daemon_return_" + std::to_string(getpid()));

    expectExitedSuccessfully(
        runDaemon({"biosconfig-manager", persistPath.string()}, {}, true));
    std::filesystem::remove_all(persistPath);
}
