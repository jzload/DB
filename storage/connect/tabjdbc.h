/*************** Tabjdbc H Declares Source Code File (.H) **************/
/*  Name: TABJDBC.H   Version 1.0                                      */
/*                                                                     */
/*  (C) Copyright to the author Olivier BERTRAND          2016         */
/*  Copyright (c) 2021, JINZHUAN Information Technology Co., Ltd.      */
/*                                                                     */
/*  This file contains the TDBJDBC classes declares.                   */
/***********************************************************************/
#include "colblk.h"
#include "resource.h"
#include "jdbccat.h"

typedef class JDBCDEF *PJDBCDEF;
typedef class TDBJDBC *PTDBJDBC;
typedef class JDBCCOL *PJDBCCOL;
typedef class TDBXJDC *PTDBXJDC;
typedef class JSRCCOL *PJSRCCOL;
//typedef class TDBOIF  *PTDBOIF;
//typedef class OIFCOL  *POIFCOL;
//typedef class TDBJSRC *PTDBJSRC;

/***********************************************************************/
/*  JDBC table.                                                        */
/***********************************************************************/
class DllExport JDBCDEF : public EXTDEF { /* Logical table description */
	friend class TDBJDBC;
	friend class TDBXJDC;
	friend class TDBJDRV;
	friend class TDBJTB;
	friend class TDBJDBCL;
public:
	// Constructor
	JDBCDEF(void);

	// Implementation
	virtual const char *GetType(void) { return "JDBC"; }

	// Methods
	virtual bool DefineAM(PGLOBAL g, LPCSTR am, int poff);
	virtual PTDB GetTable(PGLOBAL g, MODE m);
	int  ParseURL(PGLOBAL g, char *url, bool b = true);
	bool SetParms(PJPARM sjp);

protected:
	// Members
	PSZ     Driver;             /* JDBC driver                           */
	PSZ     Url;                /* JDBC driver URL                       */
	PSZ     Wrapname;           /* Java wrapper name                     */
}; // end of JDBCDEF

#if !defined(NJDBC)
#include "jdbconn.h"

/***********************************************************************/
/*  This is the JDBC Access Method class declaration for files from    */
/*  other DB drivers to be accessed via JDBC.                          */
/***********************************************************************/
class TDBJDBC : public TDBEXT {
	friend class JDBCCOL;
	friend class JDBConn;
public:
	// Constructor
	TDBJDBC(PJDBCDEF tdp = NULL);
  TDBJDBC(PTDBJDBC tdbp);

	// Implementation
	virtual AMT  GetAmType(void) {return TYPE_AM_JDBC;}
  virtual PTDB Duplicate(PGLOBAL g) {return (PTDB)new(g) TDBJDBC(this);}

	// Methods
  virtual PTDB Clone(PTABS t);
//virtual int  GetRecpos(void);
	virtual bool SetRecpos(PGLOBAL g, int recpos);
//virtual PSZ  GetFile(PGLOBAL g);
//virtual void SetFile(PGLOBAL g, PSZ fn);
	virtual void ResetSize(void);
//virtual int  GetAffectedRows(void) {return AftRows;}
	virtual PSZ  GetServer(void) { return "JDBC"; }
	virtual int  Indexable(void) { return 2; }

	// Database routines
	virtual PCOL MakeCol(PGLOBAL g, PCOLDEF cdp, PCOL cprec, int n);
	virtual int  Cardinality(PGLOBAL g);
//virtual int  GetMaxSize(PGLOBAL g);
//virtual int  GetProgMax(PGLOBAL g);
	virtual bool OpenDB(PGLOBAL g);
	virtual int  ReadDB(PGLOBAL g);
	virtual int  WriteDB(PGLOBAL g);
	virtual int  DeleteDB(PGLOBAL g, int irc);
	virtual void CloseDB(PGLOBAL g);
	virtual bool ReadKey(PGLOBAL g, OPVAL op, const key_range *kr);

protected:
	// Internal functions
//int   Decode(char *utf, char *buf, size_t n);
//bool  MakeSQL(PGLOBAL g, bool cnt);
	bool  MakeInsert(PGLOBAL g);
//virtual bool  MakeCommand(PGLOBAL g);
//bool  MakeFilter(PGLOBAL g, bool c);
	bool  SetParameters(PGLOBAL g);
//char *MakeUpdate(PGLOBAL g);
//char *MakeDelete(PGLOBAL g);

	// Members
	JDBConn *Jcp;               // Points to a JDBC connection class
	JDBCCOL *Cnp;               // Points to count(*) column
	JDBCPARM Ops;               // Additional parameters
	char    *WrapName;          // Points to Java wrapper name
//int      Ncol;							// The column number
	bool     Prepared;          // True when using prepared statement
	bool     Werr;							// Write error
	bool     Rerr;							// Rewind error
}; // end of class TDBJDBC

/***********************************************************************/
/*  Class JDBCCOL: JDBC access method column descriptor.               */
/*  This A.M. is used for JDBC tables.                                 */
/***********************************************************************/
class JDBCCOL : public EXTCOL {
	friend class TDBJDBC;
public:
	// Constructors
	JDBCCOL(PCOLDEF cdp, PTDB tdbp, PCOL cprec, int i, PSZ am = "JDBC");
  JDBCCOL(JDBCCOL *colp, PTDB tdbp); // Constructor used in copy process

	// Implementation
	virtual int  GetAmType(void) { return TYPE_AM_JDBC; }

	// Methods
//virtual bool SetBuffer(PGLOBAL g, PVAL value, bool ok, bool check);
	virtual void ReadColumn(PGLOBAL g);
	virtual void WriteColumn(PGLOBAL g);

protected:
	// Constructor for count(*) column
  JDBCCOL(void);

	// Members
}; // end of class JDBCCOL

/***********************************************************************/
/*  This is the JDBC Access Method class declaration that send         */
/*  commands to be executed by other DB JDBC drivers.                  */
/***********************************************************************/
class TDBXJDC : public TDBJDBC {
	friend class JSRCCOL;
	friend class JDBConn;
public:
	// Constructors
	TDBXJDC(PJDBCDEF tdp = NULL);

	// Implementation
	virtual AMT  GetAmType(void) {return TYPE_AM_XDBC;}

	// Methods
	//virtual int  GetRecpos(void);
	//virtual PSZ  GetFile(PGLOBAL g);
	//virtual void SetFile(PGLOBAL g, PSZ fn);
	//virtual void ResetSize(void);
	//virtual int  GetAffectedRows(void) {return AftRows;}
	//virtual PSZ  GetServer(void) {return "JDBC";}

	// Database routines
	virtual PCOL MakeCol(PGLOBAL g, PCOLDEF cdp, PCOL cprec, int n);
	//virtual int  GetProgMax(PGLOBAL g);
	virtual int  GetMaxSize(PGLOBAL g);
	virtual bool OpenDB(PGLOBAL g);
	virtual int  ReadDB(PGLOBAL g);
	virtual int  WriteDB(PGLOBAL g);
	virtual int  DeleteDB(PGLOBAL g, int irc);
	//virtual void CloseDB(PGLOBAL g);

protected:
	// Internal functions
	PCMD  MakeCMD(PGLOBAL g);
	//bool  BindParameters(PGLOBAL g);

	// Members
	PCMD     Cmdlist;           // The commands to execute
	char    *Cmdcol;            // The name of the Xsrc command column
	int      Mxr;               // Maximum errors before closing
	int      Nerr;              // Number of errors so far
}; // end of class TDBXJDC

/***********************************************************************/
/*  Used by table in source execute mode.                              */
/***********************************************************************/
class JSRCCOL : public JDBCCOL {
	friend class TDBXJDC;
public:
	// Constructors
	JSRCCOL(PCOLDEF cdp, PTDB tdbp, PCOL cprec, int i, PSZ am = "JDBC");

	// Implementation
	virtual int  GetAmType(void) {return TYPE_AM_JDBC;}

	// Methods
	virtual void ReadColumn(PGLOBAL g);
	virtual void WriteColumn(PGLOBAL g);
	//        void Print(PGLOBAL g, FILE *, uint);

protected:
	// Members
	char    *Buffer;              // To get returned message
	int      Flag;                // Column content desc
}; // end of class JSRCCOL

/***********************************************************************/
/*  This is the class declaration for the Drivers catalog table.       */
/***********************************************************************/
class TDBJDRV : public TDBCAT {
public:
	// Constructor
	TDBJDRV(PJDBCDEF tdp) : TDBCAT(tdp) {Maxres = tdp->Maxres;}

protected:
	// Specific routines
	virtual PQRYRES GetResult(PGLOBAL g);

	// Members
	int      Maxres;            // Returned lines limit
}; // end of class TDBJDRV

/***********************************************************************/
/*  This is the class declaration for the tables catalog table.        */
/***********************************************************************/
class TDBJTB : public TDBJDRV {
public:
	// Constructor
	TDBJTB(PJDBCDEF tdp);

protected:
	// Specific routines
	virtual PQRYRES GetResult(PGLOBAL g);

	// Members
	char    *Schema;            // Points to schema name or NULL
	char    *Tab;               // Points to JDBC table name or pattern
	char    *Tabtype;           // Points to JDBC table type
	JDBCPARM Ops;               // Additional parameters
}; // end of class TDBJTB

/***********************************************************************/
/*  This is the class declaration for the columns catalog table.       */
/***********************************************************************/
class TDBJDBCL : public TDBJTB {
public:
	// Constructor
	TDBJDBCL(PJDBCDEF tdp);

protected:
	// Specific routines
	virtual PQRYRES GetResult(PGLOBAL g);

	// Members
	char    *Colpat;            // Points to catalog column pattern
}; // end of class TDBJDBCL

#if 0
/***********************************************************************/
/*  This is the class declaration for the Data Sources catalog table.  */
/***********************************************************************/
class TDBJSRC : public TDBJDRV {
public:
	// Constructor
	TDBJSRC(PJDBCDEF tdp) : TDBJDRV(tdp) {}

protected:
	// Specific routines
	virtual PQRYRES GetResult(PGLOBAL g);

	// No additional Members
}; // end of class TDBJSRC
#endif // 0

#endif // !NJDBC
