#include "common.hpp"
#include "password.hpp"

#include <dlfcn.h>
#include <openssl/evp.h>

#include <nlohmann/json.hpp>
#include <xyz/openbmc_project/BIOSConfig/Common/error.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{

int failPbkdfCall = 0;
int pbkdfCallCount = 0;

using PbkdfFn = int (*)(const char*, int, const unsigned char*, int, int,
                        const EVP_MD*, int, unsigned char*);

PbkdfFn realPbkdf()
{
    static auto fn =
        reinterpret_cast<PbkdfFn>(dlsym(RTLD_NEXT, "PKCS5_PBKDF2_HMAC"));
    return fn;
}

std::vector<uint8_t> hashPassword(
    const std::string& password,
    const std::array<uint8_t, bios_config_pwd::maxSeedSize>& seed,
    const EVP_MD* digest, int digestLen)
{
    std::vector<uint8_t> hash(bios_config_pwd::maxHashSize, 0);
    PbkdfFn fn = realPbkdf();
    if (fn == nullptr)
    {
        ADD_FAILURE() << "dlsym failed to resolve PKCS5_PBKDF2_HMAC";
        return hash;
    }
    EXPECT_EQ(fn(password.c_str(), password.length() + 1, seed.data(),
                 seed.size(), bios_config_pwd::iterValue, digest, digestLen,
                 hash.data()),
              1);
    return hash;
}

} // namespace

extern "C" int PKCS5_PBKDF2_HMAC(
    const char* pass, int passlen, const unsigned char* salt, int saltlen,
    int iter, const EVP_MD* digest, int keylen, unsigned char* out)
{
    ++pbkdfCallCount;
    if (failPbkdfCall != 0 && pbkdfCallCount == failPbkdfCall)
    {
        return 0;
    }

    PbkdfFn fn = realPbkdf();
    if (fn == nullptr)
    {
        // dlsym failed to resolve the real symbol; report failure to the
        // caller instead of calling through a null pointer.
        return 0;
    }
    return fn(pass, passlen, salt, saltlen, iter, digest, keylen, out);
}

namespace bios_config_pwd::test
{

using namespace bios_config_pwd;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;

class TestablePassword : public Password
{
  public:
    using Password::Password;
    using Password::verifyPassword;

    // Overload discarding the seed JSON, preserving the three-argument shape.
    void verifyPassword(std::string userName, std::string currentPassword,
                        std::string newPassword)
    {
        nlohmann::json discarded;
        Password::verifyPassword(userName, currentPassword, newPassword,
                                 discarded);
    }
};

class PasswordOpenSslFailureTest : public bios_config::test::BiosConfigTest
{
  protected:
    void SetUp() override
    {
        bios_config::test::BiosConfigTest::SetUp();
        password = std::make_unique<TestablePassword>(
            *objServer, systemBus, seedPath.parent_path().string());
    }

    void writeSeedFile(const std::string& hashAlgo, const EVP_MD* digest,
                       int digestLen, const std::string& currentPassword)
    {
        std::array<uint8_t, maxSeedSize> seed;
        seed.fill(0x5a);

        const auto hash =
            hashPassword(currentPassword, seed, digest, digestLen);

        nlohmann::json seedData;
        seedData["HashAlgo"] = hashAlgo;
        seedData["Seed"] = seed;
        seedData["UserPwdHash"] = hash;
        seedData["AdminPwdHash"] = hash;

        std::ofstream file(seedPath);
        ASSERT_TRUE(file.is_open());
        file << seedData.dump();
    }

    void expectVerifyIntegrityFailure(const std::string& hashAlgo,
                                      const EVP_MD* digest, int digestLen)
    {
        const std::string currentPassword = "current-password";
        writeSeedFile(hashAlgo, digest, digestLen, currentPassword);

        pbkdfCallCount = 0;
        failPbkdfCall = 2;
        EXPECT_THROW(password->verifyPassword("AdminPassword", currentPassword,
                                              "new-password"),
                     InternalFailure);
        failPbkdfCall = 0;
    }

    std::unique_ptr<TestablePassword> password;
};

TEST_F(PasswordOpenSslFailureTest, VerifyPasswordThrowsWhenSha256IntegrityFails)
{
    expectVerifyIntegrityFailure("SHA256", EVP_sha256(), SHA256_DIGEST_LENGTH);
}

TEST_F(PasswordOpenSslFailureTest, VerifyPasswordThrowsWhenSha384IntegrityFails)
{
    expectVerifyIntegrityFailure("SHA384", EVP_sha384(), SHA384_DIGEST_LENGTH);
}

} // namespace bios_config_pwd::test
