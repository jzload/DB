#!/bin/bash

# use kw user to run this script

main()
{
	#set -x
	#debug=true

	DEFAULT_CODE_HOME=${HOME}/DB
	DEFAULT_KW_URL="http://10.46.150.101:8080/goldendb-DB-trunk"

	script_file_path=${0}
	script_file_name=${script_file_path##*/}

	get_var ${*}

	set_environment

	clean_create_kw
	prepare_cmake
	do_cmake
	make_get_kwout
	run_kw
	get_kwreport_12
	clean_tmp_files
}


get_var()
{
	check_show_help_exit ${*}

	while [ -n "${1}" ]
	do
	    case "${1}" in
			"-c"|"-code_home") check_follow_opt "code_home" ${1} ${2}
				CODE_HOME=${2}
				shift ;;
			"-f"|"-src_files") check_follow_opt "src_files" ${1} ${2}
				shift
				src_files=${*}
				break ;;
	        *) log_error "${1} is not an option !"
				log_error "exec './${script_file_name} -help' to get usage!"
				exit 1 ;;
	    esac
	    shift
	done

	get_not_set_value

	log_debug "${CODE_HOME}"
	log_debug "${src_files}"
}

check_show_help_exit()
{
	if [[ ${1}x == "-h"x ]] ||
	   [[ ${1}x == "help"x ]] ||
	   [[ ${1}x == "-help"x ]] ||
	   [[ ${1}x == "--help"x ]]
	then
		show_help_info_exit ${*}
	fi
}

check_follow_opt()
{
	if [ "${3}"x == x ]
	then
		log_error "${1} must follow '${2}'!"
		log_error "exec './${script_file_name} -help' to get usage!"
		exit 1
	fi
}

get_not_set_value()
{
	if [ ${CODE_HOME}x == x ]
	then
		CODE_HOME=${DEFAULT_CODE_HOME}
	fi
}


set_environment()
{
	export PATH=$PATH:${HOME}/kwuser/bin
	export KLOCWORK_LICENSE_FILE=27000@10.42.171.61:27000@10.42.171.55 
	source /etc/profile
}


clean_create_kw()
{
	cd ${CODE_HOME}
	rm -rf ./table
	rm -rf *.out
	rm -rf .kwlp .kwps
	rm -rf kwreports
	mkdir -m 777 kwreports

	kwcheck create --url ${KW_URL}
}


prepare_cmake()
{
	cd ${CODE_HOME}
	rm -rf build

	mkdir build
	mkdir build/goldendb_db
	cd build/goldendb_db
	mkdir data bin lib etc log share boost
	cp -rf ../../boost/boost_1_59_0.tar.gz boost/
	cd data
	mkdir data binlog relaylog tmp redo
}


do_cmake()
{
	cd ${CODE_HOME}/build

	cmake .. \
	-DCMAKE_INSTALL_PREFIX=${CODE_HOME}/build/goldendb_db \
	-DMYSQL_DATADIR=${CODE_HOME}/build/goldendb_db/data \
	-DMYSQL_UNIX_ADDR=${CODE_HOME}/build/goldendb_db/bin/mysql.sock \
	-DDOWNLOAD_BOOST=1 \
	-DWITH_BOOST=${CODE_HOME}/build/goldendb_db/boost \
	-DDEFAULT_CHARSET=utf8 \
	-DDEFAULT_COLLATION=utf8_bin \
	-DWITH_EXTRA_CHARSETS:STRING=all \
	-DWITH_MYISAM_STORAGE_ENGINE=1 \
	-DWITH_INNOBASE_STORAGE_ENGINE=1 \
	-DWITH_INNODB_MEMCACHED=ON       \
	-DWITHOUT_TOKUDB_STORAGE_ENGINE=1 \
	-DWITH_EMBEDDED_SERVER:BOOL=OFF \
	-DWITHOUT_NDBCLUSTER_STORAGE_ENGINE=1 \
	-DENABLED_LOCAL_INFILE=1 \
	-DCMAKE_BUILD_TYPE=debug \
	-DCMAKE_C_FLAGS="-O0 -g3" \
	-DCMAKE_CXX_FLAGS="-O0 -g3" \
	-DCMAKE_C_FLAGS_DEBUG="-O0 -g3" \
	-DCMAKE_CXX_FLAGS_DEBUG="-O0 -g3"
}


make_get_kwout()
{
	cd ${CODE_HOME}/build

	make clean
	kwinject -o kw.out make install -j64

	cp kw.out ${CODE_HOME}
}


run_kw()
{
	cd ${CODE_HOME}
	kwcheck run -b kw.out -F detailed --report kwreports/kwreport.txt ${src_files}
}


get_kwreport_12_old()
{
	grep -E "\\(1\\:Critical\\)|\\(2\\:Error\\)" ${CODE_HOME}/kwreports/kwreport.txt > \
		${CODE_HOME}/kwreports/kwreport_12.txt  || touch ${CODE_HOME}/kwreports/kwreport_12.txt
}


get_kwreport_12()
{
	touch ${CODE_HOME}/kwreports/kwreport_12.txt

	lines_1234=`grep -rnE "\\(1\\:Critical\\)|\\(2\\:Error\\)|\\(3\\:Warning\\)|\\(4\\:Review\\)" \
				${CODE_HOME}/kwreports/kwreport.txt | cut -d ":" -f 1`

	lines_12=`grep -rnE "\\(1\\:Critical\\)|\\(2\\:Error\\)" ${CODE_HOME}/kwreports/kwreport.txt \
				| cut -d ":" -f 1`

	log_debug "${lines_1234}"
	log_debug "${lines_12}"
	
	if [ "${lines_1234}"x == x ]
	then
		log_info "kw result is empty!"
		> ${CODE_HOME}/kwreports/kwreport_12.txt
		return 0
	fi

	if [ "${lines_12}"x == x ]
	then
		log_info "kw result of Critical and Error is empty!"
		> ${CODE_HOME}/kwreports/kwreport_12.txt
		return 0
	fi

	L_1234=(${lines_1234})
	L_12=(${lines_12})

	cnt_1234=0
	cnt_12=0

	for line_num in ${L_1234[@]}
	do
		if [ ${L_12[${cnt_12}]} -lt ${line_num} ]
		then
			log_error "maybe kw result has something wrong!"
			exit 1
		fi

		if [ ${L_12[${cnt_12}]} -eq ${line_num} ]
		then
			((start=${line_num} - 1))
			((end_num_pos=${cnt_1234} + 1))

			if [ ${end_num_pos} -eq ${#L_1234[@]} ]
			then
				log_debug "start: ${start}"
				log_debug "end: end_file"
				cat -n ${CODE_HOME}/kwreports/kwreport.txt \
					| tail -n +${start} \
					| grep -v "Summary:" \
					| grep -v "Total Issue(s)$" \
					>> ${CODE_HOME}/kwreports/kwreport_12.txt
			else
				end_num_add1=${L_1234[${end_num_pos}]}
				((end=${end_num_add1} - ${start} - 1))
				log_debug "start: ${start}"
				log_debug "end: ${end}"
				cat -n ${CODE_HOME}/kwreports/kwreport.txt \
					| tail -n +${start} | head -n ${end} \
					>> ${CODE_HOME}/kwreports/kwreport_12.txt
			fi
			((cnt_12++))
		fi

		((cnt_1234++))

		# to break quickly
		if [ ${cnt_12} -eq ${#L_12[@]} ]
		then
			break
		fi
	done
}


clean_tmp_files()
{
    cd ${CODE_HOME}
    rm -rf ./table
    rm -rf *.out
    rm -rf .kwlp .kwps
    rm -rf build
}


show_help_info_exit()
{
	if [ ${2}x == x ]
	then
		monitor_help
		exit 0
	else
		log_error "input error!"
		log_error "exec './${script_file_name} -help' to get usage!"
		exit 1
	fi
}

monitor_help()
{
	echo "usage: "
	echo "	./${script_file_name} <[-c CODE_HOME] [-f src_file1 src_file2 ...]>"
	echo "	./${script_file_name} -h | help | -help | --help"
	echo "param: "
	echo "	'-c'  is the same as '-code_home'"
	echo "	'-f'  is the same as '-src_files'"
	echo "note: "
	echo "	code_home: code home of DB, default ${DEFAULT_CODE_HOME}."
	echo "	src_files: src files or dirents of DB, default is all files in the project."
	echo "example: "
	echo "	./${script_file_name} -c ${HOME}/DB -f client sql/mysqld.cc sql/handler.cc"
	echo "	./${script_file_name} -f sql/mysqld.cc sql/handler.cc"
	echo "	./${script_file_name} -c ${HOME}/DB -f sql/mysqld.cc"
	echo "	./${script_file_name} -help"
	echo "notice: "
	echo "	check KW of all files in the project may be slow!"
}


log_info()
{
	echo "["`date +'%Y-%m-%d %H:%M:%S'`"] INFO: ${*}" 
}

log_error()
{
	echo "["`date +'%Y-%m-%d %H:%M:%S'`"] ERROR: ${*}" 1>&2
}

log_debug()
{
	if [ "${debug}"x == "true"x ]
	then
		echo ${*}
	fi
}

main ${*}
