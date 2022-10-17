#!/bin/bash
#
# SCRIPT: install.sh
# AUTHOR: linzhoukai
# DATE:2014.12.11
# REV: 1.00
# PLATFORM: Red Hat Enterprise Linux Server release 6.5
# PURPOSE: Auto install 

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

}

read_config_from_install_ini()
{
    BIND_ADDRESS=`${UNIXGREP} instance_ip= ${init_file} | ${UNIXAWK} -F '=' '{print $2}'`
    [ -z "${BIND_ADDRESS}" ] && return_fail "Error: Fail to get BIND_ADDRESS:$BIND_ADDRESS, quit."
	msg="BIND_ADDRESS = ${BIND_ADDRESS}"
	log "$msg"

    AUTO_START=`${UNIXGREP} auto_start_db= ${init_file} | ${UNIXAWK} -F '=' '{print $2}'`
    [ -z "${AUTO_START}" ] && return_fail "Error: Fail to get AUTO_START:$AUTO_START, quit."
	msg="AUTO_START = ${AUTO_START}"
	log "$msg"
	
	DATA_HOME_DIR=`${UNIXGREP} data_home_dir= ${init_file} | ${UNIXAWK} -F '=' '{print $2}'`
    if [ -n "${DATA_HOME_DIR}" ]; then		
		msg="DATA_HOME_DIR = ${DATA_HOME_DIR}"
		log "$msg"
		configs=${configs}"datadir="${DATA_HOME_DIR}", "
		configs=${configs}"innodb_data_home_dir="${DATA_HOME_DIR}", "
	fi
	
	REDOLOG_DIR=`${UNIXGREP} redolog_dir= ${init_file} | ${UNIXAWK} -F '=' '{print $2}'`
    if [ -n "${REDOLOG_DIR}" ]; then
		msg="REDOLOG_DIR = ${REDOLOG_DIR}"
		log "$msg"
		configs=${configs}"innodb_log_group_home_dir="${REDOLOG_DIR}", "
	fi
	
	BINLOG_DIR=`${UNIXGREP} binlog_dir= ${init_file} | ${UNIXAWK} -F '=' '{print $2}'`
    if [ -n "${BINLOG_DIR}" ]; then
		msg="BINLOG_DIR = ${BINLOG_DIR}"
		log "$msg"
		configs=${configs}"log-bin="${BINLOG_DIR}"/mysql-bin, "
		configs=${configs}"relay-log="${BINLOG_DIR}"/../relaylog/relay-bin, "
	fi
	#transaction-isolation=READ-COMMITTED
	
	TRANSACTION_ISOLATION=`${UNIXGREP} transaction-isolation= ${init_file} | ${UNIXAWK} -F '=' '{print $2}'`
    if [ -n "${TRANSACTION_ISOLATION}" ]; then
		msg="TRANSACTION-ISOLATION = ${TRANSACTION_ISOLATION}"
		log "$msg"
		configs=${configs}"transaction-isolation="${TRANSACTION_ISOLATION}", "
	fi
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
init_file="$work_dir/../../DBParam.ini" 
BIND_ADDRESS="127.0.0.1"
AUTO_START=1
DATA_HOME_DIR=""
REDOLOG_DIR=""
BINLOG_DIR=""
TRANSACTION_ISOLATION=""
configs=""
#SERVER_ID="11"

#check log path
check_if_directory_existed $log_file


# title tips
msg="Start auto install " > /dev/null
log "$msg"

# get operating system type
get_os_type

# read config
read_config_from_install_ini

# call manual_install
msg="Installing " > /dev/null
log "$msg"
chmod +x $work_dir/manual_install.sh
if [ $AUTO_START -eq 1 ] ; then
	$work_dir/manual_install.sh -c "${configs}bind_address=$BIND_ADDRESS" -s -r > /dev/null
else
	$work_dir/manual_install.sh -c "${configs}bind_address=$BIND_ADDRESS" -s > /dev/null
fi
#./manual_install.sh -c "bind_address=$BIND_ADDRESS,server_id=$SERVER_ID" -n -s -r > /dev/null

result=$?
if [ $result -eq 0 ] ; then 
    echo "$success_msg"
    exit 0
else
	return_fail "Install fail! Please check install log and error log!"
fi
