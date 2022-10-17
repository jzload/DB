-- -------------------------------------------------------------
-- set mysql.slave_master_info ENCRYPTION to encryption
-- disable it beacause of xtrabackup dosen't support ENCRYPTION
-- -------------------------------------------------------------
/*
SET session sql_log_bin=0;
ALTER TABLE mysql.slave_master_info ENCRYPTION  = 'Y';
SET session sql_log_bin=1;
*/
