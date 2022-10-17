#!/bin/bash
#
# SCRIPT: manual_install.sh
# AUTHOR: linzhoukai
# DATE:2014.12.19
# REV: 1.00
# PLATFORM: Red Hat Enterprise Linux Server release 6.5
# PURPOSE: Install 

# Begin of script

#set +x

#######################################################
# Define function here
#######################################################

# write install msg to log
log()
{
    if [ -n "$1" ] ; then
        echo "$*"
        datetime=$(date | awk '{printf "%s %s %s",$2,$3,$4}') > /dev/null
        echo "$datetime $*" > /dev/null >> "$log_file"
    fi
}

#return fail
return_fail()
{
    echo "$*" >> "$log_file"
    echo "$fail_msg" "$*"
    exit 1
}

# show usage of install script
usage()
{   echo "  manual_install.sh [options] [--] optstring parameters"
    echo "  -i,--instance_id      the instance_id you want to install,default 1"
    echo "  -c,--config           mysql configuration parameters,like bind_address=127.0.0.1,port=5518 and so on"
    echo "  -b,--bit              bit of mysql to install, 32/64, default from getconf -a|grep LONG_BIT"
    echo "  -n,--not_decompress   if you have decompressed the tar.gz file yourself, then use this option, default not"
    echo "  -s,--semi             open Semi-Synchronous configs, default not"
    echo "  -r,--run              startup mysql after install, default do not startup"
    echo "  -h,--help             show usage of install"
}

get_os_type()
{
    msg="get os type" ; log "$msg"

    OS_TYPE=`uname`

    case ${OS_TYPE} in
        HP-UX)
            OS_TYPE=hpux
            ;;
        AIX)
            OS_TYPE=aix
            UNIXUNZIP="uncompress -f"
            ;;
        SunOS)
            OS_TYPE=sunos
            UNIXAWK=/usr/xpg4/bin/awk
            UNIXGREP=/usr/xpg4/bin/grep
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
        msg="unsupport os. exit."
        return_fail "$msg"
    fi

    os_type=$OS_TYPE

    if [ $os_type = "linux" ] ; then
        if [ -s "/etc/SuSE-release" ] ; then
            os_type="suselinux"
        elif [ -s "/etc/centos-release" ] ; then
            os_type="redhatlinux"
        elif [ -s "/etc/redhat-release" ] ; then
            os_type="redhatlinux"
        elif [ -s "/etc/cgsl-release" ] ; then
            os_type="cgsllinux"
        else
            msg="unsupport os. exit."
            return_fail "$msg"
        fi
    fi

    if [ "$bit_db" != "64" -a "$bit_db" != "32" ] ; then
        bit_db=$(getconf -a|grep LONG_BIT|awk '{print $2}')
    fi

    msg="os:$os_type, bit:$bit_db"
    log "$msg"
}

check_profile()
{
    msg="Checking user profile ..."
    log "$msg"
    if [ -f $HOME/.bash_profile ]
    then
        _user_profile=$HOME/.bash_profile
    elif [ -f $HOME/.profile ]
    then
        _user_profile=$HOME/.profile
    else
        touch $HOME/.bash_profile
        _user_profile=$HOME/.bash_profile
    fi

    ${UNIXGREP} -q 'HOME=' ${_user_profile} || \
    echo "HOME=${HOME};export HOME">>${_user_profile}

    ${UNIXGREP} -q 'export PATH=\$HOME/bin:/usr/local/bin:\$PATH'  ${_user_profile} || \
    echo "export PATH=\$HOME/bin:/usr/local/bin:\$PATH">>${_user_profile}

    ${UNIXGREP} -q SHLIB_PATH ${_user_profile} || \
    echo "export SHLIB_PATH=\$HOME/lib:/usr/lib">>${_user_profile}

    ${UNIXGREP} -q LIBPATH ${_user_profile} || \
    echo "export LIBPATH=\$HOME/lib">>${_user_profile}

    ${UNIXGREP} -q LD_LIBRARY_PATH ${_user_profile} || \
    echo "export LD_LIBRARY_PATH=\$HOME/lib">>${_user_profile}

    ${UNIXGREP} -q LANG ${_user_profile} || \
    echo "export LANG=C" >>${_user_profile}

    chmod 755 ${_user_profile}

  . ${_user_profile}
}

# get options
get_options()
{
    msg="getting  options" ; log "$msg"

    ARGS=`getopt -o i:c:b:nsrh -l instance_id:,config:,bit:,not_decompress,semi,run,help -- "$@"`

    if [ "$?" -ne 0 ]; then
        msg="option error! exit!"
        echo "$msg"; log "$msg" ; echo "$fail_msg"
        exit 1
    fi

    eval set -- "${ARGS}"

    while true ; do
        case "$1" in
            -i|--instance_id)
                INSTANCE_ID="$2" ; shift ;;
            -c|--config)
                configs="$2" ; shift ;;
            -b|--bit)
                bit_db="$2" ; shift ;;
            -n|--not_decompress)
                need_decompress="no" ;;
            -s|--semi)
                open_semi="yes" ;;
            -r|--run)
                run_db="yes" ;;
            -h|--help)
                usage ; exit 0 ;;
            --)
                shift ; break ;;
            *)
                msg="option error! exit!"
                echo "$msg"; log "$msg" ; echo "$fail_msg"
                exit 1 ;;
        esac
        shift
    done

    if [ -z "$configs" ] ; then
        msg="no configs input,install MySQL by default"
    else
        msg="configs=$configs"
    fi
    log "$msg"
    # remove spaces
    configs=${configs// /}
    configs=${configs//,/ }
    for config in $configs ; do
        KEY=$(echo $config | awk -F '=' '{print $1}')
        VALUE=$(echo $config | awk -F '=' '{print $2}')

        case "$KEY" in
            bind_address)
                BIND_ADDRESS="$VALUE";;
            port)
                PORT="$VALUE";;
            datadir)
                DATADIR="$VALUE";;
            innodb_data_home_dir)
                INNODB_DATA_HOME_DIR="$VALUE" ;;
            innodb_log_group_home_dir)
                INNODB_LOG_GROUP_HOME_DIR="$VALUE" ;;
            innodb_undo_directory)
                INNODB_UNDO_DIRECTORY="$VALUE" ;;
            socket)
                SOCKET="$VALUE" ;;
            log-error)
                LOG_ERROR="$VALUE" ;;
            pid-file)
                PID_FILE="$VALUE" ;;
            log-bin)
                LOG_BIN="$VALUE" ;;
            relay-log)
                RELAY_LOG="$VALUE" ;;
            tmpdir)
                TMPDIR="$VALUE" ;;                    
            lower_case_table_names)
                LOWER_CASE_TABLE_NAMES="$VALUE" ;;	
            innodb_lock_wait_log_dir)
                INNODB_LOCK_WAIT_LOG_DIR="$VALUE" ;;
            slow_query_log_file)
                SLOW_QUERY_LOG_FILE="$VALUE" ;;				
            *)
            format_configs="$format_configs $config";;
        esac
    done
    
    if [ -z "$BIND_ADDRESS" ]; then
        msg="bind_address is null,install fail" ; echo "$msg" ; log "$msg" ; exit 1;
    fi
    if [ -z "$PORT" ]; then
        msg="port is null,install fail" ; echo "$msg" ; log "$msg" ; exit 1;
    fi
    if [ -z "$DATADIR" ]; then
        msg="datadir is null,install fail" ; echo "$msg" ; log "$msg" ; exit 1;
    fi
    if [ -z "$INNODB_DATA_HOME_DIR" ]; then
        msg="innodb_data_home_dir is null,install fail" ; echo "$msg" ; log "$msg" ; exit 1;
    fi
    if [ -z "$INNODB_LOG_GROUP_HOME_DIR" ]; then
        msg="innodb_log_group_home_dir is null,install fail" ; echo "$msg" ; log "$msg" ; exit 1;
    fi
    if [ -z "$INNODB_UNDO_DIRECTORY" ]; then
        msg="innodb_undo_directory is null,install fail" ; echo "$msg" ; log "$msg" ; exit 1;
    fi
    if [ -z "$INNODB_LOCK_WAIT_LOG_DIR" ]; then
        msg="innodb_lock_wait_log_dir is null,install fail" ; echo "$msg" ; log "$msg" ; exit 1;
    fi
    if [ -z "$SLOW_QUERY_LOG_FILE" ]; then
        msg="slow_query_log_file is null,install fail" ; echo "$msg" ; log "$msg" ; exit 1;
    fi
    if [ -z "$SOCKET" ]; then
        msg="socket is null,install fail" ; echo "$msg" ; log "$msg" ; exit 1;
    fi
    if [ -z "$LOG_ERROR" ]; then
        msg="log-error is null,install fail" ; echo "$msg" ; log "$msg" ; exit 1;
    fi
    if [ -z "$PID_FILE" ]; then
        msg="pid-file is null,install fail" ; echo "$msg" ; log "$msg" ; exit 1;
    fi
    if [ -z "$LOG_BIN" ]; then
        msg="log-bin is null,install fail" ; echo "$msg" ; log "$msg" ; exit 1;
    fi
    if [ -z "$RELAY_LOG" ]; then
        msg="relay-log is null,install fail" ; echo "$msg" ; log "$msg" ; exit 1;
    fi
    if [ -z "$TMPDIR" ]; then
        msg="tmpdir is null,install fail" ; echo "$msg" ; log "$msg" ; exit 1;
    fi    
    
    if [ ${LOG_BIN:0:1} != "/" ]; then
        REAL_LOG_BIN="$DATADIR/""$LOG_BIN"
    else
        REAL_LOG_BIN="$LOG_BIN"
    fi
    if [ ${RELAY_LOG:0:1} != "/" ]; then
        REAL_RELAY_LOG="$DATADIR/""$RELAY_LOG"
    else
        REAL_RELAY_LOG="$RELAY_LOG"
    fi
    
}

check_if_already_installed()
{
   list_file=""
   if [ -d "$DATADIR" ]; then
         list_file=`ls -A $DATADIR`
         if [ -n "$list_file" ]; then
            msg="datadir:$DATADIR is not empty,install fail" ; return_fail "$msg"
         fi
   fi

   list_file=""
   if [ -d "$INNODB_DATA_HOME_DIR" ]; then
         list_file=`ls -A $INNODB_DATA_HOME_DIR`
         if [ -n "$list_file" ]; then
            msg="innodb_data_home_dir:$INNODB_DATA_HOME_DIR is not empty,install fail" ; return_fail "$msg"
         fi
   fi

   list_file=""
   if [ -d "$INNODB_LOG_GROUP_HOME_DIR" ]; then
         list_file=`ls -A $INNODB_LOG_GROUP_HOME_DIR`
         if [ -n "$list_file" ]; then
            msg="innodb_log_group_home_dir:$INNODB_LOG_GROUP_HOME_DIR is not empty,install fail" ; return_fail "$msg"
         fi
   fi
   
   list_file=""
   if [ -d "$INNODB_UNDO_DIRECTORY" ]; then
         list_file=`ls -A $INNODB_UNDO_DIRECTORY`
         if [ -n "$list_file" ]; then
            msg="INNODB_UNDO_DIRECTORY:$INNODB_UNDO_DIRECTORY is not empty,install fail" ; return_fail "$msg"
         fi
   fi
}

copy_mysql()
{
    if [ $INSTANCE_ID -eq 1 ] ; then
    
        pkg_name="mysql64_linux.tar.gz"
        if [ $bit_db = "32"  ] ; then
            pkg_name="mysql32_linux.tar.gz"
        fi
        
        pkg_dir="$work_dir/../install/rdb/rdb${bit_db}_for_${os_type}"

        if [ $need_decompress = "yes" ] ; then
            if [ -f $pkg_dir/$pkg_name ]; then
                tar xzf $pkg_dir/$pkg_name -C $pkg_dir/
            else
                msg="Can't find installation package, exit"
                return_fail "$msg"
            fi
        fi
        msg="Copying MySQL..." ;log "$msg"
        [ -f $HOME/lib/libtcmalloc.so ] && rm $pkg_dir/lib/libtcmalloc.so
        cp -rf $pkg_dir/bin $HOME && cp -rf $pkg_dir/lib $HOME && cp -rf $pkg_dir/etc $HOME && cp -rf $pkg_dir/share $HOME && cp -rf $pkg_dir/log $HOME
        if [ $? -ne 0 ] ; then
            msg="Copy MySQL failed, exit"
            return_fail "$msg"
        fi
    else
        if [ ! -x $HOME/bin/mysqld ] ; then
            msg="Cannot find mysqld, you must install instance 1 before instance $INSTANCE_ID"
            return_fail "$msg"
        fi
    
    fi
    
    check_if_directory_existed $SOCKET $LOG_ERROR $PID_FILE $REAL_LOG_BIN $REAL_RELAY_LOG $INNODB_LOG_GROUP_HOME_DIR $INNODB_UNDO_DIRECTORY/undo $TMPDIR/tmp 
    #check_if_directory_existed $INNODB_LOG_GROUP_HOME_DIR
}

modify_my_cnf()
{
    msg="Modifying my.cnf ..." ; log "$msg"
    
    instance_num=`cat ${my_cnf_file} |grep instance_num|awk -F "=" '{print $2}'`
    
    if [ $INSTANCE_ID -ne 1 ] ; then 
        if [ $INSTANCE_ID -gt $instance_num ] ; then
            read_modify_ini "${my_cnf_file}" "general" "instance_num" "$INSTANCE_ID"
        else
            msg="already installed instance $instance_num , instance_id must be bigger than $instance_num"
            return_fail "$msg"
        fi
    fi
    
    read_modify_ini "${my_cnf_file}" "mysqld" "innodb_log_group_home_dir" "$INNODB_LOG_GROUP_HOME_DIR"
    read_modify_ini "${my_cnf_file}" "mysqld" "innodb_undo_directory" "$INNODB_UNDO_DIRECTORY"
    read_modify_ini "${my_cnf_file}" "mysqld" "innodb_data_home_dir" "$INNODB_DATA_HOME_DIR"
    read_modify_ini "${my_cnf_file}" "mysqld" "pid-file" "$PID_FILE"
    read_modify_ini "${my_cnf_file}" "mysqld" "log-error" "$LOG_ERROR"
    read_modify_ini "${my_cnf_file}" "mysqld" "datadir" "$DATADIR"
    read_modify_ini "${my_cnf_file}" "mysqld" "bind_address" "$BIND_ADDRESS"
    read_modify_ini "${my_cnf_file}" "mysqld" "socket" "$SOCKET"
    read_modify_ini "${my_cnf_file}" "mysqld" "port" "$PORT"
    read_modify_ini "${my_cnf_file}" "mysqld" "log-bin" "$LOG_BIN"
    read_modify_ini "${my_cnf_file}" "mysqld" "relay-log" "$RELAY_LOG"
    read_modify_ini "${my_cnf_file}" "mysqld" "tmpdir" "$TMPDIR"
    read_modify_ini "${my_cnf_file}" "mysqld" "basedir" "$BASEDIR"
    read_modify_ini "${my_cnf_file}" "mysqld" "slow_query_log_file" "$SLOW_QUERY_LOG_FILE"
    read_modify_ini "${my_cnf_file}" "mysqld" "innodb_lock_wait_log_dir" "$INNODB_LOCK_WAIT_LOG_DIR"	
    generate_serverid
    read_modify_ini "${my_cnf_file}" "mysqld" "server_id" "$SERVER_ID"

    read_modify_ini "${my_cnf_file}" "client" "port" "$PORT"
    read_modify_ini "${my_cnf_file}" "client" "socket" "$SOCKET"
    
    read_modify_ini "${my_cnf_file}" "mysqld" "report_host" "$BIND_ADDRESS"
    read_modify_ini "${my_cnf_file}" "mysqld" "report_port" "$PORT"
    read_modify_ini "${my_cnf_file}" "mysqld" "lower_case_table_names" "$LOWER_CASE_TABLE_NAMES"

    modify_config "${my_cnf_file}" "mysqld" "$format_configs"
}

#generate server id by ip and port
generate_serverid()
{
    num1=`echo $BIND_ADDRESS |awk -F '.' '{print $1}'`
    num2=`echo $BIND_ADDRESS |awk -F '.' '{print $2}'`
    num3=`echo $BIND_ADDRESS |awk -F '.' '{print $3}'`
    num4=`echo $BIND_ADDRESS |awk -F '.' '{print $4}'`

    # Don not use 0 and MAX_SERVER_ID(2 ** 32 - 1) !
    # 0 is reserved for Mysql, and MAX_SERVER_ID is reserved for restore.py.
    # The below SERVER_ID's range: 1 ~ 2 ** 32 - 2
    SERVER_ID=$[ ($num4 * 2 ** 24 + $num3 * 2 ** 16 + $num2 * 2 ** 8 + $num1 + $PORT) % (2 ** 32 - 2) + 1 ]
}

# modify config, $1 path of config, $2 section , $3 args e.g. key=value[,...]
modify_config()
{
    INI=$1
    SECTION=$2
    ARGS=$3
    KEY=""
    VALUE=""
    ARG=""

    if [ $# -ne 3 ] ; then
        msg="Usage: modify_config <my.cnf file> <section> <args>"
        return_fail "$msg" ;
    fi

    # convert space to ,
    ARGS=${ARGS//,/ }

    # modify configs into ini
    for ARG in $ARGS ; do
        KEY=$(echo $ARG | awk -F '=' '{print $1}')
        VALUE=$(echo $ARG | awk -F '=' '{print $2}')
        read_modify_ini "$INI" "$SECTION" "$KEY" "$VALUE"
        #echo "$INI $SECTION $KEY $VALUE"
    done
}

install_db()
{
    host_name=$(echo $HOSTNAME)
    $HOME/bin/mysqld --defaults-file="$HOME/etc/my.cnf" --initialize-insecure --user=${USER}
    
    sh mysql.server start
    sleep 2
    
    sh mysql.server stop
}

open_plugin_config()
{
    if [ $open_semi = "yes" ] ; then
        sed -i 's/#rpl_semi_sync_master_enabled = ON/rpl_semi_sync_master_enabled = ON/g' $my_cnf_file
        sed -i 's/#rpl_semi_sync_slave_enabled = ON/rpl_semi_sync_slave_enabled = ON/g' $my_cnf_file
        msg="Semi-Synchronous configs opened" ; log "$msg"
    fi
    
    sed -i 's/#early-plugin-load = keyring_file.so/early-plugin-load = keyring_file.so/g' $my_cnf_file
    sed -i 's?#keyring_file_data = /home/mysql/data/data/keyring?keyring_file_data = '$DATADIR'/keyring?g' $my_cnf_file
    msg="keyring_file configs opened" ; log "$msg"
}

startup_db()
{
    if [ $run_db = "yes" ] ; then
        check_pid
        if [ $pid_flag = 0 ] ; then
            sh mysql.server start
        else
            msg="MySQL is already started" ; log "$msg"
        fi
    fi

}

check_pid()
{
    i=1
    while [ $i -lt 30 ]
    do
    if [ ! -e $PID_FILE ] ; then
        {
            pid_flag=0
            break
        }
    fi
    i=`expr $i + 1`
    sleep 1
    done
}

#check directory
check_if_directory_existed()
{
   while [ $# -gt 0 ]; do
      dir_name=`dirname $1`
      #echo $dir_name
      if [ ! -d "$dir_name" ]; then
         mkdir -p  $dir_name
         msg="datadir:$dir_name does not exist,create it" ;log "$msg"
         if [ $? -ne 0 ]; then
         msg="datadir:$dir_name create fail,exit" ; return_fail "$msg"
         fi
      fi
      shift
   done
   dir_name=$INNODB_LOG_GROUP_HOME_DIR
   #echo $dir_name
    if [ ! -d "$dir_name" ]; then
         mkdir -p  $dir_name
         msg="datadir:$dir_name does not exist,create it" ;log "$msg"
         if [ $? -ne 0 ]; then
         msg="datadir:$dir_name create fail,exit" ; return_fail "$msg"
         fi
   fi
   dir_name=$INNODB_UNDO_DIRECTORY
   if [ ! -d "$dir_name" ]; then
    mkdir -p  $dir_name
    msg="datadir:$dir_name does not exist,create it" ;log "$msg"
    if [ $? -ne 0 ]; then
        msg="datadir:$dir_name create fail,exit" ; return_fail "$msg"
    fi
   fi
}

# modify init file by key_value
read_modify_ini()
{
    if [ $# -lt 2 ] ; then
        msg="Usage: read_modify_ini <.ini file> <section> [<key>] [<newvalue>]"
        return_fail "$msg"
    fi

    if [ "$3" = "" ];then
            sed -n "/\[$2\]/,/\[.*\]/{
      /^\s*\[.*\]/d
      /^$/d
      /^\s*[;#].*/d
            p
            }" $1
    else
        Has_section=1
        Has_key=1
        #we identify '-' be same with '_',so we may find key twice
        key1=`echo "$3" |sed 's/-/_/g'`
        key2=`echo "$3" |sed 's/_/-/g'`
        found_key="$3"

        ${UNIXGREP} "\[$2\]" $1 >/dev/null 2>&1 || Has_section=0
        cat $1 | ${UNIXAWK} 'BEGIN{HAS_SECTION=0}{if($0~/\[.*\]/) {HAS_SECTION=0} if($0~/\['$2'\]/){HAS_SECTION=1} if(HAS_SECTION==1) {if(($0!~/^[ |\t]*[;#]/)&&($0~/^[ |\t]*'$key1'[ |\t]*=.*/)) {print $0}}}' | ${UNIXGREP} "$key1" >/dev/null 2>&1 || Has_key=0

        if [ ${Has_key} -eq 0 ]; then
            Has_key=1
            cat $1 | ${UNIXAWK} 'BEGIN{HAS_SECTION=0}{if($0~/\[.*\]/) {HAS_SECTION=0} if($0~/\['$2'\]/){HAS_SECTION=1} if(HAS_SECTION==1) {if(($0!~/^[ |\t]*[;#]/)&&($0~/^[ |\t]*'$key2'[ |\t]*=.*/)) {print $0}}}' | ${UNIXGREP} "$key2" >/dev/null 2>&1 || Has_key=0
            if [ ${Has_key} -eq 0 ]; then
                :
            else
                found_key="$key2"
            fi
        else
            found_key="$key1"
        fi

        if [ ${Has_section} -eq 1 -a ${Has_key} -eq 0 ];then
            if [ "$4" = "" ] ; then
                cat $1 | $UNIXAWK -vKEY_N=$3 'BEGIN{HAS_SECTION=0;}{{print $0;}if($0~/\[.*\]/) {HAS_SECTION=0} if($0~/\['$2'\]/){HAS_SECTION=1} if(HAS_SECTION==1) {printf("%s\n",KEY_N);HAS_SECTION=0;}}' >awk_inifile_temp
            else
                cat $1 | $UNIXAWK -vKEY_N=$3 -vKEY_V="$4" 'BEGIN{HAS_SECTION=0;}{{print $0;}if($0~/\[.*\]/) {HAS_SECTION=0} if($0~/\['$2'\]/){HAS_SECTION=1} if(HAS_SECTION==1) {printf("%s=%s\n",KEY_N,KEY_V);HAS_SECTION=0;}}' >awk_inifile_temp
            fi
            if [ $? -eq 0 ];then
                cat awk_inifile_temp > $1
                rm -f awk_inifile_temp
            fi
        fi

        if [ -n "$4" -a ${Has_section} -eq 1 -a ${Has_key} -eq 1 ];then
            new_val=`echo $4 | sed 's/\//\\\\\//g'`

            if [ ${OS_TYPE} = "linux" ];then
                cat $1 | sed "/\[$2\]/,/\[.*\]/{s/^[ |\t]*$found_key[ |\t]*=.*/$found_key=$new_val/}" >sed_temp_modify
                if [ $? -eq 0 ];then
                    cat sed_temp_modify > $1
                    rm -f sed_temp_modify
                fi
            else
                cat $1 | sed "/\[$2\]/,/\[.*\]/s/^[ |\t]*$found_key[ |\t]*=.*/$found_key=$new_val/" >sed_temp_modify
                if [ $? -eq 0 ];then
                    cat sed_temp_modify > $1
                    rm -f sed_temp_modify
                else
                    echo "modify failure!"
                fi
            fi
        fi

        if [ ${Has_section} -eq 0 ];then
            echo "[$2]" >>$1
            echo "$3=$4" >>$1
        fi
    fi
    return 0
}


execute_command_after_initialize_db()
{
  msg="execute_command_after_initialize_db ..." ; log "$msg"
  sh mysql.server start
  
  if [ $? -eq 1 ]; then
    return_fail "DB start fail"
  fi
  
  mysql -uroot -e "SET session sql_log_bin=0;ALTER TABLE mysql.slave_master_info ENCRYPTION  = 'Y'; SET session sql_log_bin=1;";
  
  return 0;
}


#######################################################
# Start of main
#######################################################

# command define,default linux
UNIXAWK=awk
UNIXGREP=grep
UNIXUNZIP="gunzip -f"

# global variables
work_dir=$(cd `dirname $0`;pwd)
log_file="$work_dir/../installlog/install_rdb.log"
fail_msg="failure"
success_msg="success"
bit_db=""
run_db="no"
os_type=""
#init_file="../../install.ini"
my_cnf_file="$HOME/etc/my.cnf"
pid_flag="1"
need_decompress="yes"
open_semi="no"

#config that may be modified
BIND_ADDRESS="127.0.0.1"
PORT="5518"
DATADIR="$HOME/data/data"
INNODB_DATA_HOME_DIR="$HOME/data/data"
INNODB_LOG_GROUP_HOME_DIR="$HOME/data/redo"
INNODB_UNDO_DIRECTORY="$HOME/data/undo"
INNODB_LOCK_WAIT_LOG_DIR="$HOME/log/"
SLOW_QUERY_LOG_FILE="$HOME/log/slow.log"
SOCKET="$HOME/bin/mysql1.sock"
LOG_ERROR="$HOME/log/mysqld1.log"
PID_FILE="$HOME/bin/mysqld1.pid"
INSTANCE_ID="1"
LOG_BIN="../binlog/mysql-bin"
RELAY_LOG="../relaylog/relay-bin"
TMPDIR="$HOME/data/tmp"
BASEDIR="$HOME"
LOWER_CASE_TABLE_NAMES=1
#check log path
check_if_directory_existed $log_file

# title tips
msg="Start install MySQL" > /dev/null
log "$msg"

# get operating system type
get_os_type

# check the profile
check_profile

# get options
get_options "$@"

#check some specific config
check_if_already_installed

#copy MySQL
copy_mysql

#read_config_from_my.cnf
modify_my_cnf

# install db
install_db

#open semi configs
open_plugin_config

# alter db status after initialize to ensure goldendb system run well
# diable beacause of xtrbackup dosen't support encryption
# execute_command_after_initialize_db

# run MySQL
startup_db

#return success
log "$success_msg"
exit 0

