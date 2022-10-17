#!/bin/sh

main()
{
	#set -x
	#debug=true
	get_os_type
	get_var ${*}
	check_cmd_match_opt

	if [[ "${cmd_type}"x == x ]] || \
	   [[ "${cmd_type}"x == "-h"x ]] || \
	   [[ "${cmd_type}"x == "-help"x ]] 
	then
		gdbpd_help
	elif [ "${cmd_type}"x == "-locks"x ]
	then
		gdbpd_locks 
	elif [ "${cmd_type}"x == "-lock-waits"x ]
	then
		gdbpd_lock_waits 
	elif [ "${cmd_type}"x == "-latest-deadlock"x ]
	then
		gdbpd_latest_deadlock 
	elif [ "${cmd_type}"x == "-processlist"x ]
	then
		gdbpd_processlist 
	elif [ "${cmd_type}"x == "-recovery-progress"x ]
	then
		gdbpd_recovery_progress 
	elif [ "${cmd_type}"x == "-ruid"x ]
	then
		gdbpd_ruid 
	elif [ "${cmd_type}"x == "-slowlog"x ]
	then
		gdbpd_slow_log 
	elif [ "${cmd_type}"x == "-stack"x ]
	then
		gdbpd_stack 
	elif [ "${cmd_type}"x == "-table-statinfo"x ]
	then
		gdbpd_table_statinfo  
	elif [ "${cmd_type}"x == "-trx"x ]
	then
		gdbpd_trx 
	else
		# never go to here, because of func 'check_cmd_type'
		log_error "exec './gdbpd -help' to get usage!"
		exit 1
	fi
}


# get operating system type
get_os_type()
{
    OS_TYPE=`uname`
	
    case ${OS_TYPE} in
        HP-UX)
            OS_TYPE=hpux
            ;;
        AIX)
            OS_TYPE=aix
            ;;
        SunOS)
            OS_TYPE=sunos
            ;;
        Linux)
            OS_TYPE=linux
            ;;
        *)
            OS_TYPE=un-supported
            ;;
    esac

	# maybe a un-known unix.
    if [ $OS_TYPE = "un-supported" ] ; then
		  echo "unsupport os. exit."
		  exit 2
    fi
	
    os_type=$OS_TYPE
	
    if [ $os_type = "linux" ] ; then
	if [ -s "/etc/SuSE-release" ] ; then
		os_type="suselinux"
	elif [ -s "/etc/redhat-release" ] ; then
		os_type="redhatlinux"
	elif [ -s "/etc/cgsl-release" ] ; then
		os_type="cgsllinux"
        elif [ -s "/etc/os-release" -a -s "/etc/lsb-release" ] ; then
		os_type="ubuntu"  
	fi
    fi
}

get_var()
{
	cmd_type=${1}
	check_cmd_type
	shift
	
	while [ -n "${1}" ]
	do
	    case "${1}" in
			"-u"|"-username") check_follow_opt "username" ${1} ${2}
				username=${2}
				shift ;;
			"-p"|"-password") check_follow_opt "password" ${1} ${2}
				password=${2}
				shift ;;
			"-c"|"-connection_id") check_follow_opt "connection_id" ${1} ${2}
				connection_id=${2}
				check_type_int "connection_id" ${connection_id}
				shift ;;
			"-d"|"-database_name") check_follow_opt "database_name" ${1} ${2}
				database_name=${2}
				shift ;;
			"-t"|"-table_name") check_follow_opt "table_name" ${1} ${2}
				table_name=${2}
				shift ;;
			"-st"|"-start_time") check_follow_opt "start_time" ${1} ${2}
				start_time=${2}
				check_type_time "start_time" ${start_time}
				shift ;;
			"-et"|"-end_time") check_follow_opt "end_time" ${1} ${2}
				end_time=${2}
				check_type_time "end_time" ${end_time}
				shift ;;
	        *) log_error "${1} is not an option !"
				log_error "exec './gdbpd -help' to get usage!"
				exit 1 ;;
	    esac
	    shift
	done
}

check_cmd_type()
{
	if [[ "${cmd_type}"x != x ]] && \
	   [[ "${cmd_type}"x != "-h"x ]] && \
	   [[ "${cmd_type}"x != "-help"x ]] && \
	   [[ "${cmd_type}"x != "-locks"x ]] && \
	   [[ "${cmd_type}"x != "-lock-waits"x ]] && \
	   [[ "${cmd_type}"x != "-latest-deadlock"x ]] && \
	   [[ "${cmd_type}"x != "-processlist"x ]] && \
	   [[ "${cmd_type}"x != "-recovery-progress"x ]] && \
	   [[ "${cmd_type}"x != "-ruid"x ]] && \
	   [[ "${cmd_type}"x != "-slowlog"x ]] && \
	   [[ "${cmd_type}"x != "-stack"x ]] && \
	   [[ "${cmd_type}"x != "-table-statinfo"x ]] && \
	   [[ "${cmd_type}"x != "-trx"x ]]
	then
		log_error "cmd_type '${cmd_type}' wrong!"
		log_error "exec './gdbpd -help' to get usage!"
		exit 1
	fi
}

check_follow_opt()
{
	if [ "${3}"x == x ]
	then
		log_error "${1} must follow '${2}'!"
		log_error "exec './gdbpd -help' to get usage!"
		exit 1
	fi
}

check_type_int()
{
	expr ${2} + 0 &>/dev/null
	# note: when ${2} is 0, ${?} is 1 (not 0)
	not_zero_num_flag=${?}
	log_debug ${not_zero_num_flag}
	if [[ ${not_zero_num_flag} -ne 0 ]] || \
	   ( [[ ${not_zero_num_flag} -eq 0 ]] && [[ ${2} -lt 0 ]] )
	then
		log_error "type of '${1}' must 'positive int', check your input!"
		log_error "exec './gdbpd -help' to get usage!"
		exit 1
	fi
}

check_type_time()
{
	timestamp=`date -d "${2}" +%s`
	if [ ${?} -ne 0 ]
	then
		log_error "'${1}' time format error, check your input!"
		log_error "exec './gdbpd -help' to get usage!"
		exit 1
	fi
}


check_cmd_match_opt()
{
	if [[ "${cmd_type}"x == x ]] || \
	   [[ "${cmd_type}"x == "-h"x ]] || \
	   [[ "${cmd_type}"x == "-help"x ]] || \
	   [[ "${cmd_type}"x == "-recovery-progress"x ]] || \
	   [[ "${cmd_type}"x == "-stack"x ]]
	then
		# ./gdbpd
		# ./gdbpd -h
		# ./gdbpd -help
		# ./gdbpd -recovery-progress
		# ./gdbpd -stack
		check_unmatch_opt ${cmd_type} "username" ${username} 
		check_unmatch_opt ${cmd_type} "password" ${password} 
		check_unmatch_opt ${cmd_type} "connection_id" ${connection_id} 
		check_unmatch_opt ${cmd_type} "database_name" ${database_name} 
		check_unmatch_opt ${cmd_type} "table_name" ${table_name} 
		check_unmatch_opt ${cmd_type} "start_time" ${start_time} 
		check_unmatch_opt ${cmd_type} "end_time" ${end_time} 
	elif [[ "${cmd_type}"x == "-locks"x ]] || \
	     [[ "${cmd_type}"x == "-lock-waits"x ]] || \
		 [[ "${cmd_type}"x == "-processlist"x ]]
	then
		# ./gdbpd -locks -u username <[-p password] [-c connection_id]>
		# ./gdbpd -lock-waits -u username <[-p password] [-c connection_id]>
		# ./gdbpd -processlist -u username <[-p password] [-c connection_id]>
		check_required_opt ${cmd_type} "username" ${username} 
		check_unmatch_opt ${cmd_type} "database_name" ${database_name} 
		check_unmatch_opt ${cmd_type} "table_name" ${table_name} 
		check_unmatch_opt ${cmd_type} "start_time" ${start_time} 
		check_unmatch_opt ${cmd_type} "end_time" ${end_time} 
	elif [ "${cmd_type}"x == "-latest-deadlock"x ]
	then
		# ./gdbpd -latest-deadlock -u username <[-p password]>"
		check_required_opt ${cmd_type} "username" ${username} 
		check_unmatch_opt ${cmd_type} "connection_id" ${connection_id} 
		check_unmatch_opt ${cmd_type} "database_name" ${database_name} 
		check_unmatch_opt ${cmd_type} "table_name" ${table_name} 
		check_unmatch_opt ${cmd_type} "start_time" ${start_time} 
		check_unmatch_opt ${cmd_type} "end_time" ${end_time} 
	elif [[ "${cmd_type}"x == "-ruid"x ]] || \
		 [[ "${cmd_type}"x == "-table-statinfo"x ]]
	then
		# ./gdbpd -ruid -u username <[-p password] [-d database_name] \
		#                              [-t table_name]>
		# ./gdbpd -table-statinfo -u username <[-p password] [-d database_name] \
		# 							   [-t table_name]>
		check_required_opt ${cmd_type} "username" ${username} 
		check_unmatch_opt ${cmd_type} "connection_id" ${connection_id} 
		check_unmatch_opt ${cmd_type} "start_time" ${start_time} 
		check_unmatch_opt ${cmd_type} "end_time" ${end_time} 
	elif [ "${cmd_type}"x == "-slowlog"x ]
	then
		# ./gdbpd -slowlog -u username <[-p password] [-c connection_id] \
		#                                  [-st start_time] [-et end_time]>
		check_required_opt ${cmd_type} "username" ${username} 
		check_unmatch_opt ${cmd_type} "database_name" ${database_name} 
		check_unmatch_opt ${cmd_type} "table_name" ${table_name} 
	elif [ "${cmd_type}"x == "-trx"x ]
	then
		# ./gdbpd -trx -u username <[-p password] [-c connection_id]>
		check_required_opt ${cmd_type} "username" ${username} 
		check_unmatch_opt ${cmd_type} "database_name" ${database_name} 
		check_unmatch_opt ${cmd_type} "table_name" ${table_name} 
		check_unmatch_opt ${cmd_type} "start_time" ${start_time} 
		check_unmatch_opt ${cmd_type} "end_time" ${end_time} 
	else
		# never go to here, because of func 'check_cmd_type'
		log_error "exec './gdbpd -help' to get usage!"
		exit 1
	fi
}

check_required_opt()
{
	if [ "${3}"x == x ]
	then
		log_error "'${1}' must with '${2}'!"
		log_error "exec './gdbpd -help' to get usage!"
		exit 1
	fi
}

check_unmatch_opt()
{
	if [ "${3}"x != x ]
	then
		log_error "'${1}' must not with '${2}'!"
		log_error "exec './gdbpd -help' to get usage!"
		exit 1
	fi
}


gdbpd_trx()
{
	check_mysqld_running
	check_mysql_passwd ${password}

	get_new_file "tmp_gdbpd_trx_"
	tmpfile1_path=${new_file}

	if [ "${connection_id}"x == x ]
	then
		mysql_exec "
			select 
				trx_mysql_thread_id as connection_id, 
				trx_state, 
				trx_id, 
				trx_started, 
				trx_requested_lock_id, 
				trx_wait_started, 
				trx_weight, 
				trx_operation_state, 
				trx_tables_in_use, 
				trx_tables_locked, 
				trx_rows_locked, 
				trx_rows_modified, 
				trx_isolation_level,
				trx_serial_num,
				trx_gtm_gtid,
				INFO as trx_sql
			from information_schema.INNODB_TRX 
			inner join information_schema.PROCESSLIST as plist
			on trx_mysql_thread_id = ID \G;" ${tmpfile1_path}
		print_sql_res "no trx!" ${tmpfile1_path}
	else
		mysql_exec "	
			select 
				trx_mysql_thread_id as connection_id, 
				trx_state, 
				trx_id, 
				trx_started, 
				trx_requested_lock_id, 
				trx_wait_started, 
				trx_weight, 
				trx_operation_state, 
				trx_tables_in_use, 
				trx_tables_locked, 
				trx_rows_locked, 
				trx_rows_modified, 
				trx_isolation_level, 
				trx_serial_num,
				trx_gtm_gtid,
				INFO as trx_sql
			from information_schema.INNODB_TRX 
			inner join information_schema.PROCESSLIST as plist
			on trx_mysql_thread_id = ID 
			where 
				trx_mysql_thread_id = ${connection_id} \G;" ${tmpfile1_path}
		print_sql_res "no trx that connection_id = ${connection_id}!" ${tmpfile1_path}
	fi

	delete_file ${tmpfile1_path}
}

get_new_file()
{
	new_file="./${1}${RANDOM}"
	while [ -f ${new_file} ]
	do
		new_file="./${1}${RANDOM}"
	done
}

delete_file()
{
	file=${1}
	if [ -f ${file} ]
	then
		rm -f ${file}
	fi
}


# NOTE: 'start_time' and 'end_time' use UTC
gdbpd_slow_log()
{
	check_mysqld_running
	check_mysql_passwd ${password}

	check_slow_query_log
	check_slow_query_log_file

	time_lines=`grep -r "# Time: " ${slow_log_path} | cut -c 9-34`
	if [ "${time_lines}"x == x ]
	then
		log_info "no available slow log record!"
		exit 0
	fi

	time_utc_arr=(${time_lines})
	cnt_time_utc=0
	for time_utc in ${time_utc_arr[@]}
	do
		timestamp[${cnt_time_utc}]=`date -d "${time_utc}" +%s` 
		log_debug "${timestamp[${cnt_time_utc}]} ${cnt_time_utc} ${time_utc_arr[${cnt_time_utc}]}"
		((cnt_time_utc++))
	done

	if [ "${start_time}"x != x ]
	then 
		start_timestamp=`date -d "${start_time}" +%s`
	else
		start_timestamp=-1
	fi
	log_debug ${start_timestamp}
	if [ "${end_time}"x != x ]
	then 
		end_timestamp=`date -d "${end_time}" +%s`
	else
		end_timestamp=-1
	fi
	log_debug ${end_timestamp}

	if [[ ${start_timestamp} -gt ${end_timestamp} ]] && \
	   [[ ${end_timestamp} -ne -1 ]]
	then
		log_error "must 'start_time' < 'end_time'!"
		exit 1
	fi

	# -2: not in the range
	# -1: no limit
	# other num: pos num
	find_right "${timestamp[*]}" ${start_timestamp}
	real_start_pos=${right}
	log_debug ${real_start_pos}

	if [ ${real_start_pos} -eq -2 ]
	then
		log_info "no slow log matched!"
		exit 0
	elif [ ${real_start_pos} -eq -1 ]
	then
		start_line=1
	else
		start_line=`grep -rn ${time_utc_arr[${real_start_pos}]} \
						${slow_log_path} | cut -d ":" -f 1`
		log_debug ${start_line}
	fi

	# -2: not in the range
	# -1: no limit
	# other num: pos num
	find_left "${timestamp[*]}" ${end_timestamp}
	real_end_pos=${left}
	log_debug ${real_end_pos}

	if [ ${real_end_pos} -eq -2 ]
	then
		log_info "no slow log matched!"
		exit 0
	elif [[ ${real_end_pos} -eq -1 ]] || \
		 [[ ${real_end_pos} -eq `expr ${#timestamp[@]} - 1` ]]
	then
		end_line=-1
	else
		end_line=0
		((real_end_pos++))
		log_debug "real_end_pos: ${real_end_pos}"
		end_line_add1=`grep -rn ${time_utc_arr[${real_end_pos}]} \
						${slow_log_path} | cut -d ":" -f 1`
		log_debug ${end_line} ${end_line_add1}
	fi

	#exit 0

	if [ "${connection_id}"x == x ]
	then
		if [ ${end_line} -eq -1 ]
		then
			# why '^Tcp port: ' no use
			cat -n ${slow_log_path} | tail -n +${start_line} \
				| grep -v "started with:$" \
				| grep -v "Id Command    Argument$" \
				| grep -v "Tcp port:"
		else
			((end_line=${end_line_add1} - ${start_line} - 1))
			cat -n ${slow_log_path} | tail -n +${start_line} | head -n ${end_line} \
				| grep -v "started with:$" \
				| grep -v "Id Command    Argument$" \
				| grep -v "Tcp port:"
		fi
	else
		# get all id line number
		id_lines=`grep -rn "\]  Id:" ${slow_log_path} | cut -d ":" -f 1`
		# get particular id (connection_id) line number 
		id_part_lines=`grep -rn "\]  Id:" ${slow_log_path} \
						| grep " ${connection_id}$\|:${connection_id}$" \
						| cut -d ":" -f 1`
		if [ "${id_lines}"x == x ]
		then
			log_info "no available slow log record!"
			exit 0
		fi
		if [ "${id_part_lines}"x == x ]
		then
			log_info "no slow log matched!"
			exit 0
		fi

		lines=(${id_lines})
		part_lines=(${id_part_lines})
		#echo "lines: ${lines[@]}"
		#echo "part_lines: ${part_lines[@]}"

		cnt_lines=0
		cnt_part=0
		have_record="false"
		for line_num in ${lines[@]} 
		do 
			if [ ${part_lines[${cnt_part}]} -lt ${line_num} ]
			then
				log_error "maybe slow log record wrong!"
				exit 1
			fi

			if [ ${part_lines[${cnt_part}]} -eq ${line_num} ]
			then
				((start=${line_num} - 1))
				((end_num_pos=${cnt_lines} + 1))

				do_flag="true"
				if [ ${start_line} -gt ${start} ]	
				then
					do_flag="false"
				fi
				if [ ${end_line} -ne -1 ]
				then
					if [ ${end_num_pos} -eq ${#lines[@]} ]
					then
						do_flag="false"
					else
					#if [ ${end_num_pos} -lt ${#lines[@]} ]
					#then
						end_num_add=`expr ${lines[${end_num_pos}]} - 1`
						if [ ${end_line_add1} -lt ${end_num_add} ]
						then
							do_flag="false"
						fi
						log_debug "${end_line_add1} ${end_num_add}"
					fi
				fi
				
				if [ "${do_flag}"x == "true"x ]
				then
					# why '^Tcp port: ' no use
					if [ ${end_num_pos} -eq ${#lines[@]} ]
					then
						log_debug "start: "${start}
						log_debug "end: end_file"
						cat -n ${slow_log_path} | tail -n +${start} \
							| grep -v "started with:$" \
							| grep -v "Id Command    Argument$" \
							| grep -v "Tcp port: "
					else
						end_num_add1=${lines[${end_num_pos}]}
						((end=${end_num_add1} - ${start} - 1))
						log_debug "start: "${start}
						log_debug "end: "${end}
						cat -n ${slow_log_path} | tail -n +${start} \
							| head -n ${end} \
							| grep -v "started with:$" \
							| grep -v "Id Command    Argument$" \
							| grep -v "Tcp port:"
					fi
					have_record="true"
				fi
				((cnt_part++))
			fi

			((cnt_lines++))

			# to break quickly
			if [ ${cnt_part} -eq ${#part_lines[@]} ]
			then
				break
			fi
		done

		if [ "${have_record}"x == "false"x ]
		then
			log_info "no slow log matched!"
		fi

		log_debug ${id_lines}
		log_debug ${id_part_lines}
	fi
}

# find the first greater or equal time
# right:
# 	-2: not in the range
# 	-1: no limit
# 	other num: pos num
find_right()
{
	array=(${1})
	target=${2}
	low=0
	high=`expr ${#array[@]} - 1`
	down=${low}
	up=${high}
	right=-1
	log_debug "right ${right}, low ${low}, high ${high}, down ${down}, up ${up}"

	if [ ${target}x == x ]
	then 
		right=-2
		return 0
	fi

	if [ ${target} -eq -1 ]
	then
		right=-1
		return 0
	fi

	if [ ${target} -gt ${array[${up}]} ]
	then
		right=-2
		log_debug "right ${right}, low ${low}, high ${high}, down ${down}, up ${up}"
		return 0
	fi

	while [ ${low} -lt ${high} ]
	do
		mid=`expr \( ${low} + ${high} \) / 2`
		mid_time=${array[${mid}]}
		if [ ${mid_time} -lt ${target} ] 
		then
			low=`expr ${mid} + 1`
		elif [ ${mid_time} -gt ${target} ] 
		then 
			high=${mid}
		else
			low=${mid}
			break
		fi
	done
	right=${low}
	log_debug "right ${right}, low ${low}, high ${high}, down ${down}, up ${up}"

	less_right=`expr ${right} - 1`
	while [[ ${less_right} -ge ${down} ]] && \
		  [[ ${array[${right}]} -eq ${array[${less_right}]} ]]
	do 
		less_right=`expr ${less_right} - 1`
	done
	right=`expr ${less_right} + 1`
	log_debug "right ${right}, low ${low}, high ${high}, down ${down}, up ${up}"
}

# find the last less or equal time
# left:
# 	-2: not in the range
# 	-1: no limit
# 	other num: pos num
find_left()
{
	array=(${1})
	target=${2}
	low=0
	high=`expr ${#array[@]} - 1`
	down=${low}
	up=${high}
	left=-1
	log_debug "left ${left}, low ${low}, high ${high}, down ${down}, up ${up}"

	if [ ${target}x == x ]
	then 
		left=-2
		return 0
	fi

	if [ ${target} -eq -1 ]
	then
		left=-1
		return 0
	fi

	if [ ${target} -lt ${array[${down}]} ]
	then
		left=-2
		log_debug "left ${left}, low ${low}, high ${high}, down ${down}, up ${up}"
		return 0
	fi
	if [ ${target} -eq ${array[${down}]} ]
	then
		left=0
		log_debug "left ${left}, low ${low}, high ${high}, down ${down}, up ${up}"
		return 0
	fi
	if [ ${target} -ge ${array[${up}]} ]
	then
		left=${up}
		log_debug "left ${left}, low ${low}, high ${high}, down ${down}, up ${up}"
		return 0
	fi

	while [ ${low} -lt ${high} ]
	do
		mid=`expr \( ${low} + ${high} \) / 2`
		mid_time=${array[${mid}]}
		if [ ${mid_time} -lt ${target} ] 
		then
			low=`expr ${mid} + 1`
		elif [ ${mid_time} -gt ${target} ] 
		then 
			high=${mid}
		else
			left=${mid}
			break
		fi
	done
	if [ ${low} -ge ${high} ]
	then
		left=`expr ${low} - 1`
	fi
	log_debug "left ${left}, low ${low}, high ${high}, down ${down}, up ${up}"

	more_left=`expr ${left} + 1`
	while [[ ${more_left} -le ${up} ]] && \
		  [[ ${array[${left}]} -eq ${array[${more_left}]} ]]
	do 
		more_left=`expr ${more_left} + 1`
	done
	left=`expr ${more_left} - 1`
	log_debug "left ${left}, low ${low}, high ${high}, down ${down}, up ${up}"
}


gdbpd_ruid()
{
	check_mysqld_running
	check_mysql_passwd ${password}

	check_performance_schema

	get_new_file "tmp_gdbpd_ruid_"
	tmpfile1_path=${new_file}

	# print_sql_res not use, because result need handle twice
	if [[ "${database_name}"x == x ]] && [[ "${table_name}"x == x ]] 
	then
		mysql_exec "
			select 
				ruid_tab.OBJECT_TYPE as Type,
	       		ruid_tab.OBJECT_SCHEMA as DatabaseName,
				ruid_tab.OBJECT_NAME as TableName, 
				ruid_tab.COUNT_READ as CountRead, 
	      	 	ruid_tab.COUNT_INSERT as CountInsert, 
	      	 	ruid_tab.COUNT_UPDATE as CountUpdate, 
	      	 	ruid_tab.COUNT_DELETE as CountDelete, 
	      	 	ruid_idx.INDEX_NAME as IndexName,
	      	 	ruid_idx.COUNT_STAR as ScanTimes
			from performance_schema.table_io_waits_summary_by_table as ruid_tab
			inner join performance_schema.table_io_waits_summary_by_index_usage as ruid_idx
			on ruid_tab.OBJECT_TYPE = ruid_idx.OBJECT_TYPE 
	    		and ruid_tab.OBJECT_SCHEMA = ruid_idx.OBJECT_SCHEMA 
	    		and ruid_tab.OBJECT_NAME = ruid_idx.OBJECT_NAME \G;" ${tmpfile1_path}
		res_ruid=`cat ${tmpfile1_path} | grep -v \
		   "mysql: \[Warning\] Using a password on the command line interface can be insecure."`
		if [ "${res_ruid}"x == x ]
		then
			echo "no ruid record!"
			delete_file ${tmpfile1_path}
			exit 0
		fi
	elif [[ "${database_name}"x != x ]] && [[ "${table_name}"x == x ]]
	then
		mysql_exec "
			select 
				ruid_tab.OBJECT_TYPE as Type,
	       		ruid_tab.OBJECT_SCHEMA as DatabaseName,
				ruid_tab.OBJECT_NAME as TableName, 
				ruid_tab.COUNT_READ as CountRead, 
	      	 	ruid_tab.COUNT_INSERT as CountInsert, 
	      	 	ruid_tab.COUNT_UPDATE as CountUpdate, 
	      	 	ruid_tab.COUNT_DELETE as CountDelete, 
	      	 	ruid_idx.INDEX_NAME as IndexName,
	      	 	ruid_idx.COUNT_STAR as ScanTimes
			from performance_schema.table_io_waits_summary_by_table as ruid_tab
			inner join performance_schema.table_io_waits_summary_by_index_usage as ruid_idx
			on ruid_tab.OBJECT_TYPE = ruid_idx.OBJECT_TYPE 
	    		and ruid_tab.OBJECT_SCHEMA = ruid_idx.OBJECT_SCHEMA 
	    		and ruid_tab.OBJECT_NAME = ruid_idx.OBJECT_NAME 
			where ruid_tab.OBJECT_SCHEMA = '${database_name}' \G;" ${tmpfile1_path}
		res_ruid=`cat ${tmpfile1_path} | grep -v \
		   "mysql: \[Warning\] Using a password on the command line interface can be insecure."`
		if [ "${res_ruid}"x == x ]
		then
			echo "no ruid record in database: ${database_name}"
			delete_file ${tmpfile1_path}
			exit 0
		fi
	elif [[ "${database_name}"x != x ]] && [[ "${table_name}"x != x ]]
	then
		mysql_exec "
			select 
				ruid_tab.OBJECT_TYPE as Type,
	       		ruid_tab.OBJECT_SCHEMA as DatabaseName,
				ruid_tab.OBJECT_NAME as TableName, 
				ruid_tab.COUNT_READ as CountRead, 
	      	 	ruid_tab.COUNT_INSERT as CountInsert, 
	      	 	ruid_tab.COUNT_UPDATE as CountUpdate, 
	      	 	ruid_tab.COUNT_DELETE as CountDelete, 
	      	 	ruid_idx.INDEX_NAME as IndexName,
	      	 	ruid_idx.COUNT_STAR as ScanTimes
			from performance_schema.table_io_waits_summary_by_table as ruid_tab
			inner join performance_schema.table_io_waits_summary_by_index_usage as ruid_idx
			on ruid_tab.OBJECT_TYPE = ruid_idx.OBJECT_TYPE 
	    		and ruid_tab.OBJECT_SCHEMA = ruid_idx.OBJECT_SCHEMA 
	    		and ruid_tab.OBJECT_NAME = ruid_idx.OBJECT_NAME 
			where ruid_tab.OBJECT_SCHEMA = '${database_name}' 
				and ruid_tab.OBJECT_NAME = '${table_name}' \G;" ${tmpfile1_path}
		res_ruid=`cat ${tmpfile1_path} | grep -v \
		   "mysql: \[Warning\] Using a password on the command line interface can be insecure."`
		if [ "${res_ruid}"x == x ]
		then
			echo "no ruid record in database.table: ${database_name}.${table_name}"
			delete_file ${tmpfile1_path}
			exit 0
		fi
	else
		log_error "can't only tell table name without database name!"
		delete_file ${tmpfile1_path}
		exit 1
	fi

	get_new_file "tmp_gdbpd_ruid_"
	tmpfile2_path=${new_file}
	echo "${res_ruid}" > ${tmpfile2_path}
	
	start_line=1
	contain_lines=10
	total_lines=`cat ${tmpfile2_path} | wc -l`
	count_row=1
	for (( line = start_line; line < total_lines; line += contain_lines )) 
	do
		echo "*************************** ${count_row}. row ***************************"
		index_num=1

		((line_add1 = line + 1))
		((contain_lines_dec3 = contain_lines - 3))
		cat ${tmpfile2_path} | tail -n +${line_add1} | head -n ${contain_lines_dec3}	

		((line_end = line + contain_lines - 1))
		((less_line_end = line_end - 1))

		idxcnt_line=`cat ${tmpfile2_path} | tail -n +${line_end} | head -n 1`
		idx_line=`cat ${tmpfile2_path} | tail -n +${less_line_end} | head -n 1`
		index_name=${idx_line##*: }
		index_count=${idxcnt_line##*: }
		echo "  IndexName${index_num}: ${index_name}"
		echo "  ScanTimes${index_num}: ${index_count}"

		while [ 1 == 1 ]
		do
			if [ ${line_end} -eq ${total_lines} ] 
			then
				break
			else
				((line_add1 = line + 1))
				((line_add2 = line + 2))
				((line_add3 = line + 3))
				((line_end = line + contain_lines - 1))
				((less_line_end = line_end - 1))

				obtype_line=`cat ${tmpfile2_path} | tail -n +${line_add1} | head -n 1`
				schema_line=`cat ${tmpfile2_path} | tail -n +${line_add2} | head -n 1`
				obname_line=`cat ${tmpfile2_path} | tail -n +${line_add3} | head -n 1`
				idxcnt_line=`cat ${tmpfile2_path} | tail -n +${line_end} | head -n 1`
				idx_line=`cat ${tmpfile2_path} | tail -n +${less_line_end} | head -n 1`
				obtype=${obtype_line##*: }
				database=${schema_line##*: }
				table=${obname_line##*: }
				index_name=${idx_line##*: }
				index_count=${idxcnt_line##*: }
				log_debug "obtype: ${obtype}, database: ${database}, table: ${table}"
				log_debug "index_name: ${index_name}, index_count: ${index_count}"
			
				((next_line = line + contain_lines))
				((next_line_add1 = next_line + 1))
				((next_line_add2 = next_line + 2))
				((next_line_add3 = next_line + 3))
				((next_line_end = next_line + contain_lines - 1))
				((less_next_line_end = next_line_end - 1))
				
				next_obtype_line=`cat ${tmpfile2_path} | tail -n +${next_line_add1} | head -n 1`
				next_schema_line=`cat ${tmpfile2_path} | tail -n +${next_line_add2} | head -n 1`
				next_obname_line=`cat ${tmpfile2_path} | tail -n +${next_line_add3} | head -n 1`
				next_idxcnt_line=`cat ${tmpfile2_path} | tail -n +${next_line_end} | head -n 1`
				next_idx_line=`cat ${tmpfile2_path} | tail -n +${less_next_line_end} | head -n 1`
				next_obtype=${next_obtype_line##*: }
				next_database=${next_schema_line##*: }
				next_table=${next_obname_line##*: }
				next_index_name=${next_idx_line##*: }
				next_index_count=${next_idxcnt_line##*: }
				log_debug "next_obtype: ${next_obtype}, next_database: ${next_database}, next_table: ${next_table}"
				log_debug "next_index_name: ${next_index_name}, next_index_count: ${next_index_count}"

				if [[ ${obtype}x == ${next_obtype}x ]] && \
				   [[ ${database}x == ${next_database}x ]] && \
				   [[ ${table}x == ${next_table}x ]]
				then
					# cat ${tmpfile2_path} | tail -n +${less_next_line_end} | head -n 2	
					next_index_num=`expr ${index_num} + 1`
					echo "  IndexName${next_index_num}: ${next_index_name}"
					echo "  ScanTimes${next_index_num}: ${next_index_count}"
					((line += contain_lines))
					((index_num++))
				else 
					break
				fi
			fi
		done
		if [ ${line_end} -eq ${total_lines} ] 
		then
			break
		fi
		((count_row++))
	done

	delete_file ${tmpfile1_path}
	delete_file ${tmpfile2_path}
}


gdbpd_stack()
{
	check_mysqld_running
	pid=${ps_info[1]}

        if [ "$os_type" = "suselinux" ]; then
	  gstack ${pid}
        else
          pstack ${pid}
        fi
}

gdbpd_locks() 
{
	check_mysqld_running
	check_mysql_passwd ${password}

	check_innodb_status_output_locks
	check_performance_schema
	check_setup_instruments_enabled

	parpare_innodb_locks

	# to show 'innodb locks'
	count_separator=0
	for separator_line in ${separator_lines[@]}
	do
		if [ ${separator_line}x == x ]
		then
			continue
		fi
		odd=`expr ${count_separator} % 2`
		if [ ${odd} -eq 1 ]
		then
			content_line=`expr ${separator_line} - 1`
			content=`cat ${tmpfile1_path} | tail -n +${content_line} | head -n 1` 
			if [ "${content}"x == "TRANSACTIONS"x ]
			then
				# next_start_cnt in the range with no doubt 
				next_start_cnt=`expr ${count_separator} + 1`
				while [ ${separator_lines[${next_start_cnt}]}x == x ]
				do
					((next_start_cnt++))
				done
				((start=${separator_line} + 1))
				((end=${separator_lines[${next_start_cnt}]} - ${start}))
				log_debug "start ${start}, end ${end}"
				cat ${tmpfile1_path} | tail -n +${start} | head -n ${end} > ${tmpfile2_path}
			fi
		fi
		((count_separator++))
	done
	echo -e "+++++++++++++++++++++++++++++++\c"
	echo -e "  INNODB LOCKS  \c"
	echo    "++++++++++++++++++++++++++++++++"
	if [ "${connection_id}"x == x ] 
	then
		cat ${tmpfile2_path}
	else
		thread_id_line=`grep -rn "^MySQL thread id ${connection_id}, OS thread handle" \
			${tmpfile2_path} | cut -d ":" -f 1`
		trx_lines=(`grep -rn "^---TRANSACTION" ${tmpfile2_path} | cut -d ":" -f 1`)
		log_debug "thread_id_line: ${thread_id_line}, trx_lines: ${trx_lines[*]}"
		# -2: not in the range
		# -1: no limit
		# other num: pos num
		find_left "${trx_lines[*]}" ${thread_id_line}
		left_range=${left}
		find_right "${trx_lines[*]}" ${thread_id_line}
		right_range=${right}
		log_debug "left: ${left}, right: ${right}"
		if [ ${left_range} -ge 0 ]
		then
			start_line=${trx_lines[${left_range}]}
			if [ ${right_range} -lt 0 ]
			then
				cat ${tmpfile2_path} | tail -n +${start_line}
			else
				((end_line=${trx_lines[${right_range}]} - ${start_line} - 1))
				cat ${tmpfile2_path} | tail -n +${start_line} | head -n ${end_line}
			fi
		else
			echo "no innodb lock that connection_id = ${connection_id}" 
		fi
	fi
	echo " "
		
	# to show 'MDL' 
	echo -e "++++++++++++++++++++++++++++++++++++\c"
	echo -e "  MDL  \c"
	echo    "++++++++++++++++++++++++++++++++++++"

	get_new_file "tmp_gdbpd_locks_mdl_"
	tmpfile3_path=${new_file}

	if [ "${connection_id}"x == x ] 
	then
		mysql_exec "
			select 
				OBJECT_TYPE, 
				OBJECT_SCHEMA, 
				OBJECT_NAME, 
				OBJECT_INSTANCE_BEGIN, 
				LOCK_TYPE, 
				LOCK_DURATION, 
				LOCK_STATUS, 
				SOURCE, 
				PROCESSLIST_ID as connection_id, 
				OWNER_THREAD_ID, 
				OWNER_EVENT_ID 
			from performance_schema.metadata_locks 
			inner join performance_schema.threads
			on OWNER_THREAD_ID = THREAD_ID \G;" ${tmpfile3_path}
		print_sql_res "no MDL" ${tmpfile3_path}
	else
		mysql_exec "
			select 
				OBJECT_TYPE, 
				OBJECT_SCHEMA, 
				OBJECT_NAME, 
				OBJECT_INSTANCE_BEGIN, 
				LOCK_TYPE, 
				LOCK_DURATION, 
				LOCK_STATUS, 
				SOURCE, 
				PROCESSLIST_ID as connection_id, 
				OWNER_THREAD_ID, 
				OWNER_EVENT_ID 
			from performance_schema.metadata_locks 
			inner join performance_schema.threads
			on OWNER_THREAD_ID = THREAD_ID 
			where PROCESSLIST_ID = ${connection_id} \G;" ${tmpfile3_path}
		print_sql_res "no MDL that connnection_id = ${connection_id}" ${tmpfile3_path}
	fi

	delete_file ${tmpfile3_path}
	end_innodb_locks
}

parpare_innodb_locks()
{
	get_new_file "tmp_gdbpd_innodb_locks_"
	tmpfile1_path=${new_file}

	mysql_exec "show engine innodb status \G;" ${tmpfile1_path}
	separator_lines=(`grep -rn "^-*-$" ${tmpfile1_path} | cut -d ":" -f 1`)
	log_debug ${separator_lines[@]}
	log_debug "sizeof separator_lines: ${#separator_lines[@]}"
	for ((count_separator=0;count_separator<${#separator_lines[@]};count_separator++))
	do
		next_count_separator=`expr ${count_separator} + 1`	
		expect_next=`expr ${separator_lines[${count_separator}]} + 2`
		if [ ${expect_next}x != ${separator_lines[${next_count_separator}]}x ]
		then
			unset separator_lines[${count_separator}]
		else
			((count_separator++))
		fi
	done
	log_debug ${separator_lines[@]}
	log_debug "sizeof separator_lines: ${#separator_lines[@]}"

	get_new_file "tmp_gdbpd_innodb_locks_"
	tmpfile2_path=${new_file}

	log_debug "tmpfile1_path: ${tmpfile1_path}"
	log_debug "tmpfile2_path: ${tmpfile2_path}"
}

end_innodb_locks()
{
	delete_file ${tmpfile1_path}
	delete_file ${tmpfile2_path}
}


gdbpd_lock_waits() 
{
	check_mysqld_running
	check_mysql_passwd ${password}

	check_performance_schema
	check_setup_instruments_enabled

	get_new_file "tmp_gdbpd_lock_waits_innodb_"
	tmpfile1_path=${new_file}
	get_new_file "tmp_gdbpd_lock_waits_mdl_"
	tmpfile2_path=${new_file}

	if [ "${connection_id}"x == x ] 
	then
		echo -e "++++++++++++++++++++++++++++++ \c"
		echo -e "INNODB LOCK WAITS \c"
		echo    "++++++++++++++++++++++++++++++"
		mysql_exec "
			select 
				w.requesting_engine_transaction_id as requesting_trx_id, 
				t_req.trx_mysql_thread_id as connection_id, 
				t_req.trx_started as requesting_trx_started,
				t_req.trx_wait_started as requesting_trx_wait_started,
				t_req.trx_query as requesting_query_sql, 
				t_req.trx_serial_num as requesting_trx_serial_num,
				t_req.trx_gtm_gtid as requesting_trx_gtm_gtid,
				w.requesting_engine_lock_id as requesting_lock_id, 
				l_req.lock_mode as requesting_lock_mode, 
				l_req.lock_type as requesting_lock_type, 
				l_req.object_schema as requesting_lock_schema, 
				l_req.object_name as requesting_lock_table, 
				l_req.index_name as requesting_lock_index, 
				l_req.lock_data as requesting_lock_data, 
				w.blocking_engine_transaction_id as blocking_trx_id, 
				t_blk.trx_mysql_thread_id as blocking_thread_id, 
				t_blk.trx_started as blocking_trx_started,
				t_blk.trx_serial_num as blocking_trx_serial_num,
				t_blk.trx_gtm_gtid as blocking_trx_gtm_gtid,
				w.blocking_engine_lock_id as blocking_lock_id, 
				l_blk.lock_mode as blocking_lock_mode, 
				l_blk.lock_type as blocking_lock_type, 
				l_blk.object_schema as blocking_lock_schema, 
				l_blk.object_name as blocking_lock_table, 
				l_blk.index_name as blocking_lock_index, 
				l_blk.lock_data as blocking_lock_data  
			from performance_schema.data_lock_waits as w
			inner join performance_schema.data_locks as l_req
				on w.requesting_engine_transaction_id = l_req.engine_transaction_id
				   and w.requesting_engine_lock_id = l_req.engine_lock_id 
			inner join performance_schema.data_locks as l_blk 
				on w.blocking_engine_transaction_id = l_blk.engine_transaction_id
				   and w.blocking_engine_lock_id = l_blk.engine_lock_id
			inner join information_schema.innodb_trx as t_req 
				on w.requesting_engine_transaction_id = t_req.trx_id
			inner join information_schema.innodb_trx as t_blk 
				on w.blocking_engine_transaction_id = t_blk.trx_id 
			where t_req.trx_state = 'LOCK WAIT' \G;" ${tmpfile1_path}
		print_sql_res "no innodb lock waits!" ${tmpfile1_path}
		echo " "
		echo -e "++++++++++++++++++++++++++++++ \c"
		echo -e "  MDL  LOCK WAITS \c"
		echo    "++++++++++++++++++++++++++++++"
		mysql_exec "
			select 
				OBJECT_TYPE, 
				OBJECT_SCHEMA, 
				OBJECT_NAME, 
				OBJECT_INSTANCE_BEGIN, 
				LOCK_TYPE, 
				LOCK_DURATION, 
				LOCK_STATUS, 
				SOURCE, 
				PROCESSLIST_ID as connection_id, 
				OWNER_THREAD_ID, 
				OWNER_EVENT_ID 
			from performance_schema.metadata_locks 
			inner join performance_schema.threads
			on OWNER_THREAD_ID = THREAD_ID
			where LOCK_STATUS='PENDING' \G;" ${tmpfile2_path}
		print_sql_res "no MDL waits!" ${tmpfile2_path}
	else
		echo -e "++++++++++++++++++++++++++++++ \c"
		echo -e "INNODB LOCK WAITS \c"
		echo    "++++++++++++++++++++++++++++++"
		mysql_exec "
			select 
				w.requesting_engine_transaction_id as requesting_trx_id, 
				t_req.trx_mysql_thread_id as connection_id, 
				t_req.trx_started as requesting_trx_started,
				t_req.trx_wait_started as requesting_trx_wait_started,
				t_req.trx_query as requesting_query_sql, 
				t_req.trx_serial_num as requesting_trx_serial_num,
				t_req.trx_gtm_gtid as requesting_trx_gtm_gtid,
				w.requesting_engine_lock_id as requesting_lock_id, 
				l_req.lock_mode as requesting_lock_mode, 
				l_req.lock_type as requesting_lock_type, 
				l_req.object_schema as requesting_lock_schema, 
				l_req.object_name as requesting_lock_table, 
				l_req.index_name as requesting_lock_index, 
				l_req.lock_data as requesting_lock_data, 
				w.blocking_engine_transaction_id as blocking_trx_id, 
				t_blk.trx_mysql_thread_id as blocking_thread_id, 
				t_blk.trx_started as blocking_trx_started,
				t_blk.trx_serial_num as blocking_trx_serial_num,
				t_blk.trx_gtm_gtid as blocking_trx_gtm_gtid,
				w.blocking_engine_lock_id as blocking_lock_id, 
				l_blk.lock_mode as blocking_lock_mode, 
				l_blk.lock_type as blocking_lock_type, 
				l_blk.object_schema as blocking_lock_schema, 
				l_blk.object_name as blocking_lock_table, 
				l_blk.index_name as blocking_lock_index, 
				l_blk.lock_data as blocking_lock_data  
			from performance_schema.data_lock_waits as w
			inner join performance_schema.data_locks as l_req
				on w.requesting_engine_transaction_id = l_req.engine_transaction_id
				   and w.requesting_engine_lock_id = l_req.engine_lock_id 
			inner join performance_schema.data_locks as l_blk 
				on w.blocking_engine_transaction_id = l_blk.engine_transaction_id
				   and w.blocking_engine_lock_id = l_blk.engine_lock_id
			inner join information_schema.innodb_trx as t_req 
				on w.requesting_engine_transaction_id = t_req.trx_id
			inner join information_schema.innodb_trx as t_blk 
				on w.blocking_engine_transaction_id = t_blk.trx_id 
			where t_req.trx_mysql_thread_id = ${connection_id} 
				and t_req.trx_state = 'LOCK WAIT' \G;" ${tmpfile1_path}
		print_sql_res "no innodb lock waits that connection_id = ${connection_id}!" \
						${tmpfile1_path}
		echo " "
		echo -e "++++++++++++++++++++++++++++++ \c"
		echo -e "  MDL  LOCK WAITS \c"
		echo    "++++++++++++++++++++++++++++++"
		mysql_exec "
			select 
				OBJECT_TYPE, 
				OBJECT_SCHEMA, 
				OBJECT_NAME, 
				OBJECT_INSTANCE_BEGIN, 
				LOCK_TYPE, 
				LOCK_DURATION, 
				LOCK_STATUS, 
				SOURCE, 
				PROCESSLIST_ID as connection_id, 
				OWNER_THREAD_ID, 
				OWNER_EVENT_ID 
			from performance_schema.metadata_locks 
			inner join performance_schema.threads
			on OWNER_THREAD_ID = THREAD_ID
			where LOCK_STATUS='PENDING' 
				and PROCESSLIST_ID = ${connection_id} \G;" ${tmpfile2_path}
		print_sql_res "no mdl waits that connection_id = ${connection_id}!" \
						${tmpfile2_path}
	fi
	
	delete_file ${tmpfile1_path}
	delete_file ${tmpfile2_path}
}


gdbpd_latest_deadlock()
{
	check_mysqld_running
	check_mysql_passwd ${password}

	parpare_innodb_locks

	# to show 'LATEST DETECTED DEADLOCK'
	deadlock_flag="false"
	count_separator=0
	for separator_line in ${separator_lines[@]}
	do
		if [ ${separator_line}x == x ]
		then
			continue
		fi
		odd=`expr ${count_separator} % 2`
		if [ ${odd} -eq 1 ]
		then
			content_line=`expr ${separator_line} - 1`
			content=`cat ${tmpfile1_path} | tail -n +${content_line} | head -n 1` 
			if [ "${content}"x == "LATEST DETECTED DEADLOCK"x ]
			then
				# next_start_cnt in the range with no doubt 
				next_start_cnt=`expr ${count_separator} + 1`
				((start=${separator_line} + 1))
				((end=${separator_lines[${next_start_cnt}]} - ${start}))
				log_debug "start ${start}, end ${end}"
				echo -e "++++++++++++++++++++++++++ \c"
				echo -e "LATEST DETECTED DEADLOCK  \c"
				echo    "++++++++++++++++++++++++++"
				cat ${tmpfile1_path} | tail -n +${start} | head -n ${end}
				deadlock_flag="true"
			fi
		fi
		((count_separator++))
	done
	
	if [ ${deadlock_flag}x == "false"x ]
	then
		log_info "no deadlock occured!"
	fi

	end_innodb_locks
}


gdbpd_processlist() 
{
	check_mysqld_running
	check_mysql_passwd ${password}

	get_new_file "tmp_gdbpd_processlist_"
	tmpfile1_path=${new_file}

	if [ "${connection_id}"x == x ] 
	then
		mysql_exec "
			select ID as connection_id, HOST, DB, COMMAND, TIME, STATE, INFO 
				from information_schema.processlist \G;" ${tmpfile1_path}
		print_sql_res "no process!" ${tmpfile1_path}
	else
		mysql_exec "
			select ID as connection_id, HOST, DB, COMMAND, TIME, STATE, INFO 
				from information_schema.processlist
				where ID = ${connection_id} \G;" ${tmpfile1_path}
		print_sql_res "no process that connection_id = ${connection_id}!" ${tmpfile1_path}
	fi
	
	delete_file ${tmpfile1_path}
}


gdbpd_table_statinfo()
{
	check_mysqld_running
	check_mysql_passwd ${password}

	get_new_file "tmp_gdbpd_processlist_"
	tmpfile1_path=${new_file}

	if [[ "${database_name}"x == x ]] && [[ "${table_name}"x == x ]] 
	then
		mysql_exec "select * from mysql.innodb_table_stats \G;" ${tmpfile1_path} 
		print_sql_res "no table statinfo!" ${tmpfile1_path}
	elif [[ "${database_name}"x != x ]] && [[ "${table_name}"x == x ]]
	then
		mysql_exec "
			select * from mysql.innodb_table_stats 
			where database_name='${database_name}' \G;" ${tmpfile1_path} 
		print_sql_res "no table statinfo in database: ${database_name}!" \
						 ${tmpfile1_path}
	elif [[ "${database_name}"x != x ]] && [[ "${table_name}"x != x ]]
	then
		mysql_exec "
			select * from mysql.innodb_table_stats 
			where database_name='${database_name}' 
				and table_name='${table_name}' \G;" ${tmpfile1_path} 
		print_sql_res "no table statinfo in database.table: ${database_name}.${table_name}" \
						${tmpfile1_path}
	else
		log_error "can't only tell table name without database name!"
		delete_file ${tmpfile1_path}
		exit 1
	fi

	delete_file ${tmpfile1_path}
}


gdbpd_recovery_progress()
{
	check_error_log_file
	check_mysqld_starting

	print_point_circle=5

	echo "======================= note ======================="
	echo "please make sure start mysql prior."

	stage_1=`grep -rn " starting as process " \
			${error_log_path} | tail -n 1 | cut -d ":" -f 1`
	log_debug "stage_1: ${stage_1}"
	if [ ${stage_1}x == x ]
	then
		echo ""
		echo "=================== no recovery ===================="
		log_info " no recovery process occur!"
		exit 0
	else
		echo ""
		echo " if this program abort, please exec 'ps -ef | grep \"tail -f\" | grep -v grep',"
		echo "   get the unknow 'tail process id', then kill it." 
		echo ""
		echo ""
		echo "===================================================="
		echo "================ recovery progress ================="
		echo "===================================================="
		echo ""
		stage_2=`grep -rn ": ready for connections." \
		         ${error_log_path} | tail -n 1 | cut -d ":" -f 1`
		log_debug "stage_2: ${stage_2}"

		if [[ ${stage_2}x != x ]] && \
		   [[ ${stage_1} -le ${stage_2} ]]
		then
			head -n ${stage_2} ${error_log_path} | tail -n +${stage_1}
			exit 0
		fi
		
		stage_1_tmp=${stage_1}
		while [[ ${stage_2}x == x ]] || \
		      [[ ${stage_1} -gt ${stage_2} ]]
		do
			stage_2_tmp=`wc -l ${error_log_path} | cut -d " " -f 1` 
			if [ ${stage_2_tmp}x == ${stage_2_tmp_old}x ]
			then
				stage_2=`grep -rn ": ready for connections." \
				         ${error_log_path} | tail -n 1 | cut -d ":" -f 1`
				continue
			fi
			head -n ${stage_2_tmp} ${error_log_path} | tail -n +${stage_1_tmp}
			stage_2_tmp_old=${stage_2_tmp}
			stage_1_tmp=`expr ${stage_2_tmp} + 1`
			stage_2=`grep -rn ": ready for connections." \
			         ${error_log_path} | tail -n 1 | cut -d ":" -f 1`
		done
		head -n ${stage_2} ${error_log_path} | tail -n +${stage_1_tmp}
	fi
}

# NOTE:
#  slow:
	#start_timer
	#stage_N=`cat -n ${path} | tail -n +${stage_before_N} \
	#		 | grep "${flag_N}" | tail -n 1 | cut -f 1` 
	#end_timer
#  fast:
	#start_timer
	#stage_N=`grep -rn "${flag_N}" ${path} | tail -n 1 | cut -d ":" -f 1` 
	#if [[ ${stage_N}x != x ]] && \
	#   [[ ${stage_N} -lt ${stage_before_N} ]]
	#then
	#	stage_N=""
	#fi
	#end_timer
get_stage_N()
{
	flag_N=${1}
	stage_name=${2}
	path=${3}
	stage_before_N=${4}
	stage_N=`grep -rn "${flag_N}" ${path} | tail -n 1 | cut -d ":" -f 1` 
	if [[ ${stage_N}x != x ]] && \
	   [[ ${stage_N} -lt ${stage_before_N} ]]
	then
		stage_N=""
	fi
	log_debug "${stage_name}: ${stage_N}"
}


gdbpd_help()
{
	echo "usage: "
	echo "	./gdbpd cmd_type <[-u username] [-p password] [-c connection_id] ..>"
	echo "detail: "
	echo "	./gdbpd"
	echo "	./gdbpd -h[elp] : show the manual of tool 'gdbpd'"
	echo "	./gdbpd -locks -u[sername] username <[-p[assword] password] [-c[onnection_id] connection_id]> : show all locks, connection_id is optional input param"
	echo "	./gdbpd -lock-waits -u[sername] username <[-p[assword] password] [-c[onnection_id] connection_id]> : show all lock waits, connection_id is optional input param"
	echo "	./gdbpd -latest-deadlock -u[sername] username <[-p[assword] password]> : show the latest deadlock"
	echo "	./gdbpd -processlist -u[sername] username <[-p[assword] password] [-c[onnection_id] connection_id]> : show all process, connection_id is optional input param"
	echo "	./gdbpd -recovery-progress : show recovery progress while DB starting"
	echo "	./gdbpd -ruid -u[sername] username <[-p[assword] password] [-d[atabase_name] database_name] [-t[able_name] table_name]> : show RUID, database_name or table_name is optional input param"
	echo "	./gdbpd -slowlog -u[sername] username <[-p[assword] password] [-c[onnection_id] connection_id] [-s[tart_]t[ime] start_time] [-e[nd_]t[ime] end_time]> : show all slow log, connection_id, start_time or end_time is optional input param"
	echo "	./gdbpd -stack : show all threads' stacks"
	echo "	./gdbpd -table-statinfo -u[sername] username <[-p[assword] password] [-d[atabase_name] database_name] [-t[able_name] table_name]> : show all table statistics info, database_name or table_name is optional input param"
	echo "	./gdbpd -trx -u[sername] username <[-p[assword] password] [-c[onnection_id] connection_id]> : show all transaction, connection_id is optional input param"
	echo "param: "
	echo "	'-h[elp]'          : '-h'  is the same as '-help'"
	echo "	'-u[sername]'      : '-u'  is the same as '-username'"
	echo "	'--p[assword]'     : '-p'  is the same as '-password'"
	echo "	'-c[onnection_id]' : '-c'  is the same as '-connection_id'"
	echo "	'-d[atabase_name]' : '-d'  is the same as '-database_name'"
	echo "	'-t[able_name]'    : '-t'  is the same as '-table_name'"
	echo "	'-s[tart_]t[ime]'  : '-st' is the same as '-start_time'"
	echo "	'-e[nd_]t[ime]'    : '-et' is the same as '-end_time'"
	echo "note: "
	echo "	username:      mysql user name;"
	echo "	password:      mysql password of the 'username';"
	echo "	connection_id: thread_id, the id that connection to other modules;"
	echo "	database_name: name of database;"
	echo "	table_name:    name of table;"
	echo "	start_time:    limit start time to search slow log, many kinds of format are OK, eg: 2018-11-27T08:35:27;"
	echo "	end_time:      limit end time to search slow log, many kinds of format are OK, eg:2019-03-08T16:53:01;"
}


print_sql_res()
{
	null_statement=${1}
	file_path=${2}
	res=`cat ${file_path} | grep -v \
		"mysql: \[Warning\] Using a password on the command line interface can be insecure."`
	if [ "${res}"x == x ]
	then
		echo "${null_statement}"
	else
		echo "${res}"
	fi
}


mysql_exec()
{
	sql_text="${1}"
	file_path="${2}"
	if [ "${password}"x == x ]
	then
		mysql --login-path=${username} -e "${sql_text}" &>${file_path}
	else
		mysql -u${username} -p${password} -e "${sql_text}" &>${file_path}
	fi
}


check_mysql_passwd()
{
	local password=${1}
	mysql_exec "\q;" "/dev/null"
	if [ ${?} -ne 0 ]
	then
		log_error "mysql password is wrong!"
		exit 1
	fi
}


# NOTE: no need goto error if slow_log OFF 
check_slow_query_log()
{
	get_new_file "tmp_check_slow_query_log_file_"
	tmpfile1_path=${new_file}

	mysql_exec "show variables like 'slow_query_log' \G;" ${tmpfile1_path}
	origin_slow_log_flag=`cat ${tmpfile1_path} | grep -v \
		"mysql: \[Warning\] Using a password on the command line interface can be insecure."`
	slow_log_flag=${origin_slow_log_flag##*: }
	if [ ${slow_log_flag}x == "OFF"x ]
	then
		log_info "slow_query_log is OFF."
	fi

	delete_file ${tmpfile1_path}
}

check_slow_query_log_file()
{
	get_new_file "tmp_check_slow_query_log_file_"
	tmpfile1_path=${new_file}

	mysql_exec "show variables like 'slow_query_log_file' \G;" ${tmpfile1_path}
	origin_slow_log_path=`cat ${tmpfile1_path} | grep -v \
		"mysql: \[Warning\] Using a password on the command line interface can be insecure."`
	slow_log_path=${origin_slow_log_path##*: }
	if [ ! -f ${slow_log_path} ]
	then
		log_error "${slow_log_path} miss, or user permission problem!"
		delete_file ${tmpfile1_path}
		exit 1
	fi

	delete_file ${tmpfile1_path}
}

check_performance_schema()
{
	get_new_file "tmp_check_performance_schema_"
	tmpfile1_path=${new_file}

	mysql_exec "show variables like 'performance_schema' \G;" ${tmpfile1_path} 
	org_performance_schema=`cat ${tmpfile1_path} | grep -v \
		"mysql: \[Warning\] Using a password on the command line interface can be insecure."`
	performance_schema=${org_performance_schema##*: }
	if [ ${performance_schema}x == "OFF"x ]
	then
		log_error "performance_schema must turn ON prior!"
		log_error "method: to modify (or add) 'performance_schema = ON' in etc/my.cnf, then restart DB."
		delete_file ${tmpfile1_path}
		exit 1
	fi

	delete_file ${tmpfile1_path}
}

check_innodb_status_output_locks()
{
	get_new_file "tmp_check_innodb_status_output_locks_"
	tmpfile1_path=${new_file}

	mysql_exec "show variables like 'innodb_status_output_locks' \G;" ${tmpfile1_path}
	org_innodb_status_output_locks=`cat ${tmpfile1_path} | grep -v \
		"mysql: \[Warning\] Using a password on the command line interface can be insecure."`
	innodb_status_output_locks=${org_innodb_status_output_locks##*: }
	if [ ${innodb_status_output_locks}x == "OFF"x ]
	then
		log_error "innodb_status_output_locks must turn ON prior!"
		log_error "method: exec sql 'set global innodb_status_output_locks=ON;'"
		delete_file ${tmpfile1_path}
		exit 1
	fi

	delete_file ${tmpfile1_path}
}

check_setup_instruments_enabled()
{
	get_new_file "tmp_check_setup_instruments_enabled_"
	tmpfile1_path=${new_file}

	mysql_exec "
		select ENABLED from performance_schema.setup_instruments 
		WHERE NAME ='wait/lock/metadata/sql/mdl' \G;" ${tmpfile1_path}
	org_setup_instruments_enabled=`cat ${tmpfile1_path} | grep -v \
		"mysql: \[Warning\] Using a password on the command line interface can be insecure."`
	setup_instruments_enabled=${org_setup_instruments_enabled##*: }
	if [ ${setup_instruments_enabled}x == "NO"x ]
	then
		log_error "performance_schema.setup_instruments ENABLED where NAME ='wait/lock/metadata/sql/mdl' must update to YES prior!"
		log_error "method: exec sql ' UPDATE performance_schema.setup_instruments SET ENABLED = 'YES' WHERE NAME ='wait/lock/metadata/sql/mdl'; '"
		delete_file ${tmpfile1_path}
		exit 1
	fi

	delete_file ${tmpfile1_path}
}

check_error_log_file()
{
	if [ ${HOME}x == x ]
	then
		log_error "There is no ${HOME} dir!"
		exit 1
	else
		error_log_line=`cat ${HOME}/etc/my.cnf | grep "^log-error"`
		if [ ${error_log_line}x == x ]
		then
			log_error "no error log file set, please set 'log-error' and touch the file."
			exit 1
		fi
		error_log_path=${error_log_line##*=}
		if [ ! -f ${error_log_path} ]
		then
			log_error "${error_log_path} miss, or user permission problem!"
			exit 1
		fi
		log_debug ${error_log_path}
	fi
}

check_mysqld_starting()
{
	check_mysqld_running "no mysqld process, mysql is not starting!"
}

check_mysqld_running()
{
	error_info="${1}"
	ps_info=(`ps -ef | grep ${USER} | grep mysqld | grep -v grep`)
	if [ "${ps_info}"x == x ]
	then
		if [ "${error_info}"x == x ]
		then
			log_error "no mysqld process, please check mysql started or not!"
		else
			log_error "${error_info}"
		fi
		exit 1
	fi
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


start_timer()
{
	starttime=`date +'%Y-%m-%d %H:%M:%S'`
}

end_timer()
{
	endtime=`date +'%Y-%m-%d %H:%M:%S'`
	start_seconds=$(date --date="$starttime" +%s);
	end_seconds=$(date --date="$endtime" +%s);
	echo "running time: "$((end_seconds-start_seconds))"s"
}


main ${*}

