#!/bin/bash

#
##############################################
# check the slave status
##############################################

#######################################################
# 1. Define variables here
#######################################################

# input option variables
con_user=""
con_passwd=""
con_host=""
con_port=""
tmp_result_file="tmp_slave_result.txt"


#######################################################
# 2. Define function here
#######################################################

# show usage of install script
usage()
{   echo "  upgrade_db.sh [options] [--] optstring parameters"
    echo "  -u,--user             User to use when connecting to server"
    echo "  -p,--password         Password to use when connecting to server"
    echo "  -h,--host             Host to use when connecting to server"
    echo "  -P,--port             Port to use when connecting to server"
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
    if [ -f ${tmp_result_file} ] ; then
        rm -rf ${tmp_result_file}
    fi
}

return_fail()
{
    clear_tmp_file
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

start_slave()
{
    export MYSQL_PWD="${con_passwd}"
    if [ ! -z "${con_host}" -a ! -z "${con_port}" ] ; then
        mysql -u${con_user} -h${con_host} -P${con_port} -e "start slave;"
        if [ "$?" -ne 0 ]; then
            print_error "execute start slave error."
            return_fail
        fi
    else
        mysql -u${con_user} -e "start slave;"
        if [ "$?" -ne 0 ]; then
            print_error "execute start slave error."
            return_fail
        fi
    fi

    print_ok "execute start slave ok."
}

#check slave status
check_slave_status()
{
    export MYSQL_PWD="${con_passwd}"
    if [ ! -z "${con_host}" -a ! -z "${con_port}" ] ; then
        mysql -u${con_user} -h${con_host} -P${con_port} -e"show slave status\G" > ${tmp_result_file}
        if [ "$?" -ne 0 ]; then
            print_error "execute show slave status error."
            return_fail
        fi
    else
        mysql -u${con_user} -e"show slave status\G" > ${tmp_result_file}
        if [ "$?" -ne 0 ]; then
            print_error "execute show slave status error."
            return_fail
        fi
    fi
    
    IO_thread_status=`cat ${tmp_result_file}|grep Slave_IO_Running`
    IO_thread_status=`echo $IO_thread_status`
    if [ "$IO_thread_status" != 'Slave_IO_Running: Yes' ]; then
        print_error "the slave IO status is error, status is $IO_thread_status."
        return_fail
    fi
    
    SQL_thread_status=`cat ${tmp_result_file}|grep Slave_SQL_Running | grep -v Slave_SQL_Running_State`
    SQL_thread_status=`echo $SQL_thread_status`
    if [ "$SQL_thread_status" != 'Slave_SQL_Running: Yes'   ]; then
        print_error "the slave SQL status is error, status is $SQL_thread_status."
        return_fail
    fi
    
    rm -rf ${tmp_result_file}
    print_ok "check slave IO and SQL status ok."
}

#######################################################
# 3. Start of main
#######################################################
print_ok "check slave status start, please wait."

parse_args "$@"

start_slave

check_slave_status

print_ok "check slave status end and ok."

echo 0

exit 0




