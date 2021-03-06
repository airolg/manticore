//
// $Id$
//

//
// Copyright (c) 2001-2016, Andrew Aksyonoff
// Copyright (c) 2008-2016, Sphinx Technologies Inc
// All rights reserved
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License. You should have
// received a copy of the GPL license along with this program; if you
// did not, you can find it at http://www.gnu.org/
//

/// @file searchdaemon.h
/// Declarations for the stuff specifically needed by searchd to serve the indexes.


#ifndef _searchdaemon_
#define _searchdaemon_

/////////////////////////////////////////////////////////////////////////////
// MACHINE-DEPENDENT STUFF
/////////////////////////////////////////////////////////////////////////////

#if USE_WINDOWS
	// Win-specific headers and calls
	#include <winsock2.h>

	#define HAVE_POLL 1
	static inline int poll ( struct pollfd *pfd, int nfds, int timeout ) { return WSAPoll ( pfd, nfds, timeout ); }
	#pragma comment(linker, "/defaultlib:ws2_32.lib")
	#pragma message("Automatically linking with ws2_32.lib")

#else
	// UNIX-specific headers and calls
	#include <sys/socket.h>
	#include <sys/un.h>
	#include <arpa/inet.h>

	#if HAVE_POLL
	#include <poll.h>
	#endif

	#if HAVE_EPOLL
	#include <sys/epoll.h>
	#endif

#endif

#if USE_WINDOWS

	// Windows hacks
	#undef EINTR
	#undef EWOULDBLOCK
	#undef ETIMEDOUT
	#undef EINPROGRESS
	#undef ECONNRESET
	#undef ECONNABORTED
	#define LOCK_EX			0
	#define LOCK_UN			1
	#define STDIN_FILENO	fileno(stdin)
	#define STDOUT_FILENO	fileno(stdout)
	#define STDERR_FILENO	fileno(stderr)
	#define ETIMEDOUT		WSAETIMEDOUT
	#define EWOULDBLOCK		WSAEWOULDBLOCK
	#define EINPROGRESS		WSAEINPROGRESS
	#define EINTR			WSAEINTR
	#define ECONNRESET		WSAECONNRESET
	#define ECONNABORTED	WSAECONNABORTED
	#define ESHUTDOWN		WSAESHUTDOWN
	#define socklen_t		int

	#define ftruncate		_chsize
	#define getpid			GetCurrentProcessId

#endif // USE_WINDOWS

// socket function definitions
#if USE_WINDOWS

	#pragma comment(linker, "/defaultlib:wsock32.lib")
	#pragma message("Automatically linking with wsock32.lib")

	#define sphSockRecv(_sock,_buf,_len)	::recv(_sock,_buf,_len,0)
	#define sphSockSend(_sock,_buf,_len)	::send(_sock,_buf,_len,0)
	#define sphSockClose(_sock)				::closesocket(_sock)
#else
	// there's no MSG_NOSIGNAL on OS X
	#ifndef MSG_NOSIGNAL
	#define MSG_NOSIGNAL 0
	#endif

	#define sphSockRecv(_sock,_buf,_len)	::recv(_sock,_buf,_len,MSG_NOSIGNAL)
	#define sphSockSend(_sock,_buf,_len)	::send(_sock,_buf,_len,MSG_NOSIGNAL)
	#define sphSockClose(_sock)				::close(_sock)
#endif

const char * sphSockError ( int =0 );
int sphSockGetErrno ();
void sphSockSetErrno ( int );
int sphSockPeekErrno ();
int sphSetSockNB ( int );
void sphFDSet ( int fd, fd_set * fdset );
void sphFDClr ( int fd, fd_set * fdset );
DWORD sphGetAddress ( const char * sHost, bool bFatal=false );

/////////////////////////////////////////////////////////////////////////////
// MISC GLOBALS
/////////////////////////////////////////////////////////////////////////////

/// known commands
/// (shared here because at least SEARCHD_COMMAND_TOTAL used outside the core)
enum SearchdCommand_e : WORD
{
	SEARCHD_COMMAND_SEARCH		= 0,
	SEARCHD_COMMAND_EXCERPT		= 1,
	SEARCHD_COMMAND_UPDATE		= 2,
	SEARCHD_COMMAND_KEYWORDS	= 3,
	SEARCHD_COMMAND_PERSIST		= 4,
	SEARCHD_COMMAND_STATUS		= 5,
	SEARCHD_COMMAND_FLUSHATTRS	= 7,
	SEARCHD_COMMAND_SPHINXQL	= 8,
	SEARCHD_COMMAND_PING		= 9,
	SEARCHD_COMMAND_DELETE		= 10,
	SEARCHD_COMMAND_UVAR		= 11,
	SEARCHD_COMMAND_INSERT		= 12,
	SEARCHD_COMMAND_REPLACE		= 13,
	SEARCHD_COMMAND_COMMIT		= 14,
	SEARCHD_COMMAND_SUGGEST		= 15,

	SEARCHD_COMMAND_TOTAL,
	SEARCHD_COMMAND_WRONG = SEARCHD_COMMAND_TOTAL,
};

/// known command versions
/// (shared here because of REPLICATE)
enum SearchdCommandV_e : WORD
{
	VER_COMMAND_SEARCH		= 0x121, // 1.33
	VER_COMMAND_EXCERPT		= 0x104,
	VER_COMMAND_UPDATE		= 0x103,
	VER_COMMAND_KEYWORDS	= 0x101,
	VER_COMMAND_STATUS		= 0x101,
	VER_COMMAND_FLUSHATTRS	= 0x100,
	VER_COMMAND_SPHINXQL	= 0x100,
	VER_COMMAND_PING		= 0x100,
	VER_COMMAND_UVAR		= 0x100,

	VER_COMMAND_WRONG = 0,
};

enum ESphAddIndex
{
	ADD_ERROR	= 0,
	ADD_LOCAL	= 1,
	ADD_DISTR	= 2,
	ADD_RT		= 3,
	ADD_TMPL	= 4,
	ADD_CLUSTER	= 5,
};

/////////////////////////////////////////////////////////////////////////////
// SOME SHARED GLOBAL VARIABLES
/////////////////////////////////////////////////////////////////////////////

extern int				g_iReadTimeout;		// defined in searchd.cpp
extern int				g_iMaxPacketSize;	// in bytes; for both query packets from clients and response packets from agents

extern int				g_iDistThreads;
extern volatile bool	g_bShutdown;
extern ESphLogLevel		g_eLogLevel;

/////////////////////////////////////////////////////////////////////////////
// NETWORK BUFFERS
/////////////////////////////////////////////////////////////////////////////

template < typename T >
void sphBufferAddT ( CSphVector<BYTE> & dBuf, T tValue )
{
	int iOff = dBuf.GetLength();
	dBuf.Resize ( iOff + sizeof(T) );
	sphUnalignedWrite ( dBuf.Begin() + iOff, tValue );
}


/// dynamic response buffer
/// to remove ISphNoncopyable just add copy c-tor and operator=
/// splitting application network packets in several sphSocketSend()s causes
/// Nagle's algorithm to wait delayead ACKs (google for it if you want)
/// That's why we use relocation here instead of static-sized buffer.
/// To be twice sure we're switching off Nagle's algorithm in all sockets.
class ISphOutputBuffer : public ISphNoncopyable
{
public:
	ISphOutputBuffer ();
	explicit ISphOutputBuffer ( CSphVector<BYTE> & dBuf );
	virtual ~ISphOutputBuffer() {}

	void		SendInt ( int iValue )			{ SendT<int> ( htonl ( iValue ) ); }
	void		SendAsDword ( int64_t iValue ) ///< sends the 32bit MAX_UINT if the value is greater than it.
	{
		iValue = Min ( Max ( iValue, 0 ), UINT_MAX );
		SendDword ( DWORD(iValue) );
	}
	void		SendDword ( DWORD iValue )		{ SendT<DWORD> ( htonl ( iValue ) ); }
	void		SendWord ( WORD iValue )		{ SendT<WORD> ( htons ( iValue ) ); }
	void		SendFloat ( float fValue )		{ SendT<DWORD> ( htonl ( sphF2DW ( fValue ) ) ); }
	void		SendByte ( BYTE uValue )		{ SendT<BYTE> ( uValue ); }

	void SendLSBDword ( DWORD v )
	{
		SendByte ( (BYTE)( v & 0xff ) );
		SendByte ( (BYTE)( (v>>8) & 0xff ) );
		SendByte ( (BYTE)( (v>>16) & 0xff ) );
		SendByte ( (BYTE)( (v>>24) & 0xff) );
	}

	void SendUint64 ( uint64_t iValue )
	{
		SendT<DWORD> ( htonl ( (DWORD)(iValue>>32) ) );
		SendT<DWORD> ( htonl ( (DWORD)(iValue & 0xffffffffUL) ) );
	}

	void		SendDocid ( SphDocID_t iValue )	{ SendUint64 ( iValue ); }
	void		SendString ( const char * sStr );

	void		SendMysqlInt ( int iVal );
	void		SendMysqlString ( const char * sStr );
	void		SendBytes ( const void * pBuf, int iLen );	///< (was) protected to avoid network-vs-host order bugs
	void		SendOutput ( const ISphOutputBuffer & tOut );
	void		SwapData ( CSphVector<BYTE> & rhs ) { m_dBuf.SwapData ( rhs ); }

	virtual void	Flush () {}
	virtual bool	GetError () const { return false; }
	virtual int		GetSentCount () const { return m_dBuf.GetLength(); }
	virtual void	SetProfiler ( CSphQueryProfile * ) {}

protected:
	CSphVector<BYTE>	m_dBuf;

private:
	template < typename T > void	SendT ( T tValue )							///< (was) protected to avoid network-vs-host order bugs
	{
		int iOff = m_dBuf.GetLength();
		m_dBuf.Resize ( iOff + sizeof(T) );
		sphUnalignedWrite ( m_dBuf.Begin() + iOff, tValue );
	}
};

class NetOutputBuffer_c : public ISphOutputBuffer
{
public:
	explicit	NetOutputBuffer_c ( int iSock );

	virtual void	Flush () override;
	virtual bool	GetError () const override { return m_bError; }
	virtual int		GetSentCount () const override { return m_iSent; }
	virtual void	SetProfiler ( CSphQueryProfile * pProfiler ) override { m_pProfile = pProfiler; }
	const char*		GetErrorMsg () const;

private:
	CSphQueryProfile *	m_pProfile;
	int			m_iSock;			///< my socket
	int			m_iSent;
	bool		m_bError;
	CSphString	m_sError;

};


/// generic request buffer
class InputBuffer_c
{
public:
	InputBuffer_c ( const BYTE * pBuf, int iLen );
	virtual			~InputBuffer_c () {}

	int				GetInt () { return ntohl ( GetT<int> () ); }
	WORD			GetWord () { return ntohs ( GetT<WORD> () ); }
	DWORD			GetDword () { return ntohl ( GetT<DWORD> () ); }
	DWORD			GetLSBDword () { return GetByte() + ( GetByte()<<8 ) + ( GetByte()<<16 ) + ( GetByte()<<24 ); }
	uint64_t		GetUint64() { uint64_t uRes = GetDword(); return (uRes<<32)+GetDword(); }
	BYTE			GetByte () { return GetT<BYTE> (); }
	float			GetFloat () { return sphDW2F ( ntohl ( GetT<DWORD> () ) ); }
	CSphString		GetString ();
	CSphString		GetRawString ( int iLen );
	bool			GetString ( CSphVector<BYTE> & dBuffer );
	bool			GetError () { return m_bError; }
	bool			GetBytes ( void * pBuf, int iLen );
	const BYTE *	GetBufferPtr () const { return m_pBuf; }

	template < typename T > bool	GetDwords ( CSphVector<T> & dBuffer, int & iGot, int iMax );
	template < typename T > bool	GetQwords ( CSphVector<T> & dBuffer, int & iGot, int iMax );

protected:
	const BYTE *	m_pBuf;
	const BYTE *	m_pCur;
	bool			m_bError;
	int				m_iLen;

protected:
	void						SetError ( bool bError ) { m_bError = bError; }
	template < typename T > T	GetT ();
};

template < typename T > T InputBuffer_c::GetT ()
{
	if ( m_bError || ( m_pCur+sizeof(T) > m_pBuf+m_iLen ) )
	{
		SetError ( true );
		return 0;
	}

	T iRes = sphUnalignedRead ( *(T*)m_pCur );
	m_pCur += sizeof(T);
	return iRes;
}


/// simple memory request buffer
class MemInputBuffer_c : public InputBuffer_c
{
public:
	MemInputBuffer_c ( const BYTE * pBuf, int iLen ) : InputBuffer_c ( pBuf, iLen ) {}
};


/// simple network request buffer
class NetInputBuffer_c : public InputBuffer_c
{
public:
	explicit		NetInputBuffer_c ( int iSock );
	virtual			~NetInputBuffer_c ();

	bool			ReadFrom ( int iLen, int iTimeout, bool bIntr=false, bool bAppend=false );
	bool			ReadFrom ( int iLen ) { return ReadFrom ( iLen, g_iReadTimeout ); }

	bool			IsIntr () const { return m_bIntr; }

protected:
	static const int	NET_MINIBUFFER_SIZE = 4096;

	int					m_iSock;
	bool				m_bIntr;

	BYTE				m_dMinibufer[NET_MINIBUFFER_SIZE];
	int					m_iMaxibuffer;
	BYTE *				m_pMaxibuffer;
};

bool IsPortInRange ( int iPort );
int sphSockRead ( int iSock, void * buf, int iLen, int iReadTimeout, bool bIntr );

struct CrashQuery_t
{
	const BYTE *			m_pQuery;	// last query
	int						m_iSize;	// last query size
	WORD					m_uCMD;		// last command (header)
	WORD					m_uVer;		// last command's version (header)
	bool					m_bMySQL;	// is query from MySQL or API

	CrashQuery_t ()
		: m_pQuery ( NULL )
		, m_iSize ( 0 )
		, m_uCMD ( 0 )
		, m_uVer ( 0 )
		, m_bMySQL ( false )
	{
	}
};

// This class is basically a pointer to query string and some more additional info.
// Each thread which executes query must have exactly one instance of this class on
// its stack and m_tLastQueryTLS will contain a pointer to that instance.
// Main thread has explicitly created SphCrashLogger_c on its stack, other query
// threads should be created with SphCrashLogger_c::ThreadCreate()
class SphCrashLogger_c
{
public:
	SphCrashLogger_c () {}
	~SphCrashLogger_c ();

	static void Init ();
	static void Done ();

#if !USE_WINDOWS
	static void HandleCrash ( int );
#else
	static LONG WINAPI HandleCrash ( EXCEPTION_POINTERS * pExc );
#endif
	static void SetLastQuery ( const CrashQuery_t & tQuery );
	static void SetupTimePID ();
	static CrashQuery_t GetQuery ();
	void SetupTLS ();

	// create thread with crash logging
	static bool ThreadCreate ( SphThread_t * pThread, void ( *pCall )(void*), void * pArg, bool bDetached=false );

private:
	struct CallArgPair_t
	{
		CallArgPair_t ( void ( *pCall )(void *), void * pArg )
			: m_pCall ( pCall )
			, m_pArg ( pArg )
		{}
		void ( *m_pCall )( void * );
		void * m_pArg;
	};

	// sets up a TLS for a given thread
	static void ThreadWrapper ( void * pArg );

	CrashQuery_t			m_tQuery;		// per thread copy of last query for thread mode
	static SphThreadKey_t	m_tTLS;	// pointer to on-stack instance of this class
};

enum
{
	QUERY_STATS_INTERVAL_1MIN,
	QUERY_STATS_INTERVAL_5MIN,
	QUERY_STATS_INTERVAL_15MIN,
	QUERY_STATS_INTERVAL_ALLTIME,

	QUERY_STATS_INTERVAL_TOTAL
};


enum
{
	QUERY_STATS_TYPE_AVG,
	QUERY_STATS_TYPE_MIN,
	QUERY_STATS_TYPE_MAX,
	QUERY_STATS_TYPE_95,
	QUERY_STATS_TYPE_99,

	QUERY_STATS_TYPE_TOTAL,
};


struct QueryStatElement_t
{
	uint64_t	m_dData[QUERY_STATS_TYPE_TOTAL];
	uint64_t	m_uTotalQueries;

				QueryStatElement_t();
};


struct QueryStats_t
{
	QueryStatElement_t	m_dStats[QUERY_STATS_INTERVAL_TOTAL];
};


struct QueryStatRecord_t
{
	uint64_t	m_uQueryTimeMin;
	uint64_t	m_uQueryTimeMax;
	uint64_t	m_uQueryTimeSum;
	uint64_t	m_uFoundRowsMin;
	uint64_t	m_uFoundRowsMax;
	uint64_t	m_uFoundRowsSum;

	uint64_t	m_uTimestamp;
	int			m_iCount;
};


class QueryStatContainer_i
{
public:
	virtual void						Add ( uint64_t uFoundRows, uint64_t uQueryTime, uint64_t uTimestamp ) = 0;
	virtual void						GetRecord ( int iRecord, QueryStatRecord_t & tRecord ) const = 0;
	virtual int							GetNumRecords() const = 0;
};


class QueryStatContainer_c : public QueryStatContainer_i
{
public:
	virtual void						Add ( uint64_t uFoundRows, uint64_t uQueryTime, uint64_t uTimestamp ) override;
	virtual void						GetRecord ( int iRecord, QueryStatRecord_t & tRecord ) const override;
	virtual int							GetNumRecords() const override;

	QueryStatContainer_c();
	QueryStatContainer_c ( QueryStatContainer_c && tOther );
	QueryStatContainer_c & operator= ( QueryStatContainer_c && tOther );
	
private:
	CircularBuffer_T<QueryStatRecord_t>	m_dRecords;
};


#ifndef NDEBUG
class QueryStatContainerExact_c : public QueryStatContainer_i
{
public:
	virtual void						Add ( uint64_t uFoundRows, uint64_t uQueryTime, uint64_t uTimestamp );
	virtual void						GetRecord ( int iRecord, QueryStatRecord_t & tRecord ) const;
	virtual int							GetNumRecords() const;

	QueryStatContainerExact_c();
	QueryStatContainerExact_c ( QueryStatContainerExact_c && tOther );
	QueryStatContainerExact_c & operator= ( QueryStatContainerExact_c && tOther );

private:
	struct QueryStatRecordExact_t
	{
		uint64_t	m_uQueryTime;
		uint64_t	m_uFoundRows;
		uint64_t	m_uTimestamp;
	};

	CircularBuffer_T<QueryStatRecordExact_t> m_dRecords;
};
#endif

struct ServedDesc_t
{
	CSphIndex *			m_pIndex;
	CSphString			m_sIndexPath;
	CSphString			m_sNewPath;
	bool				m_bEnabled;		///< to disable index in cases when rotation fails
	bool				m_bMlock;
	bool				m_bPreopen;
	bool				m_bExpand;
	bool				m_bToDelete;
	bool				m_bOnlyNew;
	bool				m_bRT;
	CSphString			m_sGlobalIDFPath;
	bool				m_bOnDiskAttrs;
	bool				m_bOnDiskPools;
	int64_t				m_iMass; // relative weight (by access speed) of the index
	bool				m_bPercolate;

						ServedDesc_t ();
	virtual				~ServedDesc_t ();
};


class ServedStats_c
{
public:
						ServedStats_c();
	virtual				~ServedStats_c();
	
	void				AddQueryStat ( uint64_t uFoundRows, uint64_t uQueryTime );
	void				CalculateQueryStats ( QueryStats_t & tRowsFoundStats, QueryStats_t & tQueryTimeStats ) const;
#ifndef NDEBUG
	void				CalculateQueryStatsExact ( QueryStats_t & tRowsFoundStats, QueryStats_t & tQueryTimeStats ) const;
#endif
protected:
	virtual void		LockStats ( bool bReader ) const = 0;
	virtual void		UnlockStats() const = 0;

private:
	QueryStatContainer_c m_tQueryStatRecords;

#ifndef NDEBUG
	QueryStatContainerExact_c m_tQueryStatRecordsExact;
#endif

	TDigest_i *			m_pQueryTimeDigest;
	TDigest_i *			m_pRowsFoundDigest;

	uint64_t			m_uTotalFoundRowsMin;
	uint64_t			m_uTotalFoundRowsMax;
	uint64_t			m_uTotalFoundRowsSum;

	uint64_t			m_uTotalQueryTimeMin;
	uint64_t			m_uTotalQueryTimeMax;
	uint64_t			m_uTotalQueryTimeSum;

	uint64_t			m_uTotalQueries;

	static void			CalcStatsForInterval ( const QueryStatContainer_i * pContainer, QueryStatElement_t & tRowResult, QueryStatElement_t & tTimeResult, uint64_t uTimestamp, uint64_t uInterval, int iRecords );
	void				DoStatCalcStats ( const QueryStatContainer_i * pContainer, QueryStats_t & tRowsFoundStats, QueryStats_t & tQueryTimeStats ) const;
};


class ServedIndex_c : public ISphNoncopyable, public ServedDesc_t, public ServedStats_c
{
public:
	ServedIndex_c ();
	virtual ~ServedIndex_c ();

	void				ReadLock () const ACQUIRE_SHARED( m_tLock );
	void				WriteLock () const ACQUIRE( m_tLock );
	void				Unlock () const UNLOCK_FUNCTION( m_tLock );

	bool				InitLock () const;

	void				Release () const;
	void				SetUnlink ( const char * sUnlnk );

protected:
	virtual void		LockStats ( bool bReader ) const;
	virtual void		UnlockStats() const;

	void				AddRef () const;

private:
	mutable CSphRwlock	m_tLock;
	mutable CSphRwlock	m_tStatsLock;
	mutable CSphAtomic	m_tRefCount;
	CSphString			m_sUnlink;
};

typedef SmallStringHash_T<ServedIndex_c *> IndexHash_t;

/// global index hash
/// used in both non-threaded and multi-threaded modes
///
/// hash entry is a CSphIndex pointer, rwlock, and a few flags (see ServedIndex_t)
/// rlock on entry guarantees it won't change, eg. that index pointer will stay alive
/// wlock on entry allows to change (delete/replace) the index pointer
///
/// note that entry locks are held outside the hash
/// and Delete() honours that by acquiring wlock on an entry first
class IndexHash_c : public ISphNoncopyable 
{
	friend class IndexHashIterator_c;

public:
	IndexHash_c ();
	~IndexHash_c ();

	int						GetLength() const { return m_hIndexes.GetLength(); }
	void					Reset();

	bool					Add ( const ServedDesc_t & tDesc, const CSphString & tKey );
	void					Set ( ServedDesc_t & tDesc, const CSphString & tKey );	
	bool					Delete ( const CSphString & tKey );

	ServedIndex_c *			GetRlockedEntry ( const CSphString & tKey ) const EXCLUDES (m_tLock);
	ServedIndex_c *			GetWlockedEntry ( const CSphString & tKey ) const EXCLUDES (m_tLock);
	bool					Exists ( const CSphString & tKey, ServedDesc_t * pDesc = NULL ) const;

protected:
	void					Rlock () const ACQUIRE_SHARED( m_tLock );
	void					Wlock () const ACQUIRE( m_tLock );
	void					Unlock () const UNLOCK_FUNCTION ( m_tLock );

private:
	mutable CSphRwlock m_tLock;
	IndexHash_t m_hIndexes;
};


/// multi-threaded hash iterator
class IndexHashIterator_c : public ISphNoncopyable
{
public:
	explicit			IndexHashIterator_c ( const IndexHash_c * pHash, bool bWrite=false );
	~IndexHashIterator_c ();

	bool				Next ();
	ServedIndex_c &		Get ();
	const CSphString &	GetKey ();

private:
	const IndexHash_c * m_pHash;
	void *	m_pIterator;
};

extern IndexHash_c *						g_pLocalIndexes;	// served (local) indexes hash
extern IndexHash_c *						g_pTemplateIndexes;	// template (tokenizer) indexes hash


enum SqlStmt_e
{
	STMT_PARSE_ERROR = 0,
	STMT_DUMMY,

	STMT_SELECT,
	STMT_INSERT,
	STMT_REPLACE,
	STMT_DELETE,
	STMT_SHOW_WARNINGS,
	STMT_SHOW_STATUS,
	STMT_SHOW_META,
	STMT_SET,
	STMT_BEGIN,
	STMT_COMMIT,
	STMT_ROLLBACK,
	STMT_CALL, // check.pl STMT_CALL_SNIPPETS STMT_CALL_KEYWORDS
	STMT_DESCRIBE,
	STMT_SHOW_TABLES,
	STMT_UPDATE,
	STMT_CREATE_FUNCTION,
	STMT_DROP_FUNCTION,
	STMT_ATTACH_INDEX,
	STMT_FLUSH_RTINDEX,
	STMT_FLUSH_RAMCHUNK,
	STMT_SHOW_VARIABLES,
	STMT_TRUNCATE_RTINDEX,
	STMT_SELECT_SYSVAR,
	STMT_SHOW_COLLATION,
	STMT_SHOW_CHARACTER_SET,
	STMT_OPTIMIZE_INDEX,
	STMT_SHOW_AGENT_STATUS,
	STMT_SHOW_INDEX_STATUS,
	STMT_SHOW_PROFILE,
	STMT_ALTER_ADD,
	STMT_ALTER_DROP,
	STMT_SHOW_PLAN,
	STMT_SELECT_DUAL,
	STMT_SHOW_DATABASES,
	STMT_CREATE_PLUGIN,
	STMT_DROP_PLUGIN,
	STMT_SHOW_PLUGINS,
	STMT_SHOW_THREADS,
	STMT_FACET,
	STMT_ALTER_RECONFIGURE,
	STMT_SHOW_INDEX_SETTINGS,
	STMT_FLUSH_INDEX,
	STMT_RELOAD_PLUGINS,
	STMT_RELOAD_INDEX,
	STMT_FLUSH_HOSTNAMES,
	STMT_FLUSH_LOGS,
	STMT_RELOAD_INDEXES,
	STMT_SYSFILTERS,

	STMT_TOTAL
};


enum SqlSet_e
{
	SET_LOCAL,
	SET_GLOBAL_UVAR,
	SET_GLOBAL_SVAR,
	SET_INDEX_UVAR
};

/// refcounted vector
template < typename T >
class RefcountedVector_c : public CSphVector<T>, public ISphRefcounted
{
};

typedef CSphRefcountedPtr < RefcountedVector_c<SphAttr_t> > AttrValues_p;

/// insert value
struct SqlInsert_t
{
	int						m_iType = 0;
	CSphString				m_sVal;		// OPTIMIZE? use char* and point to node?
	int64_t					m_iVal = 0;
	float					m_fVal = 0.0;
	AttrValues_p			m_pVals;

	SqlInsert_t ()
		: m_pVals ( nullptr )
	{}
};

/// parsing result
/// one day, we will start subclassing this
struct SqlStmt_t
{
	SqlStmt_e				m_eStmt = STMT_PARSE_ERROR;
	int						m_iRowsAffected = 0;
	const char *			m_sStmt = nullptr; // for error reporting

	// SELECT specific
	CSphQuery				m_tQuery;

	CSphString				m_sTableFunc;
	CSphVector<CSphString>	m_dTableFuncArgs;

	// used by INSERT, DELETE, CALL, DESC, ATTACH, ALTER, RELOAD INDEX
	CSphString				m_sIndex;

	// INSERT (and CALL) specific
	CSphVector<SqlInsert_t>	m_dInsertValues; // reused by CALL
	CSphVector<CSphString>	m_dInsertSchema;
	int						m_iSchemaSz = 0;

	// SET specific
	CSphString				m_sSetName;		// reused by ATTACH
	SqlSet_e				m_eSet = SET_LOCAL;
	int64_t					m_iSetValue = 0;
	CSphString				m_sSetValue;
	CSphVector<SphAttr_t>	m_dSetValues;
	bool					m_bSetNull = false;

	// CALL specific
	CSphString				m_sCallProc;
	CSphVector<CSphString>	m_dCallOptNames;
	CSphVector<SqlInsert_t>	m_dCallOptValues;
	CSphVector<CSphString>	m_dCallStrings;

	// UPDATE specific
	CSphAttrUpdate			m_tUpdate;
	int						m_iListStart = -1; // < the position of start and end of index's definition in original query.
	int						m_iListEnd = -1;

	// CREATE/DROP FUNCTION, INSTALL PLUGIN specific
	CSphString				m_sUdfName; // FIXME! move to arg1?
	CSphString				m_sUdfLib;
	ESphAttr				m_eUdfType = SPH_ATTR_NONE;

	// ALTER specific
	CSphString				m_sAlterAttr;
	ESphAttr				m_eAlterColType = SPH_ATTR_NONE;

	// SHOW THREADS specific
	int						m_iThreadsCols = 0;

	// generic parameter, different meanings in different statements
	// filter pattern in DESCRIBE, SHOW TABLES / META / VARIABLES
	// target index name in ATTACH
	// token filter options in INSERT
	// plugin type in INSTALL PLUGIN
	// path in RELOAD INDEX
	CSphString				m_sStringParam;

	// generic integer parameter, used in SHOW SETTINGS, default value -1
	int						m_iIntParam = -1;

	SqlStmt_t ();
	bool AddSchemaItem ( const char * psName );
	// check if the number of fields which would be inserted is in accordance to the given schema
	bool CheckInsertIntegrity();
};

/// result set aggregated across indexes
struct AggrResult_t : CSphQueryResult
{
	CSphVector<CSphSchema>			m_dSchemas;			///< aggregated result sets schemas (for schema minimization)
	CSphVector<int>					m_dMatchCounts;		///< aggregated result sets lengths (for schema minimization)
	CSphVector<const CSphIndex*>	m_dLockedAttrs;		///< indexes which are hold in the memory until sending result
	CSphTaggedVector				m_dTag2Pools;		///< tag to MVA and strings storage pools mapping
	CSphVector<CSphString>			m_dZeroCount;

	AggrResult_t () {}
	virtual ~AggrResult_t () {}
	void ClampMatches ( int iLimit, bool bCommonSchema );
};


class ISphSearchHandler
{
public:
									ISphSearchHandler () {}
	virtual							~ISphSearchHandler() {}
	virtual void					RunQueries () = 0;					///< run all queries, get all results

	virtual void					SetQuery ( int iQuery, const CSphQuery & tQuery ) = 0;
	virtual void					SetProfile ( CSphQueryProfile * pProfile ) = 0;
	virtual AggrResult_t *			GetResult ( int iResult ) = 0;
};


class CSphSessionAccum
{
public:
	explicit	CSphSessionAccum ( bool bManage );
				~CSphSessionAccum();

	ISphRtAccum * GetAcc ( ISphRtIndex * pIndex, CSphString & sError );
	ISphRtIndex * GetIndex ();

private:
	ISphRtAccum *		m_pAcc;
	bool				m_bManage;
};


// from mysqld_error.h
enum MysqlErrors_e
{
	MYSQL_ERR_UNKNOWN_COM_ERROR			= 1047,
	MYSQL_ERR_SERVER_SHUTDOWN			= 1053,
	MYSQL_ERR_PARSE_ERROR				= 1064,
	MYSQL_ERR_FIELD_SPECIFIED_TWICE		= 1110,
	MYSQL_ERR_NO_SUCH_TABLE				= 1146
};

class SqlRowBuffer_c;

class StmtErrorReporter_i
{
public:
	virtual void Ok ( int iAffectedRows, const CSphString & sWarning ) = 0;
	virtual void Ok ( int iAffectedRows, int nWarnings=0 ) = 0;
	virtual void Error ( const char * sStmt, const char * sError, MysqlErrors_e iErr = MYSQL_ERR_PARSE_ERROR ) = 0;

	virtual SqlRowBuffer_c * GetBuffer() = 0;
};


class QueryParserFactory_i
{
public:
	virtual QueryParser_i * Create() const = 0;
};


enum ESphHttpStatus
{
	SPH_HTTP_STATUS_200,
	SPH_HTTP_STATUS_206,
	SPH_HTTP_STATUS_400,
	SPH_HTTP_STATUS_500,
	SPH_HTTP_STATUS_501,
	SPH_HTTP_STATUS_503,

	SPH_HTTP_STATUS_TOTAL
};


bool CheckCommandVersion ( WORD uVer, WORD uDaemonVersion, ISphOutputBuffer & tOut );
ISphSearchHandler * sphCreateSearchHandler ( int iQueries, const QueryParser_i * pQueryParser, bool bSphinxQL, bool bMaster, int iCID );
void sphFormatFactors ( CSphVector<BYTE> & dOut, const unsigned int * pFactors, bool bJson );
bool sphLoopClientHttp ( CSphVector<BYTE> & dData, int iCID );
void sphHttpErrorReply ( CSphVector<BYTE> & dData, ESphHttpStatus eCode, const char * szError );
bool sphParseSqlQuery ( const char * sQuery, int iLen, CSphVector<SqlStmt_t> & dStmt, CSphString & sError, ESphCollation eCollation );
void sphHandleMysqlInsert ( StmtErrorReporter_i & tOut, const SqlStmt_t & tStmt, bool bReplace, bool bCommit, CSphString & sWarning, CSphSessionAccum & tAcc, ESphCollation	eCollation );
void sphHandleMysqlUpdate ( StmtErrorReporter_i & tOut, const QueryParserFactory_i & tQueryParserFactory, const SqlStmt_t & tStmt, const CSphString & sQuery, CSphString & sWarning, int iCID );
void sphHandleMysqlDelete ( StmtErrorReporter_i & tOut, const QueryParserFactory_i & tQueryParserFactory, const SqlStmt_t & tStmt, const CSphString & sQuery, bool bCommit, CSphSessionAccum & tAcc, int iCID );

// get tokens from sphinxql
int sphGetTokTypeInt();
int sphGetTokTypeFloat();
int sphGetTokTypeStr();
int sphGetTokTypeConstMVA();


#endif // _searchdaemon_

//
// $Id$
//
