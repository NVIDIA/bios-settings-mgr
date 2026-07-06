#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <typeinfo>

namespace
{

constexpr const char* defaultPersistPath = "/var/lib/bios-settings-manager";
thread_local char translatedPath[4096];

struct sd_bus;
struct sd_bus_message;

template <typename T>
T loadSymbol(const char* name)
{
    return reinterpret_cast<T>(dlsym(RTLD_NEXT, name));
}

bool shouldReturnFromRun()
{
    return std::getenv("BIOSCONFIG_MANAGER_RETURN_FROM_RUN") != nullptr;
}

const char* translatePath(const char* path)
{
    const char* redirect = std::getenv("BIOSCONFIG_MANAGER_REDIRECT_PATH");
    if (path == nullptr || redirect == nullptr || redirect[0] == '\0')
    {
        return path;
    }

    const std::size_t prefixLen = std::strlen(defaultPersistPath);
    if (std::strncmp(path, defaultPersistPath, prefixLen) != 0)
    {
        return path;
    }
    if (path[prefixLen] != '\0' && path[prefixLen] != '/')
    {
        return path;
    }

    std::snprintf(translatedPath, sizeof(translatedPath), "%s%s", redirect,
                  path + prefixLen);
    return translatedPath;
}

void* exitDaemon(void*)
{
    const char* delayValue = std::getenv("BIOSCONFIG_MANAGER_EXIT_US");
    const useconds_t delay =
        delayValue == nullptr ? 500000U
                              : static_cast<useconds_t>(std::atoi(delayValue));
    usleep(delay);
    std::exit(0);
}

__attribute__((constructor)) void startExitThread()
{
    pthread_t thread;
    if (pthread_create(&thread, nullptr, exitDaemon, nullptr) == 0)
    {
        pthread_detach(thread);
    }
}

} // namespace

extern "C" int mkdir(const char* path, mode_t mode)
{
    using Fn = int (*)(const char*, mode_t);
    static Fn real = loadSymbol<Fn>("mkdir");
    return real(translatePath(path), mode);
}

extern "C" int mkdirat(int dirfd, const char* path, mode_t mode)
{
    using Fn = int (*)(int, const char*, mode_t);
    static Fn real = loadSymbol<Fn>("mkdirat");
    return real(dirfd, translatePath(path), mode);
}

extern "C" int stat(const char* path, struct stat* buf)
{
    using Fn = int (*)(const char*, struct stat*);
    static Fn real = loadSymbol<Fn>("stat");
    return real(translatePath(path), buf);
}

extern "C" int lstat(const char* path, struct stat* buf)
{
    using Fn = int (*)(const char*, struct stat*);
    static Fn real = loadSymbol<Fn>("lstat");
    return real(translatePath(path), buf);
}

extern "C" int access(const char* path, int mode)
{
    using Fn = int (*)(const char*, int);
    static Fn real = loadSymbol<Fn>("access");
    return real(translatePath(path), mode);
}

extern "C" int fstatat(int dirfd, const char* path, struct stat* buf, int flags)
{
    using Fn = int (*)(int, const char*, struct stat*, int);
    static Fn real = loadSymbol<Fn>("fstatat");
    if (real == nullptr)
    {
        real = loadSymbol<Fn>("fstatat64");
    }
    return real(dirfd, translatePath(path), buf, flags);
}

extern "C" [[noreturn]] void __cxa_throw(
    void* exception, std::type_info* typeInfo, void (*destructor)(void*))
{
    if (std::getenv("BIOSCONFIG_MANAGER_EXIT_ON_THROW") != nullptr)
    {
        std::exit(0);
    }

    using Fn = void (*)(void*, std::type_info*, void (*)(void*));
    static Fn real = loadSymbol<Fn>("__cxa_throw");
    real(exception, typeInfo, destructor);
    __builtin_unreachable();
}

extern "C" int sd_bus_process(sd_bus* bus, sd_bus_message** ret)
{
    if (shouldReturnFromRun())
    {
        if (ret != nullptr)
        {
            *ret = nullptr;
        }
        return 0;
    }

    using Fn = int (*)(sd_bus*, sd_bus_message**);
    static Fn real = loadSymbol<Fn>("sd_bus_process");
    return real(bus, ret);
}

extern "C" int sd_bus_get_events(sd_bus* bus)
{
    if (shouldReturnFromRun())
    {
        return 0;
    }

    using Fn = int (*)(sd_bus*);
    static Fn real = loadSymbol<Fn>("sd_bus_get_events");
    return real(bus);
}

extern "C" int sd_bus_get_timeout(sd_bus* bus, uint64_t* timeoutUsec)
{
    if (shouldReturnFromRun())
    {
        if (timeoutUsec != nullptr)
        {
            *timeoutUsec = UINT64_MAX;
        }
        return 0;
    }

    using Fn = int (*)(sd_bus*, uint64_t*);
    static Fn real = loadSymbol<Fn>("sd_bus_get_timeout");
    return real(bus, timeoutUsec);
}
