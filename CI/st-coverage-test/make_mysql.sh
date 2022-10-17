#!/bin/sh
# 
#
# USAGE: Switch to the 'BUILD' directory, execute the command as follow
#           chmod +x make_mysql.sh
#           ./make_mysql.sh
#        Built installation package is 'mysql64_linux.tar.gz' in the current directory
#

# Begin of script

# set +x

#######################################################
# Define function here
#######################################################



# Print message on terminal and log 
message()
{
   if [ $# -gt 0 ]; then
        systime=$(date|awk '{printf "%s%s %s",$2,$3,$4}') > /dev/null
        echo "$systime $*"
        echo "$systime $*" >> "$log_file_path" 
   fi
}

# show usage of install script 
usage()
{ 
    echo "make_mysql.sh [options] [--] optstring parameters"
    echo "  -b,--bit    bit to build, 32/64, default from getconf -a|grep LONG_BIT"
    echo "  -h,--help    show usage"         
}

# get option
get_option()
{
    message "get build option"
    ARGS=`getopt -o b:h -l bit:,help -- "$@"`
    if [ "$?" -ne 0 ]; then
        message "option error,exit"
    usage
        exit 1
    fi
    eval set -- "${ARGS}"

    while true ; do
        case "$1" in         
            -b|--bit)
                bit_db="$2" ; shift ;;  
            -h|--help)
                usage ; exit 0 ;;
            --)
                shift ; break ;;
            *) 
                message "option error! exit!"
                exit 1 ;;
        esac 
        shift  
    done
  
    if [ "$bit_db" = "" -o \( "$bit_db" != "32" -a $"bit_db" != "64" \) ];then
        bit_db=$match_bit
    fi
    if [ "$bit_db" = "32" ] ; then
        mysql_pkg=mysql32_linux
        me_file_path="$HOME/mysql_linux.me"
    elif [ "$match_bit" = "32" ]
    then
        message "environment not support 64 bit! exit!"
        exit 1 
    fi
  
    message "bit_db : $bit_db"
    message "match_bit : $match_bit"     
}


# get operating system type
get_os_type()
{
    message "get os type"
    
    OS_TYPE=`uname`
    
    case ${OS_TYPE} in
        HP-UX)
            OS_TYPE=hpux
            ;;
        AIX)
            OS_TYPE=aix
            ;;
        SunOS)
            OS_TYPE=sunos
            ;;
        Linux)
            OS_TYPE=linux
            ;;
        *)
            OS_TYPE=un-supported
            ;;
    esac

    # maybe a un-known unix.
    if [ $OS_TYPE = "un-supported" ] ; then
        message "unsupport os. exit."
        exit 2
    fi
    
    os_type=$OS_TYPE
    
    if [ $os_type = "linux" ] ; then
        if [ -s "/etc/SuSE-release" ] ; then
            os_type="suselinux"
        elif [ -s "/etc/cgsl-release" ] ; then
            os_type="cgsllinux"
        elif [ -s "/etc/redhat-release" ] ; then
            os_type="redhatlinux"
        elif [ -s "/etc/os-release" -a -s "/etc/lsb-release" ] ; then
            os_type="ubuntu"  
        else
            message "unsupport os. exit."
            exit 2
        fi
    fi
    message "os:$os_type"
}


# Check whether command exists
is_cmd()
{
    #command -v foo >/dev/null 2>&1 || { echo >&2 "I require foo but it's not installed.  Aborting.";  }
    #type foo >/dev/null 2>&1 || { echo >&2 "I require foo but it's not installed.  Aborting.";  }
    #hash foo 2>/dev/null || { echo >&2 "I require foo but it's not installed.  Aborting.";  }
    if [ "$#" -ne 1 ]; then
        return 1
    fi
    
    command -v $1 >/dev/null 2>&1 || return 1
    
    return 0
}

# Check whether this build script is in correct directory.
# The correct directory is as below:
# DB_src_root/
#            CMakeLists.txt
#            sql/
#               sql_yacc.yy
#            build_script_dir/
#                            make_mysql.sh
#            ...
check_build_script_dir()
{
    local build_script_dir=$work_dir
    local err_msg="Please put this build script under your folder in DB source root."

    if [ "$build_script_dir" = "/" ]; then
        echo $err_msg
        exit 1
    fi

    local cmake_file="$build_script_dir/../CMakeLists.txt"
    if [ ! -f "$cmake_file" ]; then
        echo "File $cmake_file not exists."
        echo $err_msg
        exit 1
    fi

    local sql_yacc_file="$build_script_dir/../sql/sql_yacc.yy"
    if [ ! -f "$sql_yacc_file" ]; then
        echo "File $sql_yacc_file not exists."
        echo $err_msg
        exit 1
    fi
}

# Prepare for make
prepare()
{    
    is_cmd cmake
    if [ "$?" -ne 0 ]; then
        message "cmake not exist, build exit"
        exit 1
    fi

    check_build_script_dir

    # clean the files(that is, cmake cache) under work dir
    echo "Enter into $work_dir ..."
    cd $work_dir
    echo "Clean files under $work_dir ..."
    ls | grep -v make_mysql.sh | xargs rm -rf

    rm -f ${mysql_pkg}.tar.gz
    rm -f ${comm_pkg}.tar.gz
        
    if [ -d "$build_target_dir/bin" ];then
        rm -rf $build_target_dir/bin
    fi
    if [ -d "$build_target_dir/lib" ];then
        rm -rf $build_target_dir/lib
    fi
    if [ -d "$build_target_dir/share" ];then
        rm -rf $build_target_dir/share
    fi    
    if [ -d "$build_target_dir/log" ];then
        rm -rf $build_target_dir/log
    fi
    
    mkdir -p $build_target_dir/lib
    mkdir -p $build_target_dir/bin 
    mkdir -p $build_target_dir/log
    mkdir -p $build_target_dir/share
    
    #prepare_jemalloc

}

prepare_jemalloc()
{
    if [ "$bit_db" = "64" ]; then
        cp -f $work_dir/../extra/jemalloc/configure64 $work_dir/../extra/jemalloc/configure
    else
        cp -f $work_dir/../extra/jemalloc/configure32 $work_dir/../extra/jemalloc/configure
    fi
    
    cd $work_dir/../extra/jemalloc
    chmod +x autogen.sh config.guess config.sub configure install-sh ./bin/pprof ./include/msvc_compat ./include/jemalloc/internal/size_classes.sh 
    
    cd $work_dir
    
}


# Do make
do_make()
{
    # cmake
    if [ "$bit_db" = "32" -a "$match_bit" = "64" ]; then
        message "32 bit cmake"
        if [ "$os_type" = "ubuntu" ]; then
            cmake .. \
                -DCMAKE_C_FLAGS="-m32" \
                -DCMAKE_CXX_FLAGS="-m32 -felide-constructors" \
                -DCMAKE_INSTALL_PREFIX=$build_target_dir \
                -DWITH_EXTRA_CHARSETS:STRING=all \
                -DWITH_EMBEDDED_SERVER:BOOL=OFF \
                -DENABLED_LOCAL_INFILE=1 \
                -DWITH_MYISAM_STORAGE_ENGINE=1 \
                -DWITH_INNOBASE_STORAGE_ENGINE=1 \
                -DCURSES_LIBRARY=/usr/lib32/libncurses.so \
                -DCURSES_INCLUDE_PATH=/usr/include \
                -DWITH_DEBUG=1 \
                -DENABLE_GCOV=1   2>&1 | tee -a $me_file_path
            if [ "$?" -ne 0 ]; then
                message "cmake return value is not 0, build exit"
                exit 1
            fi
        else
            cmake .. \
                -DCMAKE_C_FLAGS="-m32" \
                -DCMAKE_CXX_FLAGS="-m32 -felide-constructors" \
                -DCMAKE_INSTALL_PREFIX=$build_target_dir \
                -DWITH_EXTRA_CHARSETS:STRING=all \
                -DWITH_EMBEDDED_SERVER:BOOL=OFF \
                -DENABLED_LOCAL_INFILE=1 \
                -DWITH_MYISAM_STORAGE_ENGINE=1 \
                -DWITH_INNOBASE_STORAGE_ENGINE=1 \
                -DWITH_DEBUG=1 \
                -DENABLE_GCOV=1   2>&1 | tee -a $me_file_path
            if [ "$?" -ne 0 ]; then
                message "cmake return value is not 0, build exit"
                exit 1
            fi
        fi
    else
        message "normal cmake"
        cmake .. \
            -DCMAKE_INSTALL_PREFIX=$build_target_dir \
            -DWITH_EXTRA_CHARSETS:STRING=all \
            -DDOWNLOAD_BOOST=1\
            -DWITH_BOOST=$work_dir/../boost \
            -DWITH_EMBEDDED_SERVER:BOOL=OFF \
            -DWITH_SSL=system \
            -DENABLED_LOCAL_INFILE=1 \
            -DWITH_MYISAM_STORAGE_ENGINE=1 \
            -DWITH_INNOBASE_STORAGE_ENGINE=1 \
            -DWITH_DEBUG=1 \
            -DENABLE_GCOV=1   2>&1 | tee -a $me_file_path
        if [ "$?" -ne 0 ]; then
            message "cmake return value is not 0, build exit"
            exit 1
        fi
    fi
    message "Make : cmake finished"
    
    # make 
    make -j16 && make install  2>&1 | tee -a $me_file_path
    if [ "$?" -ne 0 ]; then
        message "cmake return value is not 0, build exit"
        exit 1
    fi
    message "Make : make finished"
    

}

#######################################################
# Do package
do_pkg()
{    
    cd $build_target_dir

    cp -f $work_dir/../ChangeLog.txt $build_target_dir/ChangeLog.txt

    cp -f $work_dir/../COPYING $build_target_dir/COPYING
    cp -f $work_dir/../support-files/mysql.server.sh $build_target_dir/bin/mysql.server
    cp -f $work_dir/../support-files/gdbpd.sh $build_target_dir/bin/gdbpd
    
    if [ "$os_type" = "redhatlinux" ]; then
        cp -f $work_dir/../lib/redhat/libtcmalloc.so $build_target_dir/lib/
        cp -f $work_dir/../lib/redhat/libstdc++.so.6.0.21 $build_target_dir/lib/
        cd $build_target_dir/lib/
        ln -s libstdc++.so.6.0.21 libstdc++.so.6
        cd -
    elif [ "$os_type" = "suselinux" ]; then
        cp -f $work_dir/../lib/suse/libtcmalloc.so $build_target_dir/lib/
    elif [ "$os_type" = "cgsllinux" ]; then
        cp -f $work_dir/../lib/cgsl/libtcmalloc.so $build_target_dir/lib/
    fi
    
    mkdir -p $build_target_dir/log
    
    # copy config
    mkdir -p $build_target_dir/etc
    cp -f $work_dir/../etc/* $build_target_dir/etc
    #remove unnecessary
    #rm -rf  COPYING* CREDITS data/ docs/ EXCEPTIONS-CLIENT INSTALL-BINARY man mysql-test README  sql-bench support-files
        
    # modify permissions
    chmod 755 bin/*    > /dev/null 2>&1
    chmod 755 lib/*    > /dev/null 2>&1
    chmod 644 etc/*    > /dev/null 2>&1
    
    # package 
    tar czf $mysql_pkg.tar.gz bin lib etc share log ChangeLog.txt COPYING  > /dev/null
    chmod 777 ${mysql_pkg}.tar.gz
    
    tar czf $comm_pkg.tar.gz lib include >/dev/null
    chmod 777 ${comm_pkg}.tar.gz
    
    cp -f ${mysql_pkg}.tar.gz $HOME
    cp -f ${comm_pkg}.tar.gz $HOME
    
    if [ -s "${mysql_pkg}.tar.gz" ]; then
        message "Make : package finished"
    else
        message "Make : package failed"
        exit 1
    fi
    
    if [ -s "${comm_pkg}.tar.gz" ]; then
        message "Make : package commlib finished"
    else
        message "Make : package commlib failed"
        exit 1
    fi

}


################################################################################
#  main
################################################################################

work_dir=$(cd `dirname $0`; pwd)

if [ -d "$work_dir/package" ];then
    rm -rf $work_dir/package
fi
mkdir $work_dir/package

build_target_dir=$work_dir/package
mysql_pkg=mysql64_linux
comm_pkg=mysql_commlib
log_file_path="$HOME/mysqlmake.log"
me_file_path="$HOME/mysql64_linux.me"
bit_db=""
match_bit=$(getconf -a|grep LONG_BIT|awk '{print $2}')

echo "Make build installation package"
message "Make : Started"

# get option
get_option "$@" # note : use $@ instead oft $*

get_os_type

prepare

do_make

do_pkg

message "Make : Completed"
echo "Make build package successfully"

# End of script
