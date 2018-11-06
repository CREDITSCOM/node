# Distributed under the OSI-approved BSD 3-Clause License.
# Copyright 2000-2018 Kitware, Inc. and Contributors
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# 
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
# 
# * Neither the name of Kitware, Inc. nor the names of Contributors
#   may be used to endorse or promote products derived from this
#   software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# 
# ------------------------------------------------------------------------------
# 
# The following individuals and institutions are among the Contributors:
# 
# * Aaron C. Meadows <cmake@shadowguarddev.com>
# * Adriaan de Groot <groot@kde.org>
# * Aleksey Avdeev <solo@altlinux.ru>
# * Alexander Neundorf <neundorf@kde.org>
# * Alexander Smorkalov <alexander.smorkalov@itseez.com>
# * Alexey Sokolov <sokolov@google.com>
# * Alex Turbov <i.zaufi@gmail.com>
# * Andreas Pakulat <apaku@gmx.de>
# * Andreas Schneider <asn@cryptomilk.org>
# * André Rigland Brodtkorb <Andre.Brodtkorb@ifi.uio.no>
# * Axel Huebl, Helmholtz-Zentrum Dresden - Rossendorf
# * Benjamin Eikel
# * Bjoern Ricks <bjoern.ricks@gmail.com>
# * Brad Hards <bradh@kde.org>
# * Christopher Harvey
# * Christoph Grüninger <foss@grueninger.de>
# * Clement Creusot <creusot@cs.york.ac.uk>
# * Daniel Blezek <blezek@gmail.com>
# * Daniel Pfeifer <daniel@pfeifer-mail.de>
# * Enrico Scholz <enrico.scholz@informatik.tu-chemnitz.de>
# * Eran Ifrah <eran.ifrah@gmail.com>
# * Esben Mose Hansen, Ange Optimization ApS
# * Geoffrey Viola <geoffrey.viola@asirobots.com>
# * Google Inc
# * Gregor Jasny
# * Helio Chissini de Castro <helio@kde.org>
# * Ilya Lavrenov <ilya.lavrenov@itseez.com>
# * Insight Software Consortium <insightsoftwareconsortium.org>
# * Jan Woetzel
# * Kelly Thompson <kgt@lanl.gov>
# * Konstantin Podsvirov <konstantin@podsvirov.pro>
# * Mario Bensi <mbensi@ipsquad.net>
# * Mathieu Malaterre <mathieu.malaterre@gmail.com>
# * Matthaeus G. Chajdas
# * Matthias Kretz <kretz@kde.org>
# * Matthias Maennich <matthias@maennich.net>
# * Michael Stürmer
# * Miguel A. Figueroa-Villanueva
# * Mike Jackson
# * Mike McQuaid <mike@mikemcquaid.com>
# * Nicolas Bock <nicolasbock@gmail.com>
# * Nicolas Despres <nicolas.despres@gmail.com>
# * Nikita Krupen'ko <krnekit@gmail.com>
# * NVIDIA Corporation <www.nvidia.com>
# * OpenGamma Ltd. <opengamma.com>
# * Per Øyvind Karlsen <peroyvind@mandriva.org>
# * Peter Collingbourne <peter@pcc.me.uk>
# * Petr Gotthard <gotthard@honeywell.com>
# * Philip Lowman <philip@yhbt.com>
# * Philippe Proulx <pproulx@efficios.com>
# * Raffi Enficiaud, Max Planck Society
# * Raumfeld <raumfeld.com>
# * Roger Leigh <rleigh@codelibre.net>
# * Rolf Eike Beer <eike@sf-mail.de>
# * Roman Donchenko <roman.donchenko@itseez.com>
# * Roman Kharitonov <roman.kharitonov@itseez.com>
# * Ruslan Baratov
# * Sebastian Holtermann <sebholt@xwmw.org>
# * Stephen Kelly <steveire@gmail.com>
# * Sylvain Joubert <joubert.sy@gmail.com>
# * Thomas Sondergaard <ts@medical-insight.com>
# * Tobias Hunger <tobias.hunger@qt.io>
# * Todd Gamblin <tgamblin@llnl.gov>
# * Tristan Carel
# * University of Dundee
# * Vadim Zhukov
# * Will Dicharry <wdicharry@stellarscience.com>
# 
# See version control history for details of individual contributions.
# 
# The above copyright and license notice applies to distributions of
# CMake in source and binary form.  Third-party software packages supplied
# with CMake under compatible licenses provide their own copyright notices
# documented in corresponding subdirectories or source files.
# 
# ------------------------------------------------------------------------------
# 
# CMake was initially developed by Kitware with the following sponsorship:
# 
#  * National Library of Medicine at the National Institutes of Health
#    as part of the Insight Segmentation and Registration Toolkit (ITK).
# 
#  * US National Labs (Los Alamos, Livermore, Sandia) ASC Parallel
#    Visualization Initiative.
# 
#  * National Alliance for Medical Image Computing (NAMIC) is funded by the
#    National Institutes of Health through the NIH Roadmap for Medical Research,
#    Grant U54 EB005149.
# 
#  * Kitware, Inc.

#.rst:
# FindGMock
# ---------
#
# Locate the Google C++ Mocking Framework.
# (This is nearly a copy of FindGTest.cmake as provided with cmake-3.10.2)
#
# Imported targets
# ^^^^^^^^^^^^^^^^
#
# This module defines the following :prop_tgt:`IMPORTED` targets:
#
# ``GMock::GMock``
#   The Google Mock ``gmock`` library, if found
# ``GMock::Main``
#   The Google Mock ``gmock_main`` library, if found
#
#
# Result variables
# ^^^^^^^^^^^^^^^^
#
# This module will set the following variables in your project:
#
# ``GMOCK_FOUND``
#   Found the Google Mocking framework
# ``GMOCK_INCLUDE_DIRS``
#   the directory containing the Google Mock headers
#
# The library variables below are set as normal variables.  These
# contain debug/optimized keywords when a debugging library is found.
#
# ``GMOCK_LIBRARIES``
#   The Google Mock ``gmock`` library; note it also requires linking
#   with an appropriate thread library
# ``GMOCK_MAIN_LIBRARIES``
#   The Google Mock ``gmock_main`` library
# ``GMOCK_BOTH_LIBRARIES``
#   Both ``gmock`` and ``gmock_main``
#
# Cache variables
# ^^^^^^^^^^^^^^^
#
# The following cache variables may also be set:
#
# ``GMOCK_ROOT``
#   The root directory of the Google Mock installation (may also be
#   set as an environment variable)
# ``GMOCK_MSVC_SEARCH``
#   If compiling with MSVC, this variable can be set to ``MT`` or
#   ``MD`` (the default) to enable searching a GMock build tree
#
#
# Example usage
# ^^^^^^^^^^^^^
#
# ::
#
#     enable_testing()
#     find_package(GMock REQUIRED)
#
#     add_executable(foo foo.cc)
#     target_link_libraries(foo GMock::GMock GMock::Main)
#
#     add_test(AllTestsInFoo foo)

cmake_minimum_required(VERSION 3.5)
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_LIST_DIR}")

function(__gmock_append_debugs _endvar _library)
    if(${_library} AND ${_library}_DEBUG)
        set(_output optimized ${${_library}} debug ${${_library}_DEBUG})
    else()
        set(_output ${${_library}})
    endif()
    set(${_endvar} ${_output} PARENT_SCOPE)
endfunction()

function(__gmock_find_library _name)
    find_library(${_name}
        NAMES ${ARGN}
        HINTS
            ENV GMOCK_ROOT
            ${GMOCK_ROOT}
        PATH_SUFFIXES ${_gmock_libpath_suffixes}
    )
    mark_as_advanced(${_name})
endfunction()

macro(__gmock_determine_windows_library_type _var)
    if(EXISTS "${${_var}}")
        file(TO_NATIVE_PATH "${${_var}}" _lib_path)
        get_filename_component(_name "${${_var}}" NAME_WE)
        file(STRINGS "${${_var}}" _match REGEX "${_name}\\.dll" LIMIT_COUNT 1)
        if(NOT _match STREQUAL "")
            set(${_var}_TYPE SHARED PARENT_SCOPE)
        else()
            set(${_var}_TYPE UNKNOWN PARENT_SCOPE)
        endif()
        return()
    endif()
endmacro()

function(__gmock_determine_library_type _var)
    if(WIN32)
        # For now, at least, only Windows really needs to know the library type
        __gmock_determine_windows_library_type(${_var})
        __gmock_determine_windows_library_type(${_var}_RELEASE)
        __gmock_determine_windows_library_type(${_var}_DEBUG)
    endif()
    # If we get here, no determination was made from the above checks
    set(${_var}_TYPE UNKNOWN PARENT_SCOPE)
endfunction()

function(__gmock_import_library _target _var _config)
    if(_config)
        set(_config_suffix "_${_config}")
    else()
        set(_config_suffix "")
    endif()

    set(_lib "${${_var}${_config_suffix}}")
    if(EXISTS "${_lib}")
        if(_config)
            set_property(TARGET ${_target} APPEND PROPERTY
                IMPORTED_CONFIGURATIONS ${_config})
        endif()
        set_target_properties(${_target} PROPERTIES
            IMPORTED_LINK_INTERFACE_LANGUAGES${_config_suffix} "CXX")
        if(WIN32 AND ${_var}_TYPE STREQUAL SHARED)
            set_target_properties(${_target} PROPERTIES
                IMPORTED_IMPLIB${_config_suffix} "${_lib}")
        else()
            set_target_properties(${_target} PROPERTIES
                IMPORTED_LOCATION${_config_suffix} "${_lib}")
        endif()
    endif()
endfunction()

#

if(NOT DEFINED GMOCK_MSVC_SEARCH)
    set(GMOCK_MSVC_SEARCH MD)
endif()

set(_gmock_libpath_suffixes lib)
if(MSVC)
    if(GMOCK_MSVC_SEARCH STREQUAL "MD")
        list(APPEND _gmock_libpath_suffixes
            msvc/gmock-md/Debug
            msvc/gmock-md/Release
            msvc/x64/Debug
            msvc/x64/Release
            )
    elseif(GMOCK_MSVC_SEARCH STREQUAL "MT")
        list(APPEND _gmock_libpath_suffixes
            msvc/gmock/Debug
            msvc/gmock/Release
            msvc/x64/Debug
            msvc/x64/Release
            )
    endif()
endif()


find_path(GMOCK_INCLUDE_DIR gmock/gmock.h
    HINTS
        $ENV{GMOCK_ROOT}/include
        ${GMOCK_ROOT}/include
)
mark_as_advanced(GMOCK_INCLUDE_DIR)

if(MSVC AND GMOCK_MSVC_SEARCH STREQUAL "MD")
    # The provided /MD project files for Google Mock add -md suffixes to the
    # library names.
    __gmock_find_library(GMOCK_LIBRARY            gmock-md  gmock)
    __gmock_find_library(GMOCK_LIBRARY_DEBUG      gmock-mdd gmockd)
    __gmock_find_library(GMOCK_MAIN_LIBRARY       gmock_main-md  gmock_main)
    __gmock_find_library(GMOCK_MAIN_LIBRARY_DEBUG gmock_main-mdd gmock_maind)
else()
    __gmock_find_library(GMOCK_LIBRARY            gmock)
    __gmock_find_library(GMOCK_LIBRARY_DEBUG      gmockd)
    __gmock_find_library(GMOCK_MAIN_LIBRARY       gmock_main)
    __gmock_find_library(GMOCK_MAIN_LIBRARY_DEBUG gmock_maind)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GMock DEFAULT_MSG GMOCK_LIBRARY GMOCK_INCLUDE_DIR GMOCK_MAIN_LIBRARY)

if(GMOCK_FOUND)
    set(GMOCK_INCLUDE_DIRS ${GMOCK_INCLUDE_DIR})
    __gmock_append_debugs(GMOCK_LIBRARIES      GMOCK_LIBRARY)
    __gmock_append_debugs(GMOCK_MAIN_LIBRARIES GMOCK_MAIN_LIBRARY)
    set(GMOCK_BOTH_LIBRARIES ${GMOCK_LIBRARIES} ${GMOCK_MAIN_LIBRARIES})

    if(NOT TARGET GMock::GMock)
        __gmock_determine_library_type(GMOCK_LIBRARY)
        add_library(GMock::GMock ${GMOCK_LIBRARY_TYPE} IMPORTED)
        if(GMOCK_INCLUDE_DIRS)
            set_target_properties(GMock::GMock PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${GMOCK_INCLUDE_DIRS}")
        endif()
        __gmock_import_library(GMock::GMock GMOCK_LIBRARY "")
        __gmock_import_library(GMock::GMock GMOCK_LIBRARY "RELEASE")
        __gmock_import_library(GMock::GMock GMOCK_LIBRARY "DEBUG")
    endif()
    if(NOT TARGET GMock::Main)
        __gmock_determine_library_type(GMOCK_MAIN_LIBRARY)
        add_library(GMock::Main ${GMOCK_MAIN_LIBRARY_TYPE} IMPORTED)
        set_target_properties(GMock::Main PROPERTIES
            INTERFACE_LINK_LIBRARIES "GMock::GMock")
        __gmock_import_library(GMock::Main GMOCK_MAIN_LIBRARY "")
        __gmock_import_library(GMock::Main GMOCK_MAIN_LIBRARY "RELEASE")
        __gmock_import_library(GMock::Main GMOCK_MAIN_LIBRARY "DEBUG")
    endif()
endif()
