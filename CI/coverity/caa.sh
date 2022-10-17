#!/bin/bash
#sh caa.sh /root/output/covdir /root/caa
covdir=$1
caadir=$2
#generate cva file
$COV_PATH/cov-export-cva --dir $covdir --output-file $caadir/db.cva