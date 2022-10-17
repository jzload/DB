#!/bin/bash

#Put diff result data
arr=()

#Parsing diff file
parse_file()
{
    LD_IFS=$IFS
    IFS=$'\n'

    git diff `git log --pretty=oneline -2 | tail -n 1 | awk -F' ' '{print $1}'` > ${HOME}/db_core/kwreports/diff_file
    #INFILE=$output/diff_file
    INFILE=${HOME}/db_core/kwreports/diff_file
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

#matching arr
find_arr()
{
    local flag_findfile=false
    check_rm=false
    #INarr:arr
    for arr_num in ${!arr[@]}
    do
        #INfile:JD_FILE
        if [ "$JD_FILE" == "${arr[$arr_num]}"  -o "$flag_findfile" = true ] ; then
            flag_findfile=true
            if [[ ${arr[$((arr_num+1))]} == *[!0-9]* ]]; then
                check_rm=false
                flag_findfile=false
                break
            fi
            #INnum:JD_NUM
            if [[ ${arr[$((arr_num+1))]} == "$JD_NUM" ]]; then
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

#Screening results
ro_kw() {
    LD_IFS=$IFS
    IFS=$'\n'
    DIFFFILE=${HOME}/db_core/kwreports/kwreport_123.txt
    OUTFILE=${HOME}/db_core/kwreports/out.txt
    rm -rf $OUTFILE
    touch $OUTFILE

    local file_num=0
    for lines in $(cat $DIFFFILE)
    do
        file_num=$((file_num+1))
        JD_FILE=`echo ${lines} | awk -F' ' '{print $3}' | cut -b 15- | awk -F':' '{print $1}'`
        JD_NUM=`echo ${lines} | awk -F' ' '{print $3}' | cut -b 15- | awk -F':' '{print $2}'`
        find_arr
        if [ $check_rm = true ] ; then
            echo ${lines}>>$OUTFILE
        fi
    done

    mv $OUTFILE $DIFFFILE
    IFS=$LD_IFS
}

mod_report(){
    sed -i "s/Critical/DB_Critical/g" ${HOME}/db_core/kwreports/kwreport.txt
    sed -i "s/Error/DB_Error/g" ${HOME}/db_core/kwreports/kwreport.txt
    sed -i "s/Warning/DB_Warning/g" ${HOME}/db_core/kwreports/kwreport.txt
}

diffile=${HOME}/db_core/codediff
srcfiles=`grep -E '.c\$|.cc\$|.cpp\$' ${diffile}`
echo $srcfiles

cd ${HOME}/db_core
rm -rf .kwlp .kwps
rm -rf kwreports
mkdir -m 777 kwreports

kwcheck create --url ${KW_URL}

rm -rf my_build
cp -r BUILD/ my_build
cd my_build
sed -i "s/make -j4 /make -j16 /g" make_mysql.sh

kwshell << EOF
sh make_mysql.sh
exit
EOF

cd ${HOME}/db_core

kwcheck disable STRONG.TYPE.ASSIGN STRONG.TYPE.ASSIGN.ARG STRONG.TYPE.ASSIGN.CONST STRONG.TYPE.ASSIGN.INIT STRONG.TYPE.ASSIGN.RETURN STRONG.TYPE.EXTRACT STRONG.TYPE.JOIN.CMP STRONG.TYPE.JOIN.CONST STRONG.TYPE.JOIN.EQ STRONG.TYPE.JOIN.OTHER

kwcheck run -F detailed --report ${HOME}/db_core/kwreports/kwreport.txt $srcfiles

grep -E "\\(1\\:Critical\\)|\\(2\\:Error\\)|\\(3\\:Warning\\)" ${HOME}/db_core/kwreports/kwreport.txt > ${HOME}/db_core/kwreports/kwreport_123.txt  || touch ${HOME}/db_core/kwreports/kwreport_123.txt

parse_file
ro_kw
mod_report
