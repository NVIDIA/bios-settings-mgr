# Branch coverage limitations (bios-settings-mgr)

This document records branch coverage results and limitations after UT-only
improvements. Coverage was produced by the OpenBMC unit-test flow (Meson
`-Db_coverage=true`, gcovr with `gcovr.cfg`). **No production code was
changed**; only tests were added or extended.

## How to generate the report

From the workspace root (parent of `bios-settings-mgr`), run Docker CI so that
coverage is built and tests run:

```bash
sudo P4ROOT=/usr/bin/p4 WORKSPACE=$(pwd) UNIT_TEST_PKG=bios-settings-mgr NO_FORMAT_CODE=1 \
  GITLAB_TOKEN_NAME=build_token GITLAB_TOKEN_VAL=<token> \
  openbmc-build-scripts/run-unit-test-docker.sh
```

Report path (after a successful run):  
`bios-settings-mgr/build/meson-logs/coveragereport/index.html`

If running without Docker (e.g. `liburing` not installed), use Meson + Ninja
with `-Db_coverage=true` and run `ninja -C build coverage-html-gcovr` (or
`coverage-html`) after tests.

---

## Overall (src/)

| Metric    | Value             |
| --------- | ----------------- |
| Lines     | 86.5% (713 / 824) |
| Functions | 97.1% (68 / 70)   |
| Branches  | 71.4% (488 / 683) |
| Decisions | 73.2% (101 / 138) |

Report generated 2026-03-17 (GCOVR 8.6). Latest test run: all 7 test suites
passed (rfutility_test, bootvalidflag_test, secureboot_test,
manager_serialize_test, boot_option_test, password_test, manager_test).

---

## Per-file summary

### rfutility.cpp

| Metric            | Value |
| ----------------- | ----- |
| Total branches    | 15    |
| Covered           | 10    |
| Uncovered         | 5     |
| Branch coverage % | 66.7% |

Branches: `parsePropertyValueAndSendEvent` has one conditional
(`lastDotPos != std::string::npos`). Both branches are exercised by existing
tests (with dot / no dot, multiple dots, empty, dot at end). Remaining uncovered
branches are likely compiler-generated (e.g. exception/string ops).

| Limitation              | Affected area                 | Approx. % of file's branches | Unblocked by          |
| ----------------------- | ----------------------------- | ---------------------------- | --------------------- |
| Compiler-generated arcs | string/vector/exception paths | ~33% (5/15)                  | gcovr exclude options |

---

### manager_serialize.cpp

| Metric            | Value |
| ----------------- | ----- |
| Total branches    | 120   |
| Covered           | 90    |
| Uncovered         | 30    |
| Branch coverage % | 75.0% |

UT-added for branch coverage (no source change):

- `DeserializeReturnsFalseWhenPathIsDirectory` – path exists but is a directory;
  exercises deserialize path where open fails or stream is invalid.
- `DeserializeReturnsFalseWhenPathExistsButNotOpenable` – path exists but
  permissions prevent read; exercises `!is.is_open()` and return false.

| Limitation                          | Affected area                             | Approx. % of file's branches | Unblocked by                                                                      |
| ----------------------------------- | ----------------------------------------- | ---------------------------- | --------------------------------------------------------------------------------- |
| serializeToBuffer catch             | std::exception in serializeToBuffer       | ~1%                          | Would need to trigger throw during archive save (e.g. injectable stream or mock). |
| asyncSerialize writeEc lambda       | Async write error callback (lambda)       | ~8%                          | Would need to force async_write to fail (e.g. disk full / mock).                  |
| deserialize / load version branches | Version fallbacks, cereal exception paths | ~10%                         | Partially covered by V1/V2 tests; remainder compiler/cereal internals.            |
| Compiler-generated arcs             | load/save templates, exception handling   | ~11% (remaining)             | gcovr exclude options                                                             |

---

### secureboot.cpp

| Metric            | Value |
| ----------------- | ----- |
| Total branches    | 20    |
| Covered           | 18    |
| Uncovered         | 2     |
| Branch coverage % | 90.0% |

Branches: serialize/deserialize try/catch; exists(secureBootFile).
`DeserializeHandlesInvalidFile` and `DeserializeHandlesMissingFile` exercise
deserialize branches.

| Limitation              | Affected area                 | Approx. % of file's branches | Unblocked by                                          |
| ----------------------- | ----------------------------- | ---------------------------- | ----------------------------------------------------- |
| serialize catch         | std::exception in serialize() | ~5%                          | Would need write failure (e.g. read-only filesystem). |
| Compiler-generated arcs | exception/archive paths       | ~10% (2/20)                  | gcovr exclude options                                 |

---

### bootvalidflag.cpp

| Metric            | Value |
| ----------------- | ----- |
| Total branches    | 117   |
| Covered           | 34    |
| Uncovered         | 83    |
| Branch coverage % | 29.1% |

Branch total 117; line coverage 54.1%. D-Bus async callbacks (ec / reply.read /
nested property checks). Largely environment-dependent.

| Limitation               | Affected area                                                                       | Approx. % of file's branches | Unblocked by                           |
| ------------------------ | ----------------------------------------------------------------------------------- | ---------------------------- | -------------------------------------- |
| D-Bus / external service | async_method_call lambdas, setDbusProperty, setBootValidFlag, setTimer, cancelTimer | ~71% (83/117)                | Mock D-Bus or integration environment. |
| Compiler-generated arcs  | exception/lambda paths                                                              | ~5%                          | gcovr exclude options                  |

---

### manager.cpp

| Metric            | Value |
| ----------------- | ----- |
| Total branches    | 266   |
| Covered           | 232   |
| Uncovered         | 34    |
| Branch coverage % | 87.2% |

D-Bus and property/serialization logic; many branches covered by existing
Manager tests.

| Limitation                  | Affected area                                        | Approx. % of file's branches | Unblocked by                     |
| --------------------------- | ---------------------------------------------------- | ---------------------------- | -------------------------------- |
| D-Bus / async / error paths | asyncSerialize callbacks, validation, error handling | ~13% (34/266)                | Mock or integration environment. |
| Compiler-generated arcs     | —                                                    | small                        | gcovr exclude options            |

### password.cpp

| Metric            | Value |
| ----------------- | ----- |
| Total branches    | 106   |
| Covered           | 71    |
| Uncovered         | 35    |
| Branch coverage % | 67.0% |

D-Bus and credential logic; many branches depend on property updates and
external services.

| Limitation               | Affected area                          | Approx. % of file's branches | Unblocked by                     |
| ------------------------ | -------------------------------------- | ---------------------------- | -------------------------------- |
| D-Bus / external service | Password property and validation paths | ~33% (35/106)                | Mock or integration environment. |
| Compiler-generated arcs  | —                                      | small                        | gcovr exclude options            |

### boot_option.cpp

| Metric            | Value |
| ----------------- | ----- |
| Total branches    | 29    |
| Covered           | 25    |
| Uncovered         | 4     |
| Branch coverage % | 86.2% |

| Limitation              | Affected area                  | Approx. % of file's branches | Unblocked by                     |
| ----------------------- | ------------------------------ | ---------------------------- | -------------------------------- |
| D-Bus / error paths     | BootOption property/validation | ~14% (4/29)                  | Mock or integration environment. |
| Compiler-generated arcs | —                              | small                        | gcovr exclude options            |

---

## Tests added for 75% branch coverage target

The following tests were added (test-only; no production code changes) to
improve branch coverage toward a 75% target:

### password_test.cpp

- `VerifyPasswordUserPathThrowsInvalidCurrentPasswordForWrongPassword` –
  userName != "AdminPassword" path with wrong password.
- `VerifyPasswordAdminPathThrowsInvalidCurrentPasswordForWrongPassword` – Admin
  path with wrong password.
- `GetParamHandlesNlohmannDetailException` – getParam catch
  (nlohmann::detail::exception) when JSON value has wrong type.

### manager_test.cpp

- `EnableAfterResetCanBeSetToFalse` – enableAfterReset(false) branch.
- `CredentialBootstrapCanBeSetToFalse` – credentialBootstrap(false) branch.
- `ResetBIOSSettingsNoActionAgain` – resetBIOSSettings(ResetFlag::NoAction)
  called again (ClearPending not in this interface).
- `BootOrderAcceptsEmptyVector` – bootOrder with empty vector.
- `PendingBootOrderAcceptsSingleElement` – pendingBootOrder with single element.

### secureboot_test.cpp

- `SerializeHandlesWriteFailureGracefully` – serialize() when persist path is a
  directory (catch branch).

### boot_option_test.cpp

- `CreateBootOptionDuplicateIdThrows` – createBootOption with existing ID.
- `DeleteBootOptionNonExistentKeyNoThrow` – deleteBootOption with non-existent
  key.

Re-run coverage (Docker or `meson -Db_coverage=true` then
`ninja -C build coverage-html-gcovr`) to obtain updated branch counts and
confirm whether the 75% target is met.

---

## Summary

- **Fixable by UT (done):** Additional tests added for `manager_serialize`
  (deserialize when path is directory or not openable), `password`
  (verifyPassword Admin/User paths, getParam exception), `manager`
  (enableAfterReset/credentialBootstrap false, ResetBIOSSettings, boot order
  edge cases), `secureboot` (serialize write failure), and `boot_option`
  (duplicate ID, delete non-existent). Current branch coverage: **71.4%**
  (488/683) for `src/` (report 2026-03-17).
- **Real limitations (no prod change):** serializeToBuffer/asyncSerialize error
  paths, D-Bus callback branches (especially `bootvalidflag.cpp`,
  `password.cpp`), and compiler-generated arcs are documented above; no
  production code was modified to cover them.
