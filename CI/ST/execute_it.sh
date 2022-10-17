#!/bin/sh

export deploy_user=$1

export user_pwd=$1

export user_group=$1

export suite_name=$2

chown $deploy_user:$user_group $user_home/$1/$file_name

chown -R $deploy_user:$user_group $user_home/$1/mysql-test

su - $deploy_user <<!

killall mysqld -u $deploy_user

rm -rf bin  data  etc  lib  log share

mkdir -p ~/setup/

rm -rf ~/setup/*

mv $file_name ~/setup

cd ~/setup

unzip $file_name > /dev/null 2>&1

chmod 755 ~/setup/autoupdate/*.sh

./autoupdate/manual_install.sh > /dev/null 2>&1


cd ~/mysql-test

chmod 755 ~/mysql-test/*.pl

./mysql-test-run.pl --force --max-test-fail=0  --parallel=10 --port-base=14200

exit
!
