#!/bin/sh

##############################################
# upgrade the db 
##############################################

#######################################################
# 1. Define variables here
#######################################################

# variables related to current goldendb version
check_new_goldendb_version="V_allmaster"
upgrade_min_version="V_2.0.03.02"

# input option variables
con_user=""
con_passwd=""
con_host=""
con_port=""
version=""

# other variables
cur_timestamp=""
backup_cnf=""
tmp_setup_dir=""

work_dir=$(cd `dirname $0`;pwd)
modify_cnf_file_path="${work_dir}/modify_cnf.ini"
extra_command_sql_path="${work_dir}/extra_command.sql"
my_cnf_path="$HOME/etc/my.cnf"


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


return_fail()
{
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
    ARGS=`getopt -o u:p:h:P:v: -l user:,password:,host:,port:,version:,help -- "$@"`

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
    
    if [[ "${con_host}"x != x ]] ||\
       [[ "${con_port}"x != x ]]
    then 
        check_host_port_ok
    fi     

    print_ok "check all the arguments ok."
}

check_host_port_ok()
{
    if [ ! -f ${my_cnf_path} ] ; then
        print_error "the my.cnf file not exist, can not upgrade db."
        return_fail
    fi 
       
    if [[ "${con_host}"x != x ]]; then 
        host=`sed -n '/^bind_address/p' ${my_cnf_path} | tail -n1 | sed 's/[[:space:]]//g' | sed 's/bind_address=//g' `
        if [[ "${host}"x != "${con_host}"x ]] ; then
        print_error "input host is not the host in my.cnf, please check"
        return_fail
        fi 
    fi
    
    if [[ "${con_port}"x != x ]]; then 
        port=`sed -n '/^port/p' ${my_cnf_path} | tail -n1 | sed 's/[[:space:]]//g' | sed 's/port=//g' `
        if [[ "${port}"x != "${con_port}"x ]] ; then
        print_error "input port is not the port in my.cnf, please check"
        return_fail
        fi
    fi
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

## execute all the config modify cmd in the modify_cnf.ini
modify_config()
{
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
    if [ -z ${program_mysql} ] ; then
        mysql.server start 
        if [ $? -ne 0 ] ; then
            print_error "start mysql error, can not upgrade db."
            return_fail
        fi
        
        program_mysql=`pgrep  mysqld  -u ${USER}|head -n 1`
        if [ -z ${program_mysql} ] ; then
            print_error "start mysql error, can not upgrade db."
            return_fail
        fi
            print_ok "start db ok."    
    fi
    print_ok "start db ok."
}
 

restart_db()
{
    program_mysql=`pgrep  mysqld  -u ${USER}|head -n 1`
    if [ -z ${program_mysql} ] ; then
        mysql.server start 
        if [ $? -ne 0 ] ; then
            print_error "start mysql error, can not upgrade db."
            return_fail
        fi
        
        program_mysql=`pgrep  mysqld  -u ${USER}|head -n 1`
        if [ -z ${program_mysql} ] ; then
            print_error "start mysql error, can not upgrade db."
            return_fail
        fi
            print_ok "start db ok."    
    else 
        mysql.server restart 
        if [ $? -ne 0 ] ; then
            print_error "restart mysql error, can not upgrade db."
            return_fail
        fi
        
        program_mysql=`pgrep  mysqld  -u ${USER}|head -n 1`
        if [ -z ${program_mysql} ] ; then
            print_error "restart mysql error, can not upgrade db."
            return_fail
        fi
            print_ok "restart db ok."        
    fi
}


rm_mysql_upgrade_info()
{
    export MYSQL_PWD="${con_passwd}"
    if [ ! -z "$con_host" -a ! -z "$con_port" ] ; then
        select_res=`mysql -u${con_user} -h${con_host} -P${con_port} -e "select @@datadir;"`
    else 
        select_res=`mysql -u${con_user} -e "select @@datadir;"`
    fi
    
    data_dir=`echo ${select_res} |sed 's/@@datadir //g'`    
    mysql_upgrade_info_file="${data_dir}mysql_upgrade_info"
    
    if [ -f ${mysql_upgrade_info_file} ] ; then
        rm -rf ${mysql_upgrade_info_file}
        if [ $? -ne 0 ] ; then
            print_error "rm -rf ${mysql_upgrade_info_file} failed. please remove it manually and run upgrade_db.sh again!"
        fi
        print_ok "remove mysql_upgrade_info file ok, can upgrade."
    else
    print_ok "mysql_upgrade_info check ok,can upgrade"
    fi
}

mysql_upgrade_db()
{
    export MYSQL_PWD="${con_passwd}"
    
    if [ ! -z "$con_host" -a ! -z "$con_port" ] ; then
        select_res=`mysql -u${con_user} -h${con_host} -P${con_port} -e "select @@log_error;"`
    else 
        select_res=`mysql -u${con_user} -e "select @@log_error;"`
    fi
    
    log_dir=`echo ${select_res} |sed 's/@@log_error //g'|sed 's/mysqld1.log//g'`
    mysql_upgrade_log_file="${log_dir}mysql_upgrade.log"
    
    if [ ! -z "${con_host}" -a ! -z "${con_port}" ] ; then
        date "+%Y-%m-%d %H:%M:%S"  >  ${mysql_upgrade_log_file}
        ~/bin/mysql_upgrade -u${con_user} -h${con_host} -P${con_port}  >> ${mysql_upgrade_log_file}
    else
        date "+%Y-%m-%d %H:%M:%S"  >  ${mysql_upgrade_log_file}
        ~/bin/mysql_upgrade -u${con_user}  >> ${mysql_upgrade_log_file}
    fi
    
    res=`cat ${mysql_upgrade_log_file}|grep "Upgrade process completed successfully" `
    if [ -z "${res}" ] ; then
        print_error "mysql_upgrade mysql error, can not upgrade db."
        cat ${mysql_upgrade_log_file}
        return_fail
    fi
    
    res=`cat ${mysql_upgrade_log_file}|grep -i "error" `
    if [ ! -z "${res}" ] ; then
        print_error "Upgrade process completed successfully but some error occur"
        cat ${mysql_upgrade_log_file}
        return_fail
    fi
        
    print_ok "Upgrade process completed successfully, mysql_upgrade execute ok."
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

check_upgrade_version_ok

modify_config

start_db

rm_mysql_upgrade_info

mysql_upgrade_db

check_db_version_ok

#execute extra command to ensure DB run well
execute_extra_command

#delete_semisync_records

set_repository_cfg

restart_db

print_ok "upgrade db end and ok."

echo 0

exit 0
