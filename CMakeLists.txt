cmake_minimum_required(VERSION 3.20)
project(BastionDTL VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(
  bastiondtl
  src/common/types.cpp
  src/common/amount.cpp
  src/common/hash.cpp
  src/common/json.cpp
  src/security/identity.cpp
  src/custody/account.cpp
  src/custody/ledger.cpp
  src/custody/reconcile.cpp
  src/receipt/receipt.cpp
  src/receipt/manifest.cpp
  src/settlement/settlement.cpp
  src/settlement/batch.cpp
  src/risk/liquidity.cpp
  src/governance/change_control.cpp
  src/runtime/scenarios.cpp
  src/runtime/version.cpp
  src/runtime/cli.cpp
  src/main.cpp
)

target_include_directories(bastiondtl PRIVATE src)

if(MSVC)
  target_compile_options(bastiondtl PRIVATE /W4 /WX)
else()
  target_compile_options(bastiondtl PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

