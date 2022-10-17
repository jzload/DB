#!/bin/sh

#############################################
# 增加定时启动脚本，目前是 show_log_backup.sh
#############################################

# 启动间隔，单位分钟
interval=5

function main()
{
    mkdir $HOME/task 2>/dev/null
    local crontab_name=$HOME/task/mytabs

    crontab -l >${crontab_name}
    crontab -r

    chmod +x ${HOME}/bin/rotate_slow_log.sh

    if [ -s ${crontab_name} ]
    then
        sed -i "/bin\/rotate_slow_log.sh/d" ${crontab_name}
    fi

    echo "*/${interval} * * * * source $HOME/.bash_profile && sh ${HOME}/bin/rotate_slow_log.sh >/dev/null 2>&1" >>${crontab_name}
    echo "Add task[rotate_slow_log.sh] success!"

    crontab ${crontab_name}
}

main
