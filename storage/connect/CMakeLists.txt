# Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
# Copyright (c) 2021, JINZHUAN Information Technology Co., Ltd.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA

SET(CONNECT_PLUGIN_STATIC  "connect")
SET(CONNECT_PLUGIN_DYNAMIC "connect")

SET(CONNECT_SOURCES
ha_connect.cc connect.cc user_connect.cc mycat.cc
fmdlex.c osutil.c plugutil.c rcmsg.c rcmsg.h
array.cpp blkfil.cpp colblk.cpp csort.cpp
filamap.cpp filamdbf.cpp filamfix.cpp filamgz.cpp filamtxt.cpp
filter.cpp maputil.cpp myutil.cpp plgdbutl.cpp
reldef.cpp tabdos.cpp tabext.cpp tabfix.cpp
table.cpp tabmul.cpp tabsys.cpp tabcol.cpp
tabvir.cpp valblk.cpp value.cpp xindex.cpp xobject.cpp

array.h blkfil.h block.h catalog.h checklvl.h colblk.h connect.h csort.h
engmsg.h filamap.h filamdbf.h filamfix.h filamgz.h filamtxt.h
filter.h global.h ha_connect.h inihandl.h maputil.h msgid.h
mycat.h myutil.h os.h osutil.h plgcnx.h plgdbsem.h preparse.h reldef.h
resource.h tabdos.h tabext.h tabfix.h tabmul.h
tabsys.h tabcol.h tabtbl.h tabvir.h
user_connect.h valblk.h value.h xindex.h xobject.h xtable.h)

#
# Definitions that are shared for all OSes
#
#add_definitions( -DMARIADB -DFORCE_INIT_OF_VARS -Dconnect_EXPORTS)
#add_definitions( -DHUGE_SUPPORT -DGZ_SUPPORT -DPIVOT_SUPPORT )


#
# OS specific C flags, definitions and source files.
#
IF(UNIX)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # Bar: -Wfatal-errors removed (does not present in gcc on solaris10)
  if(WITH_WARNINGS)
    add_definitions(-Wall -Wextra -Wmissing-declarations)
    #message(STATUS "CONNECT: GCC: All warnings enabled")
  else()
    add_definitions(-Wall -Wmissing-declarations)
    add_definitions(-Wno-write-strings)
    add_definitions(-Wno-unused-variable)
    # Bar: -Wno-unused-but-set-variables commented (does not present on sol10)
    # add_definitions(-Wno-unused-but-set-variable)
    add_definitions(-Wno-unused-value)
    add_definitions(-Wno-unused-function)
    add_definitions(-Wno-parentheses)
    #add_definitions(-Wno-missing-declarations)
    # Bar: -Wno-int-to-pointer-cast commended (does not present in gcc on sol10)
    # add_definitions(-Wno-int-to-pointer-cast)
    # Bar: -Wno-narrowing commented (does not present in gcc on solaris10)
    # add_definitions(-Wno-narrowing)

# This switch is for pure C only:
#   add_definitions(-Wno-implicit-function-declaration)
# These switches are for C++ only
#   add_definitions(-Wno-reorder)

    #message(STATUS "CONNECT: GCC: Some warnings disabled")
  endif(WITH_WARNINGS)
  endif()

  add_definitions( -DUNIX -DLINUX -DUBUNTU )

  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fpermissive -fexceptions -fPIC ")
  get_property(inc_dirs DIRECTORY PROPERTY INCLUDE_DIRECTORIES)
  SET(CONNECT_SOURCES ${CONNECT_SOURCES} inihandl.c)
  SET(IPHLPAPI_LIBRARY "")
ELSE(NOT UNIX)
  SET(CONNECT_SOURCES ${CONNECT_SOURCES}
  tabwmi.cpp tabwmi.h tabmac.cpp tabmac.h macutil.cpp macutil.h)
  # Add exception handling to the CONNECT project)
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
  SET(IPHLPAPI_LIBRARY iphlpapi.lib)
ENDIF(UNIX)


#
# VCT: the VEC format might be not supported in future versions
#

#OPTION(CONNECT_WITH_VCT "Compile CONNECT storage engine with VCT support" ON)

#IF(CONNECT_WITH_VCT)
#  SET(CONNECT_SOURCES ${CONNECT_SOURCES} filamvct.cpp tabvct.cpp filamvct.h tabvct.h)
#  add_definitions(-DVCT_SUPPORT)
#ENDIF(CONNECT_WITH_VCT)


#
# XML
#

#OPTION(CONNECT_WITH_LIBXML2 "Compile CONNECT storage engine with LIBXML2 support" ON)

#IF(CONNECT_WITH_LIBXML2)
#  IF(WIN32)
    #
    # NOTE: when switching to static linking of libxml2
    # make sure to define LIBXML_STATIC.
    #
    # Adding some typical places to search in
#    SET(PC_LIBXML_INCLUDE_DIRS
#        C:/libxml2/include
#        C:/libxml/include
#        D:/libxml/include)
#    SET(PC_LIBXML_LIBRARY_DIRS
#        C:/libxml2/lib
#        C:/libxml/lib
#        D:/libxml/lib)
#  ENDIF(WIN32)
#  FIND_PACKAGE(LibXml2)
#  IF (LIBXML2_FOUND)
#    INCLUDE_DIRECTORIES(${LIBXML2_INCLUDE_DIR})
#    SET(XML_LIBRARY  ${LIBXML2_LIBRARIES})
#    SET(CONNECT_SOURCES ${CONNECT_SOURCES} libdoc.cpp libdoc.h)
#    add_definitions(-DLIBXML2_SUPPORT)
#  ENDIF(LIBXML2_FOUND)
#ENDIF(CONNECT_WITH_LIBXML2)


#IF(WIN32)
  # /MP option of the Microsoft compiler does not work well with COM #import
#  string(REPLACE "/MP" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
#  string(REPLACE "/MP" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

#  OPTION(CONNECT_WITH_MSXML "Compile CONNECT storage engine with MSXML support" ON)
#  IF(CONNECT_WITH_MSXML)
#    add_definitions(-DMSX6 -DDOMDOC_SUPPORT)
#    message(STATUS "MSXML library version: msxml6")
#    SET(MSXML_FOUND 1)
#    SET(CONNECT_SOURCES ${CONNECT_SOURCES} domdoc.cpp domdoc.h)
#  ENDIF(CONNECT_WITH_MSXML)
#ENDIF(WIN32)

#IF(LIBXML2_FOUND OR MSXML_FOUND)
#  SET(CONNECT_SOURCES ${CONNECT_SOURCES}
#  tabxml.cpp tabxml.h plgxml.cpp plgxml.h)
#ENDIF()

#
# MySQL is now included unconditionnally
#

IF(NOT UNIX)
  #
  # TODO: remove this
  # change to use "#include "../../include/mysql.h" in the sources.
  INCLUDE_DIRECTORIES("../../include/mysql")
ENDIF(NOT UNIX)


#
# ODBC
#

OPTION(CONNECT_WITH_ODBC "Compile CONNECT storage engine with ODBC support" ON)

IF(CONNECT_WITH_ODBC)
  if(UNIX)
    # Note, we currently detect unixODBC only on Linux.
    # TODO: detect iODBC as well. Simply adding "iodbc" into NAMES in
    # find_library does not work on machines with both unixODBC and iODBC
    # installed, because it finds headers from unixODBC while libraries
    # from iODBC. We could search for 'isql.h' instead of 'sql.h' so
    # the library 'libodbc' gets compiled with 'isql.h' and
    # the library 'libiodbc' gets compiled with 'sql'h.
    # This will also need changes in the sources (e.g. #include <isql.h>).

    find_path(ODBC_INCLUDE_DIR sql.h
    /usr/include
    /usr/include/odbc
    /usr/local/include
    /usr/local/include/odbc
    /usr/local/odbc/include
    #"C:/Program Files/ODBC/include"
    #"C:/Program Files/Microsoft SDKs/Windows/v7.0A/include"
    #"C:/Program Files/Microsoft SDKs/Windows/v6.0a/include"
    #"C:/Program Files (x86)/Microsoft SDKs/Windows/v7.0A/include"
    DOC "Specify the directory containing sql.h."
    )

    find_library(ODBC_LIBRARY
    NAMES odbc odbcinst odbc32
    PATHS
    /usr/lib
    /usr/lib/odbc
    /usr/local/lib
    /usr/local/lib/odbc
    /usr/local/odbc/lib
    #"C:/Program Files/ODBC/lib"
    #"C:/ODBC/lib/debug"
    #"C:/Program Files/Microsoft SDKs/Windows/v7.0A/Lib"
    #"C:/Program Files/Microsoft SDKs/Windows/v6.0A/Lib"
    #"C:/Program Files (x86)/Microsoft SDKs/Windows/v7.0A/Lib"
    DOC "Specify the ODBC driver manager library here."
    )

    mark_as_advanced(ODBC_LIBRARY ODBC_INCLUDE_DIR)
    IF(ODBC_INCLUDE_DIR AND ODBC_LIBRARY)
      set(CMAKE_REQUIRED_LIBRARIES ${ODBC_LIBRARY})
      set(CMAKE_REQUIRED_INCLUDES ${ODBC_INCLUDE_DIR})
      CHECK_CXX_SOURCE_COMPILES(
"
#include <stdio.h>
#include <sql.h>
#include <sqlext.h>
//typedef long BOOL;   /* this fails with iODBC */
int main() {
  SQLULEN rowofs= 0; /* this fails on older unixODBC */
  SQLExtendedFetch(NULL, 0, 0, &rowofs, NULL);
  return 0;
}
" ODBC_OK)
    ENDIF()

    IF(ODBC_OK)
      INCLUDE_DIRECTORIES(${ODBC_INCLUDE_DIR})
      add_definitions(-DODBC_SUPPORT)
      SET(CONNECT_SOURCES ${CONNECT_SOURCES} tabodbc.cpp odbconn.cpp)
    ELSE()
      SET(ODBC_LIBRARY "")
    ENDIF()
  ELSE(NOT UNIX)
    add_definitions(-DODBC_SUPPORT)
    SET(ODBC_LIBRARY odbc32.lib odbccp32.lib)
    SET(CONNECT_SOURCES ${CONNECT_SOURCES}
    tabodbc.cpp tabodbc.h odbccat.h odbconn.cpp odbconn.h)
  ENDIF(UNIX)
ENDIF(CONNECT_WITH_ODBC)

#
# JDBC
#
#IF(APPLE)
#  OPTION(CONNECT_WITH_JDBC "Compile CONNECT storage engine without JDBC support" OFF)
#ELSE()
#  OPTION(CONNECT_WITH_JDBC "Compile CONNECT storage engine with JDBC support" ON)
#ENDIF()

#IF(CONNECT_WITH_JDBC)
#  FIND_PACKAGE(Java 1.6)
#  FIND_PACKAGE(JNI)
#  IF (JAVA_FOUND AND JNI_FOUND)
#    INCLUDE(UseJava)
#    INCLUDE_DIRECTORIES(${JAVA_INCLUDE_PATH})
#    INCLUDE_DIRECTORIES(${JAVA_INCLUDE_PATH2})
    # SET(JDBC_LIBRARY ${JAVA_JVM_LIBRARY}) will be dynamically linked
#    SET(CONNECT_SOURCES ${CONNECT_SOURCES}
#    jdbconn.cpp tabjdbc.cpp jdbconn.h tabjdbc.h jdbccat.h
#    JdbcInterface.java ApacheInterface.java MariadbInterface.java
#    MysqlInterface.java OracleInterface.java PostgresqlInterface.java
#    JavaWrappers.jar)
    # TODO: Find how to compile and install the java wrapper classes
    # Find required libraries and include directories
#    SET (JAVA_SOURCES JdbcInterface.java)
#    add_jar(JdbcInterface ${JAVA_SOURCES})
#    install_jar(JdbcInterface DESTINATION ${INSTALL_PLUGINDIR} COMPONENT connect-engine)
#    INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/JavaWrappers.jar
#      DESTINATION ${INSTALL_PLUGINDIR} COMPONENT connect-engine)
#    add_definitions(-DJDBC_SUPPORT)
#  ELSE()
#    SET(JDBC_LIBRARY "")
#  ENDIF()
#ENDIF(CONNECT_WITH_JDBC)

#
# ZIP
#

#OPTION(CONNECT_WITH_ZIP "Compile CONNECT storage engine with ZIP support" ON)

#IF(CONNECT_WITH_ZIP)
#  SET(CONNECT_SOURCES ${CONNECT_SOURCES} filamzip.cpp tabzip.cpp unzip.c ioapi.c zip.c
#    filamzip.h tabzip.h ioapi.h unzip.h zip.h)
#  add_definitions(-DZIP_SUPPORT -DNOCRYPT)
#ENDIF(CONNECT_WITH_ZIP)


#
# XMAP
#

#OPTION(CONNECT_WITH_XMAP "Compile CONNECT storage engine with index file mapping support" ON)

#IF(CONNECT_WITH_XMAP)
#  add_definitions(-DXMAP)
#ENDIF(CONNECT_WITH_XMAP)

#
# Plugin definition
#

MYSQL_ADD_PLUGIN(connect ${CONNECT_SOURCES}
  STORAGE_ENGINE
  MODULE_ONLY
  LINK_LIBRARIES ${ZLIB_LIBRARY} ${XML_LIBRARY} ${ICONV_LIBRARY}
                 ${ODBC_LIBRARY} ${JDBC_LIBRARY} ${IPHLPAPI_LIBRARY})

