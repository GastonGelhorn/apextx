# SPDX-License-Identifier: GPL-2.0-only
# NB4 is a surface-only product. Force these values so an old CMake cache cannot
# silently add a different RF stack.
set(NB4_CAR_ONLY YES)
set(NB4_RUNTIME_LAYOUT ON CACHE BOOL "Runtime car UI orientation" FORCE)
add_definitions(-DLCD_DUAL_ORIENTATION -DLCD_RUNTIME_LAYOUT)
foreach(feature HELI FLIGHT_MODES PXX1 PXX2 XJT PPM DSM2 DSMP SBUS CROSSFIRE
                MULTIMODULE GHOST AFHDS2 HARDWARE_TRAINER_MULTI
                MODULE_PROTOCOL_D8 MODULE_PROTOCOL_FCC MODULE_PROTOCOL_LBT
                INTERNAL_MODULE_MULTI INTERNAL_MODULE_CRSF INTERNAL_MODULE_PXX1
                INTERNAL_MODULE_PXX2 INTERNAL_MODULE_AFHDS2A INTERNAL_MODULE_PPM)
  set(${feature} OFF CACHE BOOL "Excluded from NB4 car firmware" FORCE)
endforeach()
set(AFHDS3 ON CACHE BOOL "Original AFHDS3 module" FORCE)
# The interface languages compiled into both Noble targets, all selectable at
# run time. A fresh install starts in TRANSLATIONS, which is English unless the
# build says otherwise.
set(RADIO_LANG_SET EN ES DE CN)

option(NB4_LED_DIAGNOSTIC "Include the opt-in LED/LTDC bench diagnostic" OFF)
if(NB4_LED_DIAGNOSTIC)
  if(NOT PCBREV STREQUAL NB4)
    message(FATAL_ERROR "NB4_LED_DIAGNOSTIC requires PCBREV=NB4 (optional NB4_PLUS_HW)")
  endif()
  add_definitions(-DNB4_LED_DIAGNOSTIC)
endif()

if(PCBREV STREQUAL NB4)
  # RECOVERED_USART6 is the route this firmware ships and is the default.
  # The candidates stay available for compile-time coverage of the other
  # mappings that were considered.
  set(NB4_RF_PROFILE "RECOVERED_USART6" CACHE STRING
      "NB4 RF profile: UNQUALIFIED, candidates or RECOVERED_USART6")
  set_property(CACHE NB4_RF_PROFILE PROPERTY STRINGS
               UNQUALIFIED CANDIDATE_USART3 CANDIDATE_USART6
               RECOVERED_USART6)
  option(APEXTX_PUBLIC_RELEASE "Mark the image as a published build" OFF)
  option(NB4_RF_UPDATE_BENCH "Enable unit-bound experimental RF maintenance" OFF)
  set(NB4_RF_UPDATE_BENCH_SERIAL "" CACHE STRING "Exact USB serial for RF bench testing")
  if(NB4_RF_UPDATE_BENCH)
    string(LENGTH "${NB4_RF_UPDATE_BENCH_SERIAL}" NB4_RF_UPDATE_SERIAL_LENGTH)
    if(APEXTX_PUBLIC_RELEASE OR NOT NB4_RF_UPDATE_SERIAL_LENGTH EQUAL 12 OR
       NOT NB4_RF_UPDATE_BENCH_SERIAL MATCHES "^[0-9A-F]+$")
      message(FATAL_ERROR "RF bench builds require one 12-digit unit serial and cannot be public releases")
    endif()
    add_definitions(-DNB4_RF_UPDATE_BENCH
      -DNB4_RF_UPDATE_BENCH_SERIAL=\"${NB4_RF_UPDATE_BENCH_SERIAL}\")
  endif()

  set(NB4_RF_PROFILE_VALUES
      UNQUALIFIED CANDIDATE_USART3 CANDIDATE_USART6
      RECOVERED_USART6)
  if(NOT NB4_RF_PROFILE IN_LIST NB4_RF_PROFILE_VALUES)
    message(FATAL_ERROR "Invalid NB4_RF_PROFILE='${NB4_RF_PROFILE}'")
  endif()

  include(${CMAKE_CURRENT_LIST_DIR}/nb4_rf_qualified.cmake)
  if(NB4_RF_PROFILE STREQUAL RECOVERED_USART6)
    if(NOT NB4_RF_QUALIFIED)
      message(FATAL_ERROR
        "Active NB4 RF requested without a reviewed RF profile. "
        "Run tools/nb4-generate-qualified-profile.py first.")
    endif()
    if(NOT NB4_RF_QUALIFIED_CANDIDATE STREQUAL "USART6")
      message(FATAL_ERROR "Recovered NB4 AFHDS3 route must be USART6")
    endif()
    if(NOT NB4_RF_QUALIFIED_FRAMING STREQUAL "ADDRESSLESS_SLIP")
      message(FATAL_ERROR "Recovered NB4 AFHDS3 framing must be addressless SLIP")
    endif()
    add_definitions(-DNB4_RF_PROFILE_RECOVERED_LAB)
    add_definitions(-DNB4_RF_TRANSPORT_USART6
                    -DNB4_RF_FRAMING_ADDRESSLESS_SLIP)
  elseif(NB4_RF_PROFILE STREQUAL CANDIDATE_USART3)
    add_definitions(-DNB4_RF_PROFILE_CANDIDATE_USART3
                    -DNB4_RF_TRANSPORT_USART3)
  elseif(NB4_RF_PROFILE STREQUAL CANDIDATE_USART6)
    add_definitions(-DNB4_RF_PROFILE_CANDIDATE_USART6
                    -DNB4_RF_TRANSPORT_USART6)
  else()
    add_definitions(-DNB4_RF_PROFILE_UNQUALIFIED)
  endif()

  if(APEXTX_PUBLIC_RELEASE)
    add_definitions(-DAPEXTX_PUBLIC_RELEASE_BUILD)
  endif()

  # USART6 is the recovered AFHDS3 path, but remains logical slot 0: the original
  # NB4 has no user-selectable external-module bay.
  set(HARDWARE_EXTERNAL_MODULE NO)
  set(MODULE_SIZE_STD OFF CACHE BOOL "No external module bay on NB4" FORCE)
  set(MODULE_SIZE_SML OFF CACHE BOOL "No external module bay on NB4" FORCE)

  set(INTERNAL_MODULE_AFHDS3 OFF CACHE BOOL "NB4 RF disabled by profile" FORCE)
  set(INTERNAL_MODULES "" CACHE STRING "NB4 logical RF modules" FORCE)
  set(DEFAULT_INTERNAL_MODULE "" CACHE STRING "NB4 default RF module" FORCE)
  if(NOT NB4_RF_PROFILE STREQUAL UNQUALIFIED)
    set(INTERNAL_MODULE_AFHDS3 ON CACHE BOOL "NB4 candidate AFHDS3" FORCE)
    set(INTERNAL_MODULES AFHDS3 CACHE STRING "NB4 logical RF module" FORCE)
    set(DEFAULT_INTERNAL_MODULE FLYSKY_AFHDS3 CACHE STRING
        "NB4 default RF module" FORCE)
  endif()

  message(STATUS "NB4 RF profile: ${NB4_RF_PROFILE} (public=${APEXTX_PUBLIC_RELEASE})")
else()
  # NB4P keeps its existing upstream RF mapping; this qualification is scoped to
  # the original NB4 only.
  set(HARDWARE_EXTERNAL_MODULE YES)
  set(INTERNAL_MODULE_AFHDS3 ON CACHE BOOL "Original internal AFHDS3" FORCE)
  set(INTERNAL_MODULES AFHDS3 CACHE STRING "NB4P internal module" FORCE)
  set(DEFAULT_INTERNAL_MODULE FLYSKY_AFHDS3 CACHE STRING
      "Default internal module" FORCE)
endif()
