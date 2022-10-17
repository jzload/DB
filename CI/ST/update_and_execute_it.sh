#!/bin/sh

MainDir=$(cd `dirname $0`;pwd)
export user_home=$MainDir
export output_path=$MainDir/output
export test_user=mysql_it2
export file_name=ZXCLOUD-GOLDENDB-ALL-DBVTRUNK.zip
export file_prefix=allmaster
export mysql_test_name=DB/mysql-test

cd $user_home/DB

git pull

cd $user_home

rm -rf $user_home/deploy_it.sh 

rm -rf $user_home/execute_it.sh

echo $user_home/  | xargs -n 1 cp -r $user_home/DB/CI/ST/deploy_it.sh 

echo $user_home/  | xargs -n 1 cp -r $user_home/DB/CI/ST/execute_it.sh 


chmod 755 $user_home/deploy_it.sh

chmod 755 $user_home/execute_it.sh

sh $user_home/deploy_it.sh

unset user_home
unset output_path
unset test_user
unset file_name
unset file_prefix
unset mysql_test_name
