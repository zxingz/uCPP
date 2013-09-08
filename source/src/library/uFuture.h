//                              -*- Mode: C++ -*- 
// 
// uC++ Version 6.0.0, Copyright (C) Peter A. Buhr and Richard C. Bilson 2006
// 
// Future.h -- 
// 
// Author           : Peter A. Buhr and Richard C. Bilson
// Created On       : Wed Aug 30 22:34:05 2006
// Last Modified By : Peter A. Buhr
// Last Modified On : Fri Dec  9 13:00:24 2011
// Update Count     : 526
// 
// This  library is free  software; you  can redistribute  it and/or  modify it
// under the terms of the GNU Lesser General Public License as published by the
// Free Software  Foundation; either  version 2.1 of  the License, or  (at your
// option) any later version.
// 
// This library is distributed in the  hope that it will be useful, but WITHOUT
// ANY  WARRANTY;  without even  the  implied  warranty  of MERCHANTABILITY  or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
// for more details.
// 
// You should  have received a  copy of the  GNU Lesser General  Public License
// along  with this library.
// 

#ifndef __U_FUTURE_H__
#define __U_FUTURE_H__


//############################## uBaseFuture ##############################


namespace UPP {
    template<typename T> _Monitor uBaseFuture {
	T result;					// future result
      public:
	_Event Cancellation {};				// raised if future cancelled

	// These members should be private but cannot be because they are referenced from user code.

	bool addAccept( UPP::BaseFutureDL *acceptState ) {
	    if ( available() ) return false;
	    acceptClients.addTail( acceptState );
	    return true;
	} // uBaseFuture::addAccept

	void removeAccept( UPP::BaseFutureDL *acceptState ) {
	    acceptClients.remove( acceptState );
	} // uBaseFuture::removeAccept
      protected:
	uCondition delay;				// clients waiting for future result
	uSequence<UPP::BaseFutureDL> acceptClients;	// clients waiting for future result in selection
	uBaseEvent *cause;				// synchronous exception raised during future computation
	bool available_, cancelled_;			// future status

	void makeavailable() {
	    available_ = true;
	    while ( ! delay.empty() ) delay.signal();	// unblock waiting clients ?
	    if ( ! acceptClients.empty() ) {		// select-blocked clients ?
		UPP::BaseFutureDL *bt;			// unblock select-blocked clients
		for ( uSeqIter<UPP::BaseFutureDL> iter( acceptClients ); iter >> bt; ) {
		    bt->signal();
		} // for
	    } // if
	} // uBaseFuture::makeavailable

	void check() {
	    if ( cancelled() ) _Throw Cancellation();
	    if ( cause != NULL ) cause->reraise();
	} // uBaseFuture::check
      public:
	uBaseFuture() : cause( NULL ), available_( false ), cancelled_( false ) {}

	_Nomutex bool available() { return available_; } // future result available ?
	_Nomutex bool cancelled() { return cancelled_; } // future result cancelled ?

	// USED BY CLIENT

	T operator()() {				// access result, possibly having to wait
	    check();					// cancelled or exception ?
	    if ( ! available() ) {
		delay.wait();
		check();				// cancelled or exception ?
	    } // if
	    return result;
	} // uBaseFuture::operator()()

	_Nomutex operator T() {				// cheap access of result after waiting
	    check();					// cancelled or exception ?
#ifdef __U_DEBUG__
	    if ( ! available() ) {
		uAbort( "Attempt to access future result %p without first performing a blocking access operation.", this );
	    } // if
#endif // __U_DEBUG__
	    return result;
	} // uBaseFuture::operator T()

	// USED BY SERVER

	bool delivery( T res ) {			// make result available in the future
	    if ( cancelled() || available() ) return false; // ignore, client does not want it or already set
	    result = res;
	    makeavailable();
	    return true;
	} // uBaseFuture::delivery

	bool exception( uBaseEvent *ex ) {		// make exception available in the future : exception and result mutual exclusive
	    if ( cancelled() || available() ) return false; // ignore, client does not want it or already set
	    cause = ex;
	    makeavailable();				// unblock waiting clients ?
	    return true;
	} // uBaseFuture::exception

	void reset() {					// mark future as empty (for reuse)
	    available_ = cancelled_ = false;		// reset for next value
	    delete cause;
	    cause = NULL;
	} // uBaseFuture::reset
    }; // uBaseFuture
} // UPP


//############################## Future_ESM ##############################


// Caller is responsible for storage management by preallocating the future and passing it as an argument to the
// asynchronous call.  Cannot be copied.

template<typename T, typename ServerData> _Monitor Future_ESM : public UPP::uBaseFuture<T> {
    using UPP::uBaseFuture<T>::cancelled_;
    bool cancelInProgress;

    void makeavailable() {
	cancelInProgress = false;
	cancelled_ = true;
	UPP::uBaseFuture<T>::makeavailable();
    } // Future_ESM::makeavailable

    _Mutex int checkCancel() {
      if ( available() ) return 0;			// already available, can't cancel
      if ( cancelled() ) return 0;			// only cancel once
      if ( cancelInProgress ) return 1;
	cancelInProgress = true;
	return 2;
    } // Future_ESM::checkCancel

    _Mutex void compCancelled() {
	makeavailable();
    } // Future_ESM::compCancelled

    _Mutex void compNotCancelled() {
	// Race by server to deliver and client to cancel.  While the future is already cancelled, the server can
	// attempt to signal (unblock) this client before the client can block, so the signal is lost.
	if ( cancelInProgress ) {			// must recheck
	    delay.wait();				// wait for cancellation
	} // if
    } // Future_ESM::compNotCancelled
  public:
    using UPP::uBaseFuture<T>::available;
    using UPP::uBaseFuture<T>::reset;
    using UPP::uBaseFuture<T>::makeavailable;
    using UPP::uBaseFuture<T>::check;
    using UPP::uBaseFuture<T>::delay;
    using UPP::uBaseFuture<T>::cancelled;

    Future_ESM() : cancelInProgress( false ) {}
    ~Future_ESM() { reset(); }

    // USED BY CLIENT

    _Nomutex void cancel() {				// cancel future result
	// To prevent deadlock, call the server without holding future mutex, because server may attempt to deliver a
	// future value. (awkward code)
	unsigned int rc = checkCancel();
      if ( rc == 0 ) return;				// need to contact server ?
	if ( rc == 1 ) {				// need to contact server ?
	    compNotCancelled();				// server computation not cancelled yet, wait for cancellation
	} else {
	    if ( serverData.cancel() ) {		// synchronously contact server
		compCancelled();			// computation cancelled, announce cancellation
	    } else {
		compNotCancelled();			// server computation not cancelled yet, wait for cancellation
	    } // if
	} // if
    } // Future_ESM::cancel

    // USED BY SERVER

    ServerData serverData;				// information needed by server

    bool delivery( T res ) {				// make result available in the future
	if ( cancelInProgress ) {
	    makeavailable();
	    return true;
	} else {
	    return UPP::uBaseFuture<T>::delivery( res );
	} // if
    } // Future_ESM::delivery

    bool exception( uBaseEvent *ex ) {			// make exception available in the future : exception and result mutual exclusive
	if ( cancelInProgress ) {
	    makeavailable();
	    return true;
	} else {
	    return UPP::uBaseFuture<T>::exception( ex );
	} // if
    } // Future_ESM::exception
}; // Future_ESM


template< typename Result, typename ServerData, typename Other >
UPP::BinarySelector< UPP::OrCondition, UPP::UnarySelector< Future_ESM< Result, ServerData >, int >, UPP::UnarySelector< Other, int >, int > operator||( const Future_ESM< Result, ServerData > &s1, const Other &s2 ) {
    return UPP::BinarySelector< UPP::OrCondition,UPP::UnarySelector< Future_ESM< Result, ServerData >, int >, UPP::UnarySelector< Other, int >, int >( UPP::UnarySelector< Future_ESM< Result, ServerData >, int >( s1 ), UPP::UnarySelector< Other, int >( s2 ) );
} // operator||

template< typename Result, typename ServerData, typename Other >
UPP::BinarySelector< UPP::AndCondition, UPP::UnarySelector< Future_ESM< Result, ServerData >, int >, UPP::UnarySelector< Other, int >, int > operator&&( const Future_ESM< Result, ServerData > &s1, const Other &s2 ) {
    return UPP::BinarySelector< UPP::AndCondition, UPP::UnarySelector< Future_ESM< Result, ServerData >, int >, UPP::UnarySelector< Other, int >, int >( UPP::UnarySelector< Future_ESM< Result, ServerData >, int >( s1 ), UPP::UnarySelector< Other, int >( s2 ) );
} // operator&&


//############################## Future_ISM ##############################


// Future is responsible for storage management by using reference counts.  Can be copied.

template<typename T> class Future_ISM {
  public:
    struct ServerData {
	virtual ~ServerData() {}
	virtual bool cancel() = 0;
    };
  private:
    _Monitor Impl : public UPP::uBaseFuture<T> {	// mutual exclusion implementation
	using UPP::uBaseFuture<T>::cancelled_;
	using UPP::uBaseFuture<T>::cause;

	unsigned int refCnt;				// number of references to future
	ServerData *serverData;
      public:
	using UPP::uBaseFuture<T>::available;
	using UPP::uBaseFuture<T>::reset;
	using UPP::uBaseFuture<T>::makeavailable;
	using UPP::uBaseFuture<T>::check;
	using UPP::uBaseFuture<T>::delay;
	using UPP::uBaseFuture<T>::cancelled;

	Impl() : refCnt( 1 ), serverData( NULL ) {}
	Impl( ServerData *serverData_ ) : refCnt( 1 ), serverData( serverData_ ) {}

	~Impl() {
	    delete serverData;
	} // Impl::~Impl

	void incRef() {
	    refCnt += 1;
	} // Impl::incRef

	bool decRef() {
	    refCnt -= 1;
	  if ( refCnt != 0 ) return false;
	    delete cause;
	    return true;
	} // Impl::decRef

	void cancel() {					// cancel future result
	  if ( available() ) return;			// already available, can't cancel
	  if ( cancelled() ) return;			// only cancel once
	    cancelled_ = true;
	    if ( serverData != NULL ) serverData->cancel();
	    makeavailable();				// unblock waiting clients ?
	} // Impl::cancel
    }; // Impl

    Impl *impl;						// storage for implementation
  public:
    Future_ISM() : impl( new Impl ) {}
    Future_ISM( ServerData *serverData ) : impl( new Impl( serverData ) ) {}

    ~Future_ISM() {
	if ( impl->decRef() ) delete impl;
    } // Future_ISM::~Future_ISM

    Future_ISM( const Future_ISM<T> &rhs ) {
	impl = rhs.impl;				// point at new impl
	impl->incRef();					//   and increment reference count
    } // Future_ISM::Future_ISM

    Future_ISM<T> &operator=( const Future_ISM<T> &rhs ) {
      if ( rhs.impl == impl ) return *this;
	if ( impl->decRef() ) delete impl;		// no references => delete current impl
	impl = rhs.impl;				// point at new impl
	impl->incRef();					//   and increment reference count
	return *this;
    } // Future_ISM::operator=

    // USED BY CLIENT

    typedef typename UPP::uBaseFuture<T>::Cancellation Cancellation; // raised if future cancelled

    bool available() { return impl->available(); }	// future result available ?
    bool cancelled() { return impl->cancelled(); }	// future result cancelled ?

    T operator()() {					// access result, possibly having to wait
	return (*impl)();
    } // Future_ISM::operator()()

    operator T() {					// cheap access of result after waiting
	return (T)(*impl);
    } // Future_ISM::operator T()

    void cancel() {					// cancel future result
	impl->cancel();
    } // Future_ISM::cancel

    bool addAccept( UPP::BaseFutureDL *acceptState ) {
	return impl->addAccept( acceptState );
    } // Future_ISM::addAccept

    void removeAccept( UPP::BaseFutureDL *acceptState ) {
	return impl->removeAccept( acceptState );
    } // Future_ISM::removeAccept

    bool equals( const Future_ISM<T> &other ) {		// referential equality
	return impl == other.impl;
    } // Future_ISM::equals

    // USED BY SERVER

    bool delivery( T result ) {				// make result available in the future
	return impl->delivery( result );
    } // Future_ISM::delivery

    bool exception( uBaseEvent *cause ) {		// make exception available in the future
	return impl->exception( cause );
    } // Future_ISM::exception

    void reset() {					// mark future as empty (for reuse)
	impl->reset();
    } // Future_ISM::reset
}; // Future_ISM


template< typename Result, typename Other >
UPP::BinarySelector< UPP::OrCondition, Future_ISM< Result >, UPP::UnarySelector< Other, int >, int > operator||( const Future_ISM< Result > &s1, const Other &s2 ) {
    return UPP::BinarySelector< UPP::OrCondition, Future_ISM< Result >, UPP::UnarySelector< Other, int >, int >( s1, UPP::UnarySelector< Other, int >( s2 ) );
} // operator||

template< typename Result, typename Other >
UPP::BinarySelector< UPP::AndCondition, Future_ISM< Result >, UPP::UnarySelector< Other, int >, int > operator&&( const Future_ISM< Result > &s1, const Other &s2 ) {
    return UPP::BinarySelector< UPP::AndCondition, Future_ISM< Result >, UPP::UnarySelector< Other, int >, int >( s1, UPP::UnarySelector< Other, int >( s2 ) );
} // operator&&


//############################## uWaitQueue_ISM ##############################


template< typename Selectee >
class uWaitQueue_ISM {
    struct DL;

    struct DropClient {
	UPP::uSemaphore sem; 				// selection client waits if no future available
	unsigned int tst;				// test-and-set for server race
	DL *winner;					// indicate winner of race

	DropClient() : sem( 0 ), tst( 0 ) {};
    }; // DropClient

    struct DL : public uSeqable {
	struct uBaseFutureDL : public UPP::BaseFutureDL {
	    DropClient *client;				// client data for server
	    DL *s;					// iterator corresponding to this DL

	    uBaseFutureDL( DL *t ) : s( t ) {}

	    virtual void signal() {
		if ( uTestSet( client->tst ) == 0 ) {	// returns 0 or non-zero
		    client->winner = s;
		    client->sem.V();			// client see changes because semaphore does memory barriers
		} // if
	    } // signal
	}; // uBaseFutureDL

	uBaseFutureDL acceptState;
	Selectee selectee;

	DL( Selectee t ) : acceptState( this ), selectee( t ) {}
    }; // DL

    uSequence< DL > q;

    uWaitQueue_ISM( const uWaitQueue_ISM & );		// no copy
    uWaitQueue_ISM &operator=( const uWaitQueue_ISM & ); // no assignment
  public:
    uWaitQueue_ISM() {}

    template< typename Iterator > uWaitQueue_ISM( Iterator begin, Iterator end ) {
	add( begin, end );
    } // uWaitQueue_ISM::uWaitQueue_ISM

    ~uWaitQueue_ISM() {
	DL *t;
	for ( uSeqIter< DL > i( q ); i >> t; ) {
	    delete t;
	} // for
    } // uWaitQueue_ISM::~uWaitQueue_ISM

    bool empty() const {
	return q.empty();
    } // uWaitQueue_ISM::empty

    void add( Selectee n ) {
	q.add( new DL( n ) );
    } // uWaitQueue_ISM::add

    template< typename Iterator > void add( Iterator begin, Iterator end ) {
	for ( Iterator i = begin; i != end; ++i ) {
	    add( *i );
	} // for
    } // uWaitQueue_ISM::add

    void remove( Selectee n ) {
	DL *t = 0;
	for ( uSeqIter< DL > i( q ); i >> t; ) {
	    if ( t->selectee.equals( n ) ) {
		q.remove( t );
		delete t;
	    } // if
	} // for
    } // uWaitQueue_ISM::remove

    Selectee drop() {
	if ( q.empty() ) uAbort( "uWaitQueue_ISM: attempt to drop from an empty queue" );

	DropClient client;
	DL *t = 0;
	for ( uSeqIter< DL > i( q ); i >> t; ) {
	    t->acceptState.client = &client;
	    if ( ! t->selectee.addAccept( &t->acceptState ) ) {
		DL *s;
		for ( uSeqIter< DL > i( q ); i >> s && s != t; ) {
		    s->selectee.removeAccept( &s->acceptState );
		} // for
		goto cleanup;
	    } // if
	} // for

	client.sem.P();
	t = client.winner;
	DL *s;
	for ( uSeqIter< DL > i( q ); i >> s; ) {
	    s->selectee.removeAccept( &s->acceptState );
	} // for

      cleanup:
	Selectee selectee = t->selectee;
	q.remove( t );
	delete t;
	return selectee;
    } // uWaitQueue_ISM::drop

    // not implemented, since the "head" of the queue is not fixed i.e., if another item comes ready it may become the
    // new "head" use "drop" instead
    //T *head() const;
}; // uWaitQueue_ISM


//############################## uWaitQueue_ESM ##############################


template< typename Selectee >
class uWaitQueue_ESM {
    struct Helper {
	Selectee *s;
	Helper( Selectee *s ) : s( s ) {}
	bool available() const { return s->available(); }
	bool addAccept( UPP::BaseFutureDL *acceptState ) { return s->addAccept( acceptState ); }
	void removeAccept( UPP::BaseFutureDL *acceptState ) { return s->removeAccept( acceptState ); }
	bool equals( const Helper &other ) const { return s == other.s; }
    }; // Helper

    uWaitQueue_ISM< Helper > q;

    uWaitQueue_ESM( const uWaitQueue_ESM & );		// no copy
    uWaitQueue_ESM &operator=( const uWaitQueue_ESM & ); // no assignment
  public:
    uWaitQueue_ESM() {}

    template< typename Iterator > uWaitQueue_ESM( Iterator begin, Iterator end ) {
	add( begin, end );
    } // uWaitQueue_ESM::uWaitQueue_ESM

    bool empty() const {
	return q.empty();
    } // uWaitQueue_ESM::empty

    void add( Selectee *n ) {
	q.add( Helper( n ) );
    } // uWaitQueue_ESM::add

    template< typename Iterator > void add( Iterator begin, Iterator end ) {
	for ( Iterator i = begin; i != end; ++i ) {
	    add( &*i );
	} // for
    } // uWaitQueue_ESM::add

    void remove( Selectee *s ) {
	q.remove( Helper( s ) );
    } // uWaitQueue_ESM::remove

    Selectee *drop() {
	return empty() ? 0 : q.drop().s;
    } // uWaitQueue_ESM::drop
}; // uWaitQueue_ESM


//############################## uExecutor ##############################


class uExecutor {
    // Buffer is embedded in executor to allow the executor to delete the workers without causing a deadlock.  If the
    // buffer is incorperated into the executor by making it a monitor, the thread calling the executor destructor
    // (which is mutex) blocks when deleting the workers, preventing outstanding workers from calling remove to drain
    // the buffer.
    template<typename ELEMTYPE> _Monitor Buffer {	// unbounded buffer
	uSequence<ELEMTYPE> buf;			// unbounded list of work requests
	uCondition delay;
      public:
	void insert( ELEMTYPE *elem ) {
	    buf.addTail( elem );
	    delay.signal();
	} // Buffer::insert

	ELEMTYPE *remove() {
	    if ( buf.empty() ) delay.wait();		// no request to process ? => wait
	    return buf.dropHead();
	} // Buffer::remove
    }; // Buffer

    class WRequest : public uSeqable {			// worker request
	bool done;
      public:
	WRequest( bool done = false ) : done( done ) {}
	virtual ~WRequest() {};
	virtual bool stop() { return done; };
	virtual void doit() {};
    }; // WRequest

    template<typename R, typename F> struct CRequest : public WRequest { // client request
	F action;
	Future_ISM<R> result;
	void doit() { result.delivery( action() ); }
	CRequest( F action ) : action( action ) {}
    }; // CRequest

    _Task Worker {
	uExecutor &executor;

	void main() {
	    for ( ;; ) {
		WRequest *request = executor.requests.remove();
	      if ( request->stop() ) break;
		request->doit();
		delete request;
	    } // for
	} // Worker::main
      public:
	Worker( uCluster &wc, uExecutor &executor ) : uBaseTask( wc ), executor( executor ) {}
    }; // Worker

    const unsigned int nworkers;			// number of workers tasks
    Buffer<WRequest> requests;				// list of work requests
    Worker **workers;					// array of workers executing work requests
    uProcessor **processors;				//   corresponding number of virtual processors
    uCluster *cluster;					// workers execute on separate cluster
  public:
    uExecutor( unsigned int nworkers = 4 ) : nworkers( nworkers ) {
#if defined( __U_SEPARATE_CLUSTER__ )
	cluster = new uCluster;
#else
	cluster = &uThisCluster();
#endif // __U_SEPARATE_CLUSTER__
	processors = new uProcessor *[ nworkers ];
	workers = new Worker *[ nworkers ];

	for ( unsigned int i = 0; i < nworkers; i += 1 ) {
	    processors[ i ] = new uProcessor( *cluster );
	    workers[ i ] = new Worker( *cluster, *this );
	} // for
    } // uExecutor::uExecutor

    ~uExecutor() {
	WRequest sentinel( true );
	for ( unsigned int i = 0; i < nworkers; i += 1 ) {
	    requests.insert( &sentinel );
	} // for
	unsigned int i;
	for ( i = 0; i < nworkers; i += 1 ) {
	    delete workers[ i ];
	    delete processors[ i ];
	} // for
	delete [] workers;
	delete [] processors;
#if defined( __U_SEPARATE_CLUSTER__ )
	delete cluster;
#endif // __U_SEPARATE_CLUSTER__
    } // uExecutor::~uExecutor

    template <typename Return, typename Func> void submit( Future_ISM<Return> &result, Func action ) {
	CRequest<Return,Func> *node = new CRequest<Return,Func>( action );
	result = node->result;				// race, copy before insert
	requests.insert( node );
    } // uExecutor::submit
}; // uExecutor


#endif // __U_FUTURE_H__


// Local Variables: //
// compile-command: "make install" //
// End: //
