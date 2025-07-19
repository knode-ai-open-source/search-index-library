# SPDX-FileCopyrightText: 2023-2025 Andy Curtis <contactandyc@gmail.com>
# SPDX-FileCopyrightText: 2024-2025 Knode.ai
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.10)
project(search_builder_example VERSION 1.0)

# Enable testing
enable_testing()

# Define the library to test
set(LIB_TO_TEST search-index-library)

# Set the directory for test sources
set(TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/test_document_builder.c)

set(CUSTOM_PACKAGES a-memory-library a-tokenizer-library the-macro-library the-lz4-library the-io-library)
set(THIRD_PARTY_PACKAGES ZLIB)

find_package(a-cmake-library REQUIRED)

include(BinaryConfig)
