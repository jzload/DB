#!/bin/sh

export begin_time=$(echo `date +%s`)

echo 'begin_time='`date +%Y-%m-%d\ %T`

if [ ${output_path}x != x ]
then
  rm -rf $output_path/*
fi

if [ ${file_name}x != x ]
then
  rm -rf $file_name
fi

rm -rf $user_home/$test_user/mysql-test

echo $user_home/$test_user/  | xargs -n 1 cp -r $mysql_test_name

$user_home/jfrog rt dl --flat=true goldendb-snapshot-generic/DB/$file_prefix/last.txt
for line in `cat last.txt`; do echo $line; export $line; done
$user_home/jfrog rt dl --flat=true goldendb-snapshot-generic/DB/$file_prefix/$buildnum/$file_name

echo $user_home/$test_user/ | xargs -n 1 cp -v $file_name

sh execute_it.sh $test_user |tee $output_path/mysqltest1.log

echo 'Execute "perl mysql-test-run.pl" finish !'; 

# Get testcase_num
export testcase_num=$(cat $output_path/mysqltest*.log |grep '\[ .... ]'|wc -l);

# Get testcase_fail_num
export fail_num=$(cat $output_path/mysqltest*.log |grep '\[ fail ]'|wc -l);

export datetime=$(echo `date +%Y%m%d%H%M%S`);

# File name format 
export result_file_namel='DetailReport.xml';
export result_file_name=$output_path/$result_file_namel

# Start generating "DetailReport.xml"
echo 'Start generating "DetailReport.xml"'
echo '<?xml version="1.0"?>' >$result_file_name
echo '  <testsuites>' >>$result_file_name
echo '    <testsuite name="'MYSQL_A_D_IT'" tests="'$testcase_num'" fixtures="MySQL" crashes="0" skips="0" errors="0" failures="'$fail_num'">' >>$result_file_name

# Get testcase_name¡¢testcase_result¡¢testcase_time
cat $output_path/mysqltest1.log |grep '\[ .... ]' |awk  -F'[][]' '{print $3","$4","$5}'|sed s/[[:space:]]//g >> $output_path/temp_result.txt

for i in $(cat $output_path/temp_result.txt)
  do
   testcase_name=$(echo $i |awk -F',' '{print $1}');   

   testcase_result=$(echo $i |awk -F',' '{print $2}');

   testcase_timel=$(echo $i |awk -F',' '{print $3}');
  
   if [ ! -n "$testcase_timel" ]; then
      testcase_time=''
   else
      testcase_time=$(echo "scale=3; $testcase_timel/1000" |bc | awk '{printf "%.3f", $0}')
   fi

# Abnormal flow of "testcase"
   if [ "$testcase_result" = "fail" ] ; then 
      testcase_time=0 
      lfile_name1=$(echo $i |awk -F',' '{print $1}'|awk -F'.' '{print $2}');  
   echo '    <testcase name="'$testcase_name'" fixture="MySQL-TRUNK-IT" time="'$testcase_time'" filename="'$testcase_name'" execresult="'$testcase_result'">' >>$result_file_name;
   echo '      <failure message="null expected">Failing test(s): '$testcase_name' ! Please diff ".../r/'$lfile_name1'.result" and  ".../r/'$lfile_name1'.reject",find out the difference!</failure>' >>$result_file_name;

   else

# Normal flow of "testcase"  
    echo '    <testcase name="'$testcase_name'" fixture="MySQL-TRUNK-IT" time="'$testcase_time'" filename="'$testcase_name'" execresult="'$testcase_result'">' >>$result_file_name;
   
   fi

   echo '    </testcase>' >>$result_file_name;  

  done

echo '  </testsuite>' >>$result_file_name;
echo '</testsuites>' >>$result_file_name;

# End of generation "DetailReport.xml"
echo 'End of generation "DetailReport.xml"'

export end_time=$(echo `date +%s`)
echo 'end_time='`date +%Y-%m-%d\ %T`

# Execution time calculation  

echo 'exec_time='$(($end_time-$begin_time))'s'


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
echo '<casesTotalNum>'$testcase_num'</casesTotalNum>' >>$tr_result_file_name
echo '<casesSuccNum>'$casessuccnum'</casesSuccNum>' >>$tr_result_file_name
echo '<casesFailNum>'$fail_num'</casesFailNum>' >>$tr_result_file_name
echo '</test>' >>$tr_result_file_name
echo '</tests>' >>$tr_result_file_name
echo '</result>' >>$tr_result_file_name

# End of generation "TestReport.xml"

export end_time=$(echo `date +%s`)
echo 'end_time='`date +%Y-%m-%d\ %T`

##### End ##########################################################################
