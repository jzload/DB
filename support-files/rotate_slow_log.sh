#!/bin/bash

#############################################
# 1.按照文件大小或者文件时间备份慢日志文件。
# 2.定期删除过期的慢日志备份文件
#############################################

PATH=$HOME/bin:$PATH

mysql_login="mysql -uroot -proot"
log_backup_strategy=0
log_backup_value=200
log_backup_expiration=7

slow_log_full_name="${HOME}/log/slow.log"
slow_log_path="${HOME}/log"
slow_log_file="slow.log"
slow_log_name="slow"
slow_log_ext="log"

function get_config_info()
{
    # Get the variables from configure file.
    local db_user_info=${HOME}/etc/dbagent_info/db_user.info
    local dbagent_ini=${HOME}/etc/dbagent.ini

    local dbagent_user1_name=`sed '/^dbagent_user1_name=/!d;s/.*=//' ${db_user_info}`
    local dbagent_user1_password=`sed '/^dbagent_user1_password=/!d;s/.*=//' ${db_user_info}`

    local mysql_passwd=`dbtool -decutil2 ${dbagent_user1_password} | tail -n 1 | awk -F: '{print $2}'`
    mysql_login="mysql -u${dbagent_user1_name} -p${mysql_passwd}"

    log_backup_value=`sed '/^slow_log_backup_size=/!d;s/.*=//' ${dbagent_ini}`
    log_backup_expiration=`sed '/^slow_log_expiration_day=/!d;s/.*=//' ${dbagent_ini}`
}

function get_file_name_info()
{
    slow_log_full_name=`$mysql_login \
                -e "show variables like '%slow_query_log_file%'" \
                2>/dev/null | awk '{print $2}' | tail -1`

    # Assume slow_log_full_name is /home/db/log/slow.log
    slow_log_path=${slow_log_full_name%/*}        # value is '/home/db/log'
    slow_log_file=${slow_log_full_name##*/}       # value is 'slow.log'
    slow_log_name=${slow_log_file%.*}             # value is 'slow'
    slow_log_ext=${slow_log_full_name##*.}        # value is 'log'
}

function rename_log_file()
{
    if [ ! -f ${slow_log_full_name} ]
    then
        return 1
    fi

    local rename_log=false

    if [ ${log_backup_strategy} -eq 0 ]
    then
        # Get the size(bytes) of the slow.log
        local file_size=`wc -c ${slow_log_full_name} | awk '{print $1'}`
        local max_size=`expr ${log_backup_value} \* 1024 \* 1024`

        if ((${file_size} >= ${max_size}))
        then
            rename_log=true;
        fi
    else
        # Get the earliest time of slow.log
        local _head=`grep "# Time:" $slow_log_full_name | head -n 1`
        if [ ! -z "$_head" ]
        then
            local _d1=`echo $_head|awk '{print $3}'`
            local _t1=`date -d "$_d1" +%s`
            local _t2=`date -d "-${log_backup_value} hour" +%s`

            if [ $_t2 -ge $_t1 ]
            then
                rename_log=true;
            fi
        fi
    fi

    if [ ${rename_log} = true ]
    then
        # 1. rename slow.log
        # 2. flush slow logs
        local new_file_suffix=$(date +%F+%H%M%S)
        local new_file=${slow_log_path}/${slow_log_name}_${new_file_suffix}.${slow_log_ext}
        mv ${slow_log_full_name} ${new_file}

        $mysql_login -e "flush slow logs" >/dev/null 2>&1
    fi
}

function del_expiration_log()
{
    local files=`ls ${slow_log_path}/${slow_log_name}_20*.${slow_log_ext} 2>/dev/null`
    for file in ${files}
    do
        local file_name_with_suffix=${file##*/}
        local file_name=${file_name_with_suffix%.*}
        local file_datetime=${file_name##*_}
        local file_date=${file_datetime%+*}

        local _d1=`date -d "${file_date}" +%s`
        local _d2=`date -d "-${log_backup_expiration} day" +%s`

        if [ $_d2 -gt $_d1 ]
        then
            rm ${file} 2>/dev/null
        fi
    done
}

function main()
{
    get_config_info

    get_file_name_info

    rename_log_file

    del_expiration_log
}

main

