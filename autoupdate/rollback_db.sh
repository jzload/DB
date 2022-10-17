#!/bin/sh

##############################################
# upgrade the db 
##############################################

#######################################################
# 1. Define variables here
#######################################################

# variables related to current goldendb version
repository_to_table_version="V_5.0.01"
upgrade_min_version="V_2.0.04.02U1"
current_mysql_version="5.7.22-log"

# input option variables
con_user=""
con_passwd=""
con_host=""
con_port=""

work_dir=$(cd `dirname $0`;pwd)



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

# parse all the arguments
parse_args()
{
    ARGS=`getopt -o u:p:h:P: -l user:,password:,host:,port:,help -- "$@"`

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
    
    print_ok "check all the arguments ok."
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

## return 0 means need rollback the relay_log_info_repository config.
## return 1 meanse not need rollback.
check_goldendb_version()
{
    export MYSQL_PWD="${con_passwd}"
    if [ ! -z "${con_host}" -a ! -z "${con_port}" ] ; then
        select_res=`mysql -u${con_user} -h${con_host} -P${con_port} -e "select @@goldendb_version;" 2>/dev/null`
    else
        select_res=`mysql -u${con_user} -e "select @@goldendb_version;" 2>/dev/null`
    fi
    
    if [ "${select_res}" == "" ] ; then
      return 0
    fi
    
    version_res=`echo ${select_res} |sed 's/@@goldendb_version //g'`
    if [ ${version_res} \< ${repository_to_table_version} ] ; then
        return 0
    fi
    return 1
}

## return 0 means need rollback the relay_log_info_repository config.
## return 1 meanse not need rollback.
check_mysql_version()
{
    export MYSQL_PWD="${con_passwd}"
    if [ ! -z "${con_host}" -a ! -z "${con_port}" ] ; then
        select_res=`mysql -u${con_user} -h${con_host} -P${con_port} -e "select @@version;"`
    else
        select_res=`mysql -u${con_user} -e "select @@version;"`
    fi
    
    if [ "${select_res}" == "" ] ; then
      return 1
    fi
    
    version_res=`echo ${select_res} |sed 's/@@version //g'`
    if [ ${version_res} \< ${current_mysql_version} ] ; then
        return 0
    fi

    return 1
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

mysql_down_upgrade()
{
  check_mysql_version
  
  if [ $? -eq 0 ] ; then
    rm_mysql_upgrade_info
    mysql_upgrade_db
  fi
  return 0
}

## set the dynamic variables because these variables do not validate.
## actually the relay_log_info_repository will be FILE and not need rollback
rollback_repository_cfg()
{
  export MYSQL_PWD="${con_passwd}" 
  
  check_goldendb_version
  if [ $? -eq 0 ] ; then
  
    if [ ! -z "${con_host}" -a ! -z "${con_port}" ] ; then
      select_res=`mysql -u${con_user} -h${con_host} -P${con_port} -e "select @@relay_log_info_repository;"`
    else
      select_res=`mysql -u${con_user} -e "select @@relay_log_info_repository;"`
    fi
  
    result_res=`echo ${select_res} |sed 's/@@relay_log_info_repository //g'`
    if [ "${result_res}" != "FILE" -a "${result_res}" != "TABLE" ] ; then
      print_error "select relay_log_info_repository error, the relay_log_info_repository is ${result_res}."
      return_fail
    elif [ "${result_res}" == "TABLE" ] ; then
    
      if [ ! -z "${con_host}" -a ! -z "${con_port}" ] ; then
        mysql -u${con_user} -h${con_host} -P${con_port} -e "stop slave;set global relay_log_info_repository='FILE';"
      else
        mysql -u${con_user} -e "stop slave;set global relay_log_info_repository='FILE';"
      fi
      
      if [ $? -eq 1 ]; then
        print_error "rollback the relay_log_info_repository to FILE fail."
        return_fail
      fi
      
      print_ok "rollback the relay_log_info_repository to FILE ok."
    fi
  fi
}


#######################################################
# 3. Start of main
#######################################################
print_ok "rollback db start, please wait."

parse_args "$@"

check_db_start_ok

#mysql_down_upgrade

rollback_repository_cfg

print_ok "rollback db end and ok."

echo 0

exit 0
