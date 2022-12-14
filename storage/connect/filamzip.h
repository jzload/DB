/************** filamzip H Declares Source Code File (.H) **************/
/*  Name: filamzip.h   Version 1.1                                     */
/*                                                                     */
/*  (C) Copyright to the author Olivier BERTRAND          2016-2017    */
/*  Copyright (c) 2021, JINZHUAN Information Technology Co., Ltd.      */
/*                                                                     */
/*  This file contains the ZIP file access method classes declares.    */
/***********************************************************************/
#ifndef __FILAMZIP_H
#define __FILAMZIP_H

#include "block.h"
#include "filamap.h"
#include "filamfix.h"
#include "zip.h"
#include "unzip.h"

#define DLLEXPORT extern "C"

typedef class UNZFAM *PUNZFAM;
typedef class UZXFAM *PUZXFAM;
typedef class ZIPFAM *PZIPFAM;
typedef class ZPXFAM *PZPXFAM;

/***********************************************************************/
/*  This is the ZIP utility fonctions class.                           */
/***********************************************************************/
class DllExport ZIPUTIL : public BLOCK {
 public:
	// Constructor
	ZIPUTIL(PSZ tgt);
	//ZIPUTIL(ZIPUTIL *zutp);

	// Implementation
	//PTXF Duplicate(PGLOBAL g) { return (PTXF) new(g)UNZFAM(this); }

	// Methods
	bool OpenTable(PGLOBAL g, MODE mode, char *fn, bool append);
	bool open(PGLOBAL g, char *fn, bool append);
	bool addEntry(PGLOBAL g, char *entry);
	void close(void);
	void closeEntry(void);
  int  writeEntry(PGLOBAL g, char *buf, int len);
	void getTime(tm_zip& tmZip);

	// Members
	zipFile         zipfile;							// The ZIP container file
	PSZ             target;								// The target file name
//unz_file_info   finfo;								// The current file info
	PFBLOCK         fp;
//char           *memory;
//uint            size;
//int             multiple;             // Multiple targets
	bool            entryopen;						// True when open current entry
//char            fn[FILENAME_MAX];     // The current entry file name
//char            mapCaseTable[256];
}; // end of ZIPUTIL

/***********************************************************************/
/*  This is the unZIP utility fonctions class.                         */
/***********************************************************************/
class DllExport UNZIPUTL : public BLOCK {
 public:
	// Constructor
	UNZIPUTL(PSZ tgt, bool mul);
//UNZIPUTL(UNZIPUTL *zutp);

	// Implementation
//PTXF Duplicate(PGLOBAL g) { return (PTXF) new(g)UNZFAM(this); }

	// Methods
	bool OpenTable(PGLOBAL g, MODE mode, char *fn);
	bool open(PGLOBAL g, char *fn);
	bool openEntry(PGLOBAL g);
	void close(void);
	void closeEntry(void);
	bool WildMatch(PSZ pat, PSZ str);
	int  findEntry(PGLOBAL g, bool next);
	int  nextEntry(PGLOBAL g);

	// Members
	unzFile         zipfile;							// The ZIP container file
	PSZ             target;								// The target file name
	unz_file_info   finfo;								// The current file info
	PFBLOCK         fp;
	char           *memory;
	uint            size;
	int             multiple;             // Multiple targets
	bool            entryopen;						// True when open current entry
	char            fn[FILENAME_MAX];     // The current entry file name
	char            mapCaseTable[256];
}; // end of UNZIPUTL

/***********************************************************************/
/*  This is the unzip file access method.                              */
/***********************************************************************/
class DllExport UNZFAM : public MAPFAM {
//friend class UZXFAM;
 public:
	// Constructors
	UNZFAM(PDOSDEF tdp);
	UNZFAM(PUNZFAM txfp);

	// Implementation
	virtual AMT  GetAmType(void) {return TYPE_AM_ZIP;}
	virtual PTXF Duplicate(PGLOBAL g) {return (PTXF) new(g) UNZFAM(this);}

	// Methods
	virtual int  Cardinality(PGLOBAL g);
	virtual int  GetFileLength(PGLOBAL g);
	//virtual int  MaxBlkSize(PGLOBAL g, int s) {return s;}
	virtual bool OpenTableFile(PGLOBAL g);
	virtual bool DeferReading(void) { return false; }
	virtual int  GetNext(PGLOBAL g);
	//virtual int  ReadBuffer(PGLOBAL g);
	//virtual int  WriteBuffer(PGLOBAL g);
	//virtual int  DeleteRecords(PGLOBAL g, int irc);
	//virtual void CloseTableFile(PGLOBAL g, bool abort);

 protected:
	// Members
	UNZIPUTL *zutp;
	PSZ       target;
	bool      mul;
}; // end of UNZFAM

/***********************************************************************/
/*  This is the fixed unzip file access method.                        */
/***********************************************************************/
class DllExport UZXFAM : public MPXFAM {
//friend class UNZFAM;
 public:
	// Constructors
	UZXFAM(PDOSDEF tdp);
	UZXFAM(PUZXFAM txfp);

	// Implementation
	virtual AMT  GetAmType(void) { return TYPE_AM_ZIP; }
	virtual PTXF Duplicate(PGLOBAL g) { return (PTXF) new(g)UZXFAM(this); }

	// Methods
	virtual int  GetFileLength(PGLOBAL g);
	virtual int  Cardinality(PGLOBAL g);
	virtual bool OpenTableFile(PGLOBAL g);
	virtual int  GetNext(PGLOBAL g);
	//virtual int  ReadBuffer(PGLOBAL g);

 protected:
	// Members
	UNZIPUTL *zutp;
	PSZ       target;
	bool      mul;
}; // end of UZXFAM

/***********************************************************************/
/*  This is the zip file access method.                                */
/***********************************************************************/
class DllExport ZIPFAM : public DOSFAM {
 public:
	// Constructors
	ZIPFAM(PDOSDEF tdp);

	// Implementation
	virtual AMT  GetAmType(void) {return TYPE_AM_ZIP;}

	// Methods
	virtual int  Cardinality(PGLOBAL g) {return 0;}
	virtual int  GetFileLength(PGLOBAL g) {return g ? 0 : 1;}
	//virtual int  MaxBlkSize(PGLOBAL g, int s) {return s;}
	virtual bool OpenTableFile(PGLOBAL g);
	virtual int  ReadBuffer(PGLOBAL g);
	virtual int  WriteBuffer(PGLOBAL g);
	//virtual int  DeleteRecords(PGLOBAL g, int irc);
	virtual void CloseTableFile(PGLOBAL g, bool abort);

 protected:
	// Members
	ZIPUTIL *zutp;
	PSZ      target;
	bool     append;
}; // end of ZIPFAM

/***********************************************************************/
/*  This is the fixed zip file access method.                          */
/***********************************************************************/
class DllExport ZPXFAM : public FIXFAM {
 public:
	// Constructors
	ZPXFAM(PDOSDEF tdp);

	// Implementation
	virtual AMT  GetAmType(void) {return TYPE_AM_ZIP;}

	// Methods
	virtual int  Cardinality(PGLOBAL g) {return 0;}
	virtual int  GetFileLength(PGLOBAL g) {return g ? 0 : 1;}
	virtual bool OpenTableFile(PGLOBAL g);
	virtual int  WriteBuffer(PGLOBAL g);
	virtual void CloseTableFile(PGLOBAL g, bool abort);

 protected:
	// Members
	ZIPUTIL *zutp;
	PSZ      target;
	bool     append;
}; // end of ZPXFAM

#endif // __FILAMZIP_H
