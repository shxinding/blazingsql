#=============================================================================
# Copyright 2018-2020 BlazingDB, Inc.
#     Copyright 2018 Felipe Aramburu <felipe@blazingdb.com>
#     Copyright 2018-2020 Percy Camilo Triveño Aucahuasi <percy@blazingdb.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#=============================================================================

PROJECT(blazingdb-io)

cmake_minimum_required(VERSION 3.11)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/" ${CMAKE_MODULE_PATH})
message(STATUS "CMAKE_MODULE_PATH: ${CMAKE_MODULE_PATH}")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(GCS_SUPPORT "Enables support for GCS fileSystem" ON)
option(S3_SUPPORT "Enables support for S3 fileSystem" ON)

###################################################################################################
# - conda environment -----------------------------------------------------------------------------

if ("$ENV{CONDA_BUILD}" STREQUAL "1")
    set(CMAKE_SYSTEM_PREFIX_PATH "$ENV{BUILD_PREFIX};$ENV{PREFIX};${CMAKE_SYSTEM_PREFIX_PATH}")
    message(STATUS "Conda build detected, CMAKE_SYSTEM_PREFIX_PATH set to: ${CMAKE_SYSTEM_PREFIX_PATH}")

    set(ENV{PKG_CONFIG_PATH} $ENV{BUILD_PREFIX}/lib/pkgconfig/) # used by find libcurl
    set(ENV{LD_LIBRARY_PATH} $ENV{BUILD_PREFIX}/lib/) # to link the tests correctly against libcurl.so
    find_library(CURL PATHS $ENV{BUILD_PREFIX}/lib/ NO_DEFAULT_PATH)
    set(GOOGLE_CLOUD_CPP_INSTALL_DIR $ENV{BUILD_PREFIX})
    set(AWS_SDK_CPP_INSTALL_DIR $ENV{BUILD_PREFIX})
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} $ENV{BUILD_PREFIX}/lib/cmake)

elseif (DEFINED ENV{CONDA_PREFIX})
    set(CMAKE_SYSTEM_PREFIX_PATH "$ENV{CONDA_PREFIX};${CMAKE_SYSTEM_PREFIX_PATH}")
    message(STATUS "Conda environment detected, CMAKE_SYSTEM_PREFIX_PATH set to: ${CMAKE_SYSTEM_PREFIX_PATH}")

    set(ENV{PKG_CONFIG_PATH} $ENV{CONDA_PREFIX}/lib/pkgconfig/) # used by find libcurl
    set(ENV{LD_LIBRARY_PATH} $ENV{CONDA_PREFIX}/lib/) # to link the tests correctly against libcurl.so
    find_library(CURL PATHS $ENV{CONDA_PREFIX}/lib/ NO_DEFAULT_PATH)
    set(GOOGLE_CLOUD_CPP_INSTALL_DIR $ENV{CONDA_PREFIX})
    set(AWS_SDK_CPP_INSTALL_DIR $ENV{CONDA_PREFIX})
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} $ENV{CONDA_PREFIX}/lib/cmake)

endif ()


#########################################################################3
# - compiler stuff ------------------
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CUDA_STANDARD 14)
set(CMAKE_CUDA_STANDARD_REQURIED ON)

# Include CMake modules
include(FeatureSummary)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(CTest)

# TODO percy jp mario we don't need curl unless we use the storages
add_definitions( -DCURL_STATICLIB )
find_package(CURL REQUIRED)

# BEGIN find arrow
find_path(ARROW_INCLUDE_DIR "arrow" HINTS "$ENV{ARROW_ROOT}/include")
find_library(ARROW_LIB "arrow" NAMES libarrow HINTS "$ENV{ARROW_ROOT}/lib" "$ENV{ARROW_ROOT}/build")
find_library(ARROW_CUDA_LIB "arrow_cuda" NAMES libarrow_cuda HINTS "$ENV{ARROW_ROOT}/lib" "$ENV{ARROW_ROOT}/build")
message(STATUS "ARROW: ARROW_INCLUDE_DIR set to ${ARROW_INCLUDE_DIR}")
message(STATUS "ARROW: ARROW_LIB set to ${ARROW_LIB}")
message(STATUS "ARROW: ARROW_CUDA_LIB set to ${ARROW_CUDA_LIB}")
add_library(arrow SHARED IMPORTED ${ARROW_LIB})
add_library(arrow_cuda SHARED IMPORTED ${ARROW_CUDA_LIB})

if(ARROW_INCLUDE_DIR AND ARROW_LIB AND ARROW_CUDA_LIB)
  set_target_properties(arrow PROPERTIES IMPORTED_LOCATION ${ARROW_LIB})
  set_target_properties(arrow_cuda PROPERTIES IMPORTED_LOCATION ${ARROW_CUDA_LIB})
endif(ARROW_INCLUDE_DIR AND ARROW_LIB AND ARROW_CUDA_LIB)
# END find arrow

set(FILESYSTEM_SRC_FILES
    ${CMAKE_SOURCE_DIR}/src/FileSystem/FileSystemType.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/Path.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/Uri.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/FileStatus.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/FileFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/FileSystemConnection.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/FileSystemException.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/LocalFileSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/HadoopFileSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/FileSystemManager.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/FileSystemEntity.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/FileSystemRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/private/LocalFileSystem_p.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/private/HadoopFileSystem_p.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/private/FileSystemManager_p.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/private/FileSystemFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem/private/FileSystemRepository_p.cpp)

set(LOGGING_SRC_FILES
    ${CMAKE_SOURCE_DIR}/src/Library/Logging/BlazingLogger.cpp
    ${CMAKE_SOURCE_DIR}/src/Library/Logging/CoutOutput.cpp
    ${CMAKE_SOURCE_DIR}/src/Library/Logging/FileOutput.cpp
    ${CMAKE_SOURCE_DIR}/src/Library/Logging/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/Library/Logging/LoggingLevel.cpp
    ${CMAKE_SOURCE_DIR}/src/Library/Logging/ServiceLogging.cpp
    ${CMAKE_SOURCE_DIR}/src/Library/Logging/TcpOutput.cpp)

set(EXCEPTION_SRC_FILES
    ${CMAKE_SOURCE_DIR}/src/ExceptionHandling/BlazingThread.cpp
    ${CMAKE_SOURCE_DIR}/src/ExceptionHandling/BlazingException.cpp
    ${CMAKE_SOURCE_DIR}/src/ExceptionHandling/BlazingExceptionHolder.cpp)

set(UTIL_SRC_FILES
    ${CMAKE_SOURCE_DIR}/src/Util/StringUtil.cpp
    ${CMAKE_SOURCE_DIR}/src/Util/EncryptionUtil.cpp
    ${CMAKE_SOURCE_DIR}/src/Util/FileUtil.cpp
    ${CMAKE_SOURCE_DIR}/src/Config/BlazingContext.cpp)

if(GCS_SUPPORT)
    add_definitions(-DGCS_SUPPORT)
    find_package(storage_client REQUIRED)
    set(GCS_FILESYSTEM_SRC_FILES
        ${CMAKE_SOURCE_DIR}/src/FileSystem/GoogleCloudStorage.cpp
        ${CMAKE_SOURCE_DIR}/src/FileSystem/private/GoogleCloudStorageReadableFile.cpp
        ${CMAKE_SOURCE_DIR}/src/FileSystem/private/GoogleCloudStorageOutputStream.cpp
        ${CMAKE_SOURCE_DIR}/src/FileSystem/private/GoogleCloudStorage_p.cpp)
    set(GCS_LIBRARY storage_client)
endif()

if(S3_SUPPORT)
    add_definitions(-DS3_SUPPORT)
    set(S3_FILESYSTEM_SRC_FILES
        ${CMAKE_SOURCE_DIR}/src/FileSystem/S3FileSystem.cpp
        ${CMAKE_SOURCE_DIR}/src/FileSystem/private/S3ReadableFile.cpp
        ${CMAKE_SOURCE_DIR}/src/FileSystem/private/S3OutputStream.cpp
        ${CMAKE_SOURCE_DIR}/src/FileSystem/private/S3FileSystem_p.cpp)
    set(S3_LIBRARY aws-cpp-sdk-core aws-cpp-sdk-s3 aws-cpp-sdk-s3-encryption)
endif()

include_directories(blazingdb-io ${CMAKE_SOURCE_DIR}/src $ENV{CONDA_PREFIX}/include "${ARROW_INCLUDE_DIR}")
link_directories($ENV{CONDA_PREFIX}/lib)

add_library(blazingdb-io SHARED
    ${FILESYSTEM_SRC_FILES}
    ${GCS_FILESYSTEM_SRC_FILES}
    ${S3_FILESYSTEM_SRC_FILES}
    ${UTIL_SRC_FILES}
    ${LOGGING_SRC_FILES}
    ${EXCEPTION_SRC_FILES}
)

target_link_libraries(blazingdb-io ${CURL_LIBRARIES} ${GCS_LIBRARY} ${S3_LIBRARY} arrow)

# Install target and headers
install(TARGETS blazingdb-io DESTINATION lib)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/ DESTINATION include/blazingdb/io FILES_MATCHING PATTERN "*.h")

# Tests
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Print the project summary
feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
