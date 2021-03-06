//                              -*- Mode: C++ -*- 
// 
// uC++ Version 5.0, Copyright (C) Ashif S. Harji 2005
// 
// OwnerShip2.cc -- 
// 
// Author           : Ashif S. Harji
// Created On       : Sun Jan  9 16:12:31 2005
// Last Modified By : Peter A. Buhr
// Last Modified On : Tue Jun 26 10:48:19 2007
// Update Count     : 15
// 

#include <iostream>
using std::cout;
using std::osacquire;
using std::endl;

_Task T;

_Cormonitor CM {
    T *t;

    void main() {
	osacquire( cout ) << "CM::main" << endl;
	mem2();
    } // CM::main
  public:
    void mem1( T *t ) {
	osacquire( cout ) << "CM::mem" << endl;
	CM::t = t;
	resume();
    } // CM::mem1

    void mem2();
}; // CM

_Task T {
    CM &cm;

    void main() {
	osacquire( cout ) << "T::main" << endl;
	cm.mem1( this );
    } // T::main
  public:
    T( CM & cm ) : cm( cm ) {}

    void mem() {
	resume();
    } // T::mem
}; // T

void CM::mem2() {
    osacquire( cout ) << "CM::mem2" << endl;
    t->mem();
} // CM::mem2

_Task Worker {
    void main() {}
}; // Worker


// This test checks that creating tasks and processors not directly in uMain::main works correctly
_Monitor nonMainTest {
  public:
    void test() {
	uProcessor processors[2];
        Worker tasks[2];
    } // nonMainTest::test
}; // nonMainTest

void uMain::main() {
    nonMainTest m;
    osacquire( cout ) << "The first test should complete" << endl;
    m.test();
    osacquire( cout ) << "The first test successfully" << endl;
    osacquire( cout ) << "The second test should fail" << endl;
    CM cm;
    T t( cm );
} // uMain::main
