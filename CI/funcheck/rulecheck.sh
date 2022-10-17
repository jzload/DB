#!/bin/bash

regularfile=$1
codediff=$2
output=$3

checkfiles=`grep -E '^.*\.(c|cc|cpp|h)$' $codediff | grep -Ev '^.*(/ds_os/|/mysql/).*$' | grep -Ev '^.*/ut/.*$' | grep -Ev '^(\./){0,1}(sql/|ssl/|os/).*$'`

echo "$checkfiles"

echo "$checkfiles" | wc -l 

if [ "$checkfiles" = "" ]
then
	echo > $output
else
	grep -E -H -n -f $regularfile $checkfiles | grep -Ev '^.*:[0-9]+:\s*//.*$' > $output
fi

exit 0

