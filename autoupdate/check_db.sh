#!/bin/sh

##############################################
# check if the db can upgrade 
##############################################


#######################################################
# 1. Define variables here
#######################################################

# define variable
con_user=""
con_passwd=""
con_host=""
con_port=""
version=""


#######################################################
# 2. Define function here
#######################################################

# show usage of install script
usage()
{   echo "  check_db.sh [options] [--] optstring parameters"
    echo "  -u,--user             User to use when connecting to server"
    echo "  -p,--password         Password to use when connecting to server"
    echo "  -h,--host             Host to use when connecting to server"
    echo "  -P,--port             Port to use when connecting to server"
    echo "  -v,--version          the version of the current db"
    echo "  --help                show usage of install"
}


# print ok ouput: time msg <GOOD>~
print_ok()
{
    if [ -n "$1" ] ; then
        datetime=$(date "+%Y-%m-%d %H:%M:%S") > /dev/null        
        echo "$datetime $1 <GOOD>~"
    fi
}

# print error output: time msg <FAIL>~
print_error()
{
    if [ -n "$1" ] ; then
        datetime=$(date "+%Y-%m-%d %H:%M:%S") > /dev/null        
        echo "$datetime $1 <FAIL>~"
    fi
}

# parse all the arguments
parse_args()
{
    ARGS=`getopt -o u:p:h:P:v: -l user:,password:,host:,port:,version:,help -- "$@"`

    if [ "$?" -ne 0 ]; then
        print_error "execute getopt function error, see --help."
        echo -1
        exit 1
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
                echo -1
                exit 1 ;;
        esac
        shift
    done

    if [ -z "$con_user" -o -z "$con_passwd" ] ; then
        print_error "execute shell without user or passwd, see --help."
        echo -1
        exit 1
    fi
    
    if [ -z "$version" ] ; then
        print_error "execute shell without version, see --help."
        echo -1
        exit 1
    fi
    
    print_ok "check all the arguments ok."
}

check_not_root_user()
{
    user=$(env |grep USER |cut -d "=" -f 2)
    if [ "$user" == "root" ] ; then
        print_error "the user is root, can not upgrade db."
        echo -1
        exit 1
    fi
    print_ok "check user is not root ok."
}

check_dbagent_stop_ok()
{
    program_dbagent=`pgrep  dbagent  -u ${USER}|head -n 1`
    if [ ! -z $program_dbagent ] ; then
        print_error " The program dbagent is running, can not upgrade db."
        echo -1
        exit 1
    fi
    print_ok "check dbagent stop ok."
}

# check if the db is start or can start ok
check_db_start_ok()
{
    program_mysql=`pgrep  mysqld  -u ${USER}|head -n 1`
    if [ -z $program_mysql ] ; then
        mysql.server start
        if [ $? -ne 0 ] ; then
            print_error "start mysql error, can not upgrade db."
            echo -1
            exit 1
        fi
  
        program_mysql=`pgrep  mysqld  -u ${USER}|head -n 1`
        if [ -z $program_mysql ] ; then
            print_error "start mysql error, can not upgrade db."
            echo -1
            exit 1
        fi
    fi
    print_ok "check db start ok."
}


check_db_version_ok()
{
    export MYSQL_PWD="${con_passwd}"
    if [ ! -z "$con_host" -a ! -z "$con_port" ] ; then
        select_res=`mysql -u${con_user} -h${con_host} -P${con_port} -e "select @@goldendb_version;"`
    else
        select_res=`mysql -u${con_user} -e "select @@goldendb_version;"`
    fi
    
    version_res=`echo $select_res |sed 's/@@goldendb_version //g'`
    if [ "${version_res}" != "${version}" ] ; then
        print_error "check db version error, the current version is ${version_res}."
        echo -1
        exit 1
    fi
    print_ok "check db version ok."
}



#######################################################
# 3. Start of main
#######################################################
print_ok "check db start, please wait."

parse_args "$@"

check_not_root_user

check_dbagent_stop_ok

check_db_start_ok

check_db_version_ok

print_ok "check db end and ok."

echo 0

exit 0
