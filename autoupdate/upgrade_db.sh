#!/bin/sh

##############################################
# upgrade the db 
##############################################

#######################################################
# 1. Define variables here
#######################################################

# variables related to current goldendb version
check_new_goldendb_version="V_allmaster"
upgrade_min_version="V_2.0.04.02U1"

# input option variables
con_user=""
con_passwd=""
con_host=""
con_port=""
version=""
version_path=""

# other variables
cur_timestamp=""
backup_cnf=""
tmp_setup_dir=""

work_dir=$(cd `dirname $0`;pwd)
modify_cnf_file_path="${work_dir}/modify_cnf.ini"
extra_command_sql_path="${work_dir}/extra_command.sql"

# os info variables
bit_db=""
os_type=""

#######################################################
# 2. Define function here
#######################################################

# show usage of install script
usage()
{   echo "  upgrade_db.sh [options] [--] optstring parameters"
    echo "  -u,--user             User to use when connecting to server"
    echo "  -p,--password         Password to use when connecting to server"
    echo "  -h,--host             Host to use when connecting to server, default is localhost"
    echo "  -P,--port             Port to use when connecting to server, default is localhost"
    echo "  -v,--version          the version of the current db"
    echo "  -V,--version_path     the version path of the upgrade db, default version is in ../install/ path"
    echo "  --help                show usage of install"
}

# print ok ouput: time msg <GOOD>~
print_ok()
{
    if [ -n "$1" ] ; then
        datetime=$(date "+%Y-%m-%d %H:%M:%S") > /dev/null        
        echo "${datetime} $1 <GOOD>~"
    fi
}

# print error output: time msg <FAIL>~
print_error()
{
    if [ -n "$1" ] ; then
        datetime=$(date "+%Y-%m-%d %H:%M:%S") > /dev/null        
        echo "${datetime} $1 <FAIL>~"
    fi
}

clear_tmp_file()
{
    if [ ! -z "${backup_cnf}" -a -f ${HOME}/etc/${backup_cnf} ] ; then
        rm -rf ${HOME}/etc/${backup_cnf}
    fi
    
    if [ ! -z "${tmp_setup_dir}" -a -d ${HOME}/${tmp_setup_dir} ] ; then
        rm -rf ${HOME}/${tmp_setup_dir}
    fi
}

return_fail()
{
    clear_tmp_file
    echo -1
    exit 1
}

## execute the sql command to mysql.
## $1 is the input sql.
## return 0 is exec ok, 1 is exec false
exec_sql()
{
    if [ -z "$1" ] ; then
        return 1
    fi
        
    export MYSQL_PWD="${con_passwd}"
    if [ -n "${con_host}" -a -n "${con_port}" ] ; then
        mysql -u${con_user} -h${con_host} -P${con_port} -e "$1"
    else
        mysql -u${con_user} -e "$1"
    fi
    
    if [ $? -ne 0 ] ; then
        return 1;
    fi
    
    return 0;
}

## execute the select sql command to mysql.
## $1 is the input sql.
## return 0 is exec ok and has result, other is error or without result
exec_select_sql()
{
    if [ -z "$1" ] ; then
        return 1
    fi
        
    export MYSQL_PWD="${con_passwd}"
    if [ -n "${con_host}" -a -n "${con_port}" ] ; then
        res=`mysql -u${con_user} -h${con_host} -P${con_port} -e "$1"`
    else
        res=`mysql -u${con_user} -e "$1"`
    fi
    
    if [ $? -ne 0 ] ; then
        return 1;
    fi
    
    if [ -n "$res" ] ; then
        return 0;
    fi
    
    return 1;
}


# parse all the arguments
parse_args()
{
    ARGS=`getopt -o u:p:h:P:v:V: -l user:,password:,host:,port:,version:,version_path:,help -- "$@"`

    if [ "$?" -ne 0 ]; then
        print_error "execute getopt function error, see --help."
        return_fail
    fi

    eval set -- "${ARGS}"
    while true ; do
        case "$1" in
            -u|--user)
                con_user="$2" ; shift ;;
            -p|--passwork)
                con_passwd="$2" ; shift ;;
            -h|--host)
                con_host="$2" ; shift ;;
            -P|--port)
                con_port="$2" ; shift ;;
            -v|--version)
                version="$2" ; shift ;;
            -V|--version_path)
                version_path="$2" ; shift ;;
            --help)
                usage ; exit 0 ;;
            --)
                shift ; break ;;
            *)
                print_error "execute shell with invalid argument, see --help."
                return_fail ;;
        esac
        shift
    done

    if [ -z "${con_user}" -o -z "${con_passwd}" ] ; then
        print_error "execute shell without user or passwd, see --help."
        return_fail
    fi
    
    if [ -z "${version}" ] ; then
        print_error "execute shell without current version, see --help."
        return_fail
    fi
    
    print_ok "check all the arguments ok."
}

get_os_type()
{
	echo "get os type"
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
	echo ${msg}
}

check_upgrade_version_ok()
{
    if [ ${version} \< ${upgrade_min_version} ] ; then
        print_error "input version ${version} < ${upgrade_min_version} error."
        return_fail
    fi
    if [ ${version} \> ${check_new_goldendb_version} ] ; then
        print_error "input version ${version} > ${check_new_goldendb_version} error."
        return_fail
    fi
    print_ok "check the input db version ok."
}

# check if the db is start or can start ok
check_db_start_ok()
{
    program_mysql=`pgrep  mysqld  -u ${USER}|head -n 1`
    if [ -z ${program_mysql} ] ; then
        mysql.server start
        if [ $? -ne 0 ] ; then
            print_error "start the mysql error, can not upgrade db."
            return_fail
        fi

        program_mysql=`pgrep  mysqld  -u ${USER}|head -n 1`
        if [ -z ${program_mysql} ] ; then
            print_error "start mysql error, can not upgrade db."
            return_fail
        fi
    fi
    print_ok "check db start ok."
}

# uninstall the semisync plugin before upgrade the DB version 
uninstall_semisync_plugin()
{
    exec_select_sql "select name from mysql.plugin where name='rpl_semi_sync_master';"
    if [ $? -eq 0 ] ; then
        exec_sql "uninstall plugin rpl_semi_sync_master;"
        if [ $? -eq 0 ] ; then
            print_ok "uninstall plugin rpl_semi_sync_master ok."
        else
            print_error "uninstall plugin rpl_semi_sync_master failed."
        fi
    fi
    
    exec_select_sql "select name from mysql.plugin where name='rpl_semi_sync_slave';"
    if [ $? -eq 0 ] ; then
        exec_sql "stop slave; uninstall plugin rpl_semi_sync_slave;"
        if [ $? -eq 0 ] ; then
            print_ok "uninstall plugin rpl_semi_sync_slave ok."
        else
            print_error "uninstall plugin rpl_semi_sync_slave failed."
        fi
    fi
}

stop_db()
{
    mysql.server stop
    if [ $? -ne 0 ] ; then
        print_error "stop mysql error, can not upgrade db."
        return_fail
    fi
    print_ok "stop db ok."
}

get_current_timestamp()
{
    timeStamp=`date +%s`
    echo ${timeStamp}
}

back_up_old_config()
{
    cur_timestamp=$(get_current_timestamp)
    backup_cnf="my.cnf_backup_${cur_timestamp}"
    cp ${HOME}/etc/my.cnf ${HOME}/etc/${backup_cnf}
    if [ $? -ne 0 ] ; then
        print_error "back up old my.cnf error, can not upgrade db."
        return_fail
    fi
    print_ok "back up the old my.cnf ok."
}

unzip_upgrade_version()
{
    tmp_setup_dir="tmp_setup_$cur_timestamp"
    
    if [ "${version_path}" == "" ] ; then
        tar_path="${work_dir}/../install/rdb/rdb${bit_db}_for_${os_type}/mysql64_linux.tar.gz"
    else
        if [ ! -f ${version_path} ] ; then
            print_error "the upgrade version not exist, can not upgrade db."
            return_fail
        fi

        unzip -o ${version_path} -d ${HOME}/${tmp_setup_dir} > /dev/null
        if [ $? -ne 0 ] ; then
            print_error "unzip db version error, can not upgrade db."
            return_fail
        fi
        
        tar_path="${HOME}/${tmp_setup_dir}/install/rdb/rdb${bit_db}_for_${os_type}/mysql64_linux.tar.gz"
    fi
    
    if [ ! -f ${tar_path} ] ; then
        print_error "the upgrade mysql64_linux.tar.gz not exist, can not upgrade db."
        return_fail
    fi    
        
    tar -xzvf ${tar_path} -C ${HOME}/ > /dev/null
    if [ $? -ne 0 ] ; then
        print_error "tar db version error, can not upgrade db."
        return_fail
    fi
    
    rm -rf ${HOME}/${tmp_setup_dir}
    print_ok "unzip upgrade db version ok."
}

## execute all the config modify cmd in the modify_cnf.ini
modify_config()
{
    mv ${HOME}/etc/${backup_cnf} ${HOME}/etc/my.cnf
    if [ $? -ne 0 ] ; then
        print_error "restore old backup cnf failed, can not upgrade db."
        return_fail
    fi
    
    if [ ! -f ${modify_cnf_file_path} ] ; then
        print_error "the modify_cnf.ini file not exist, can not upgrade db."
        return_fail
    fi
    
    SAVEIFS=$IFS
    IFS=$'\n'
    for line in $(cat ${modify_cnf_file_path})
    do
        if [ ${line:0:3} == "sed" -o ${line:0:4} == "grep" ] ; then
            exec_cmd="${line}"
            eval ${exec_cmd}
            if [ $? -ne 0 ] ; then
                print_error "exec modify cnf cmd failed: ${exec_cmd}."
                return_fail
            fi
            print_ok "exec modify cnf cmd is: ${exec_cmd}."
        fi
    done
    IFS=$SAVEIFS
    
    print_ok "modify db config ok."
}

start_db()
{
    program_mysql=`pgrep  mysqld  -u ${USER}|head -n 1`
    if [ ! -z ${program_mysql} ] ; then
        print_error "mysql is running before start, can not upgrade db."
        return_fail
    fi

    mysql.server start
    if [ $? -ne 0 ] ; then
        print_error "start mysql error, can not upgrade db."
        return_fail
    fi
    print_ok "start db ok."
}

check_db_version_ok()
{
    export MYSQL_PWD="${con_passwd}"
    if [ ! -z "${con_host}" -a ! -z "${con_port}" ] ; then
        select_res=`mysql -u${con_user} -h${con_host} -P${con_port} -e "select @@goldendb_version;"`
    else
        select_res=`mysql -u${con_user} -e "select @@goldendb_version;"`
    fi
    
    version_res=`echo ${select_res} |sed 's/@@goldendb_version //g'`
    if [ "${version_res}" != "${check_new_goldendb_version}" ] ; then
        print_error "check goldendb version error, the current version is ${version_res}."
        return_fail
    fi
    print_ok "check goldendb version after upgrade ok."
}


execute_extra_command()
{
  export MYSQL_PWD="${con_passwd}"      
    
  if [ ! -z "${con_host}" -a ! -z "${con_port}" ] ; then
    mysql -u${con_user} -h${con_host} -P${con_port} -e "source $extra_command_sql_path"
  else
    mysql -u${con_user} -e "source $extra_command_sql_path"
  fi

  if [ $? -eq 1 ]; then
    print_error "execute extra command fail."
    return_fail
  fi
    
  print_ok "execute extra command ok."
  
  return 0;
}

## This upgrade have the new DB version and the data of old DB version.
## So we can not uninstall the semisync plugin in the old DB version.
## We will delete the record from system table mysql.plugin to avoid the error_log in log.
delete_semisync_records()
{
    exec_select_sql "select name from mysql.plugin where name='rpl_semi_sync_master';"
    if [ $? -eq 0 ] ; then
        exec_sql "delete from mysql.plugin where name='rpl_semi_sync_master';"
        if [ $? -eq 0 ] ; then
            print_ok "delete rpl_semi_sync_master record from mysql.plugin ok."
        else
            print_error "delete rpl_semi_sync_master record from mysql.plugin failed."
        fi
    fi
    
    exec_select_sql "select name from mysql.plugin where name='rpl_semi_sync_slave';"
    if [ $? -eq 0 ] ; then
        exec_sql "delete from mysql.plugin where name='rpl_semi_sync_slave';"
        if [ $? -eq 0 ] ; then
            print_ok "delete rpl_semi_sync_slave record from mysql.plugin ok."
        else
            print_error "delete rpl_semi_sync_master record from mysql.plugin failed."
        fi
    fi
}

## set the dynamic relay_log_info_repository variable because these variable does not validate.
set_repository_cfg()
{
  export MYSQL_PWD="${con_passwd}" 

  if [ ! -z "${con_host}" -a ! -z "${con_port}" ] ; then
    select_res=`mysql -u${con_user} -h${con_host} -P${con_port} -e "select @@relay_log_info_repository;"`
  else
    select_res=`mysql -u${con_user} -e "select @@relay_log_info_repository;"`
  fi
  
  result_res=`echo ${select_res} |sed 's/@@relay_log_info_repository //g'`
  if [ "${result_res}" != "FILE" -a "${result_res}" != "TABLE" ] ; then
    print_error "check relay_log_info_repository error, the relay_log_info_repository is ${result_res}."
    return_fail
  elif [ "${result_res}" == "FILE" ] ; then
  
    if [ ! -z "${con_host}" -a ! -z "${con_port}" ] ; then
      mysql -u${con_user} -h${con_host} -P${con_port} -e "stop slave;set global relay_log_info_repository='TABLE';"
    else
      mysql -u${con_user} -e "stop slave;set global relay_log_info_repository='TABLE';"
    fi
    
    if [ $? -eq 1 ]; then
      print_error "execute set global relay_log_info_repository = TABLE fail."
      return_fail
    fi
    
    print_ok "execute set global relay_log_info_repository = TABLE OK."
  fi
}


#######################################################
# 3. Start of main
#######################################################
print_ok "upgrade db start, please wait."

parse_args "$@"

# get operating system type
get_os_type

check_upgrade_version_ok

check_db_start_ok

#uninstall_semisync_plugin

stop_db

back_up_old_config

unzip_upgrade_version

modify_config

start_db

check_db_version_ok

#execute extra command to ensure DB run well
execute_extra_command

#delete_semisync_records

set_repository_cfg

print_ok "upgrade db end and ok."

echo 0

exit 0
