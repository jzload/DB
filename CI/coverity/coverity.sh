#!/bin/bash
#sh coverity.sh all ZXCLOUD-GOLDENDB-DBPROXYVTRUNK /home/output /root/db_core
#sh coverity.sh inc ZXCLOUD-GOLDENDB-DBPROXYVTRUNK /home/output /root/db_core
citype=$1
stream=$2
output=$3
codedir=$4


#coverity server info
host=10.229.16.238
port=8080
user=goldendb
passwd=goldendb-ci

#Code modified by oneself in git-diff
arr=()
#in or out flag
check_rm=false
#Parsing files
parse_file()
{
    LD_IFS=$IFS
    IFS=$'\n'

    git diff `git log --pretty=oneline -2 | tail -n 1 | awk -F' ' '{print $1}'` > $output/diff_file
    INFILE=$output/diff_file
    #file number
    local deal_line_num=0
    local fin_add=0
    #arr index
    local index=0

    local flag_add=false
    local flag_at=false
    for dealline in $(cat $INFILE)
    do
        deal_line_num=$((deal_line_num+1))
        #Analysis modified file
        if [ "${dealline:0:3}" == "+++" -o "$flag_add" = true ] ; then
            fin_add=$((fin_add+1))
            if [ "$fin_add" == "1" ] ; then
                if [ "${dealline:0:3}" == "+++" -o "$flag_add" = false ] ; then
                    arr[$index]=${dealline#*/}
                    flag_add=true
                fi
            else
                if [ "${dealline:0:3}" == "+++" -o "$flag_add" = false ] ; then
                    index=$((index+1))
                    arr[$index]=${dealline#*/}
                    flag_add=true
                fi
            fi
            #Analysis modified number
            if [ "${dealline:0:2}" == "@@" -o "$flag_at" = true ] ; then
                if [ "${dealline:0:2}" == "@@" -o "$flag_at" = false ] ; then
                    temp=`echo ${dealline} | awk -F' ' '{print $3}' | awk -F',' '{print $1}' | tr -d '+'`
                    temp=$((temp-1))
                    flag_at=true
                fi

                if [ "${dealline:0:1}" == "+" ] ; then
                    index=$((index+1))
                    temp=$((temp+1))
                    arr[index]=$temp
                fi

                if [ "${dealline:0:1}" == " " ] ; then
                    temp=$((temp+1))
                fi

                if [ "${dealline:0:4}" == "diff" ] ; then
                    flag_add=false
                    flag_at=false
                fi
            fi
        fi
    done
    IFS=$LD_IFS
}

find_arr()
{
    local flag_findfile=false
    #INarr:arr
    for arr_num in ${!arr[@]}
    do
        #INfile:choose_filename
        if [ "$choose_filename" == "${arr[$arr_num]}"  -o "$flag_findfile" = true ] ; then
            flag_findfile=true
            if [[ ${arr[$((arr_num+1))]} == *[!0-9]* ]]; then
                check_rm=false
                flag_findfile=false
                break
            fi
            #INnum:choose_filenum
            if [[ ${arr[$((arr_num+1))]} == "$choose_filenum" ]]; then
                check_rm=true
                flag_findfile=false
                break
            fi
        fi
        #search to end
        if [ "${#arr[@]}" -eq $((arr_num+1)) ] ; then
           check_rm=false
        fi
    done
}

find_event()
{
    LD_IFS=$IFS
    IFS=$'\n'

    NEWFILE=$OUTFILE
    FINFILE=$output/choose.txt

    local fin_line_num=0
    local fin_num_merge=0

    #remove num
    local fin_remove_begin=0
    local fin_remove_end=0
    local fin_mer=false
    #find act flag
    local flag_path=false
    local flag_num=false
    local flag_end=false
    for chooseline in $(cat $NEWFILE)
    do
        fin_line_num=$((fin_line_num+1))
        if [ "${chooseline:6:10}" == '"mergeKey"' -o "$fin_mer" = true ] ; then
            fin_num_merge=$((fin_num_merge+1))
            if [ $fin_num_merge = 1 ] ; then
                sed -i "$(($fin_line_num-1))d" $FINFILE
            fi

            if [ "${chooseline:6:10}" == '"mergeKey"' -o "$fin_mer" = false ] ; then
                fin_remove_begin=$((fin_line_num))
                fin_mer=true
                fin_flag_end=0
            fi

            if [ "${chooseline:6:23}" == '"mainEventFilePathname"' -o "$flag_path" = true ] ; then
                if [ "${chooseline:6:23}" == '"mainEventFilePathname"' ] ; then
                    #Problematic documents
                    choose_filename=`echo ${chooseline} | awk -F' ' '{print $3}' | cut -b 16- |tr -d '"'|tr -d ','`
                    flag_path=true
                    check_rm=false
                fi

                if [ "${chooseline:6:21}" == '"mainEventLineNumber"' -o "$flag_num" = true ] ; then
                    if [ "${chooseline:6:21}" == '"mainEventLineNumber"' ] ; then
                        #Problematic num
                        choose_filenum=`echo ${chooseline} | awk -F' ' '{print $3}' |tr -d ','`
                        #Find out if we changed the code
                        find_arr
                        flag_num=true
                    fi

                    if [ "${chooseline:8:28}" == '"subcategoryLongDescription"' -o "$flag_end" = true ] ; then
                         flag_end=true
                         fin_flag_end=$((fin_flag_end+1))
                         if [ $fin_flag_end = 4 ] ; then
                            if [ "$check_rm" = true ] ; then
                                fin_remove_end=$((fin_line_num))
                                flag_end=false
                                flag_num=false
                                flag_path=false
                                fin_mer=false

                                if [ "${chooseline:2:1}" == "]" ] ; then
                                    sed -n "$(($fin_remove_begin-1)),$(($fin_remove_end))p" $NEWFILE >> $FINFILE
                                else
                                    sed -n "$(($fin_remove_begin-1)),$(($fin_remove_end-1))p" $NEWFILE >> $FINFILE
                                fi
                            else
                                if [ "${chooseline:2:1}" == "]" ] ; then
                                    echo ${chooseline}>>$FINFILE
                                fi
                                flag_end=false
                                flag_num=false
                                flag_path=false
                                fin_mer=false
                            fi
                         fi
                    fi
                fi
            fi
        else
            echo ${chooseline}>>$FINFILE
        fi
    done
    IFS=$LD_IFS
}

#Remove low from coverity report
mv_coverity_low()
{
    LD_IFS=$IFS
    IFS=$'\n'

    #Input and output file
    FILENAME=$output/coverity.json
    OUTFILE=$output/remove_low.txt

    #Fields to query
    value_mergeKey='"mergeKey"'
    value_Low='"impact" : "Low"'
    value_Medium='"impact" : "Medium"'
    value_High='"impact" : "High"'

    line_num=0
    remove_begin=0
    remove_end=0

    #Step tag value
    variable=false
    flag_low=false
    flag_High=false
    flag_Medium=false

    #The number of times a merge was found
    num_merge=0

    for line in $(cat $FILENAME)
    do
        line_num=$((line_num+1))
        #find mergeKey
        if [ "${line:6:10}" == "$value_mergeKey" -o "$variable" = true ] ; then
            num_merge=$((num_merge+1))
            if [ $num_merge = 1 ] ; then
                sed -i "$(($line_num-1))d" $OUTFILE
            fi

            #Save the number of lines found in merkey
            if [ "${line:6:10}" == "$value_mergeKey" -o "$variable" = false ] ; then
                remove_begin=$((line_num))
                variable=true
                flag_end=0
            fi

            #Search low until the end of the segment
            if [ "${line:8:16}" == "$value_Low" -o "$flag_low" = true ] ; then
                flag_low=true
                flag_end=$((flag_end+1))
                if [ $flag_end = 8 ] ; then
                    #The end is ']' reserved
                    if [ "${line:2:1}" == "]" ] ; then
                        echo ${line}>>$OUTFILE
                    fi
                        #Cycle flag bit end
                        variable=false
                        flag_low=false
                        remove_end=$line_num
                fi
            fi

            #Search High until the end of the segment
            if [ "${line:8:17}" == "$value_High" -o "$flag_High" = true ] ; then
                flag_High=true
                flag_end=$((flag_end+1))
                if [ $flag_end = 8 ] ; then
                    variable=false
                    flag_High=false
                    remove_end=$((line_num))

                    #The current high segment is output to the file
                    if [ "${line:2:1}" == "]" ] ; then
                        sed -n "$(($remove_begin-1)),$(($remove_end))p" $FILENAME >> $OUTFILE
                    else
                        sed -n "$(($remove_begin-1)),$(($remove_end-1))p" $FILENAME >> $OUTFILE
                    fi
                fi
            fi

            #Search Medium until the end of the segment
            if [ "${line:8:19}" == "$value_Medium" -o "$flag_Medium" = true ] ; then
                flag_Medium=true
                flag_end=$((flag_end+1))
                if [ $flag_end = 8 ] ; then
                    variable=false
                    flag_Medium=false
                    remove_end=$((line_num))

                    if [ "${line:2:1}" == "]" ] ; then
                        sed -n "$(($remove_begin-1)),$(($remove_end))p" $FILENAME >> $OUTFILE
                    else
                        sed -n "$(($remove_begin-1)),$(($remove_end-1))p" $FILENAME >> $OUTFILE
                    fi
                fi
            fi
        else
            #Other lines reserved
            echo ${line}>>$OUTFILE
        fi
    done

    parse_file
    find_event

    mv $FINFILE $output/coverity.json
    IFS=$LD_IFS
}


#clean complie env

#config coverity
$COV_PATH/cov-configure --comptype gcc --compiler cc --template
$COV_PATH/cov-configure --comptype g++ --compiler c++ --template

#capture build info
rm -rf my_build
cp -r BUILD/ my_build
cd my_build
sed -i "s/make -j4 /make -j16 /g" make_mysql.sh
$COV_PATH/cov-build --emit-complementary-info --dir $output/covdir  sh make_mysql.sh

#analyze build-log
#tail -500 $output/covdir/build-log.txt

if [ $citype == "all" ];then
#all
#analyze
$COV_PATH/cov-analyze --dir $output/covdir --strip-path $codedir --cpp --all --rule --enable-constraint-fpp --enable-callgraph-metrics --enable-fnptr --enable-virtual --enable USER_POINTER --enable DC.STRING_BUFFER --enable ENUM_AS_BOOLEAN --enable FORMAT_STRING_INJECTION --enable UNENCRYPTED_SENSITIVE_DATA --enable WEAK_GUARD --enable WEAK_PASSWORD_HASH --enable HARDCODED_CREDENTIALS --enable AUDIT.SPECULATIVE_EXECUTION_DATA_LEAK --enable INTEGER_OVERFLOW --enable MIXED_ENUMS --enable RISKY_CRYPTO --enable COM.ADDROF_LEAK --enable COM.BSTR.ALLOC --enable COM.BSTR.BAD_COMPARE --enable COM.BSTR.NE_NON_BSTR --enable FLOATING_POINT_EQUALITY --enable VCALL_IN_CTOR_DTOR --enable MISSING_LOCK --enable SELF_ASSIGN --enable MISSING_COPY_OR_ASSIGN --enable COPY_WITHOUT_ASSIGN --enable ASSIGN_NOT_RETURNING_STAR_THIS --enable HFA -co MISSING_LOCK:lock_inference_threshold:50 -co OVERRUN:check_nonsymbolic_dynamic:true -co STACK_USE:max_total_use_bytes:40000 --enable-parse-warnings --coding-standard-config  $COV_PATH/../config/coding-standards/cert-cpp/cert-cpp-all.config --coding-standard-config  $COV_PATH/../config/coding-standards/cert-c/cert-c-all.config --enable ODR_VIOLATION

#commit snapshot to server
$COV_PATH/cov-commit-defects  --host $host --port $port --dir $output/covdir --description "$stream" --stream "$stream" --user $user --password $passwd
#generate report
curl --user $user:$passwd "http://$host:$port/api/viewContents/issues/v1/goldendbhigh?projectId=$stream" > $output/coverity.json
else
    #inc
    #find change files
    grep -E '\.c$|\.cc$|\.cpp$' $codedir/codediff > $output/changefiles.txt
    cat $output/changefiles.txt
    #analyze
    if [ -s $output/changefiles.txt ];then
        cd $codedir/
        $COV_PATH/cov-run-desktop --dir $output/covdir --delete-stale-tus  --ignore-uncapturable-inputs true --set-new-defect-owner false --json-output-v6 $output/coverity.json --host $host --port $port --user $user --password $passwd --stream $stream --reference-snapshot latest @@$output/changefiles.txt

        mv_coverity_low
    else
        echo "{}" > $output/coverity.json
    fi
fi
