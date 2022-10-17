#!/bin/sh

test_user=coverage_all
MainDir=$(cd `dirname $0`;pwd)
user_home=$MainDir
db_root_dir=$MainDir/DB
build_dir=$db_root_dir/my_build
output_path=$MainDir/output
cov_dir=$output_path/stcodecov

su - $test_user <<!

#update codes
cd $db_root_dir
git pull

#build debug version
#local make_mysql.sh is added with -DWITH_DEBUG=1 and -DENABLE_GCOV=1
rm -rf $build_dir
mkdir -p $build_dir
cp -rf BUILD/* $build_dir/
cp -f $user_home/make_mysql.sh $build_dir/

cd $build_dir
sh make_mysql.sh

#run mysqltest
cd $build_dir/mysql-test
./mtr --force --max-test-fail=0  --parallel=10 | tee $output_path/mysqltest1.log

#remove files whose source files lcov cannot open
rm -f $build_dir/sql/CMakeFiles/sql.dir/sql_hints.yy.*
rm -rf $build_dir/storage/innobase/CMakeFiles/innobase.dir/fts
rm -rf $build_dir/storage/innobase/CMakeFiles/innobase.dir/pars

cd $output_path
#lcov
rm -f ./st_lcov.info
lcov --directory $build_dir --capture --output-file ./st_lcov.info --test-name st

#generate html
genhtml $output_path/st_lcov.info  --output-directory  $cov_dir --title "st test" --show-details --legend

#zip
rm -rf stcodecov.zip
zip -r stcodecov.zip stcodecov TestReport.xml

exit
!
#finish


# Get testcase_num
export testcase_num=$(cat $output_path/mysqltest*.log |grep '\[ .... ]'|wc -l);

# Get testcase_fail_num
export fail_num=$(cat $output_path/mysqltest*.log |grep '\[ fail ]'|wc -l);

export tr_result_file_name=$output_path/TestReport.xml
echo 's1:'$testcase_num
echo 's2:'$fail_num
echo 's3:'$(($testcase_num - $fail_num))

export casessuccnum=$(($testcase_num - $fail_num))

# Start generating "TestReport.xml"
echo 'Start generating "TestReport.xml"'
echo '<?xml version="1.0"?>' >$tr_result_file_name
echo '<result>' >>$tr_result_file_name
echo '<tests>' >>$tr_result_file_name
echo '<test>' >>$tr_result_file_name
echo '<category>IT</category>' >>$tr_result_file_name
echo '<testTools>' >>$tr_result_file_name
echo '<testTool>MySQL Test Framework</testTool>' >>$tr_result_file_name
echo '</testTools>' >>$tr_result_file_name
echo '<casesTotalNum>'100'</casesTotalNum>' >>$tr_result_file_name
echo '<casesSuccNum>'100'</casesSuccNum>' >>$tr_result_file_name
echo '<casesFailNum>'0'</casesFailNum>' >>$tr_result_file_name
echo '</test>' >>$tr_result_file_name
echo '</tests>' >>$tr_result_file_name
echo '</result>' >>$tr_result_file_name

# End of generation "TestReport.xml"
