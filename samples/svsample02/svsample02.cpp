/*
Copyright(c) 2002-2017 Anatoliy Kuznetsov(anatoliy_kuznetsov at yahoo.com)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

For more information please visit:  http://bitmagic.io
*/

/** \example svsample02.cpp
  Example of how to serialize bm::sparse_vector<> template class
 
  \sa bm::sparse_vector
  \sa bm::sparse_vector<>::push_back
  \sa bm::sparse_vector<>::equal
  \sa bm::sparse_vector_serialize
  \sa bm::sparse_vector_deserialize
  \sa bm::sparse_vector_serializer
  \sa bm::sparse_vector_deserializer

  \sa rscsample05.cpp
*/

/*! \file svsample02.cpp
    \brief Example: sparse_vector<> serialization

*/

#include <iostream>
#include <vector>
#include <assert.h>

#include "bm.h"
#include "bmsparsevec.h"
#include "bmsparsevec_serial.h"
#include "bmundef.h" /* clear the pre-proc defines from BM */

using namespace std;

typedef bm::sparse_vector<unsigned, bm::bvector<> > svector;

typedef bm::sparse_vector_serializer<svector>   sv_serializer_type;
typedef bm::sparse_vector_deserializer<svector> sv_deserializer_type;

/// Demo 1
/// Simple one function call serialization
///
static
void SDemo1()
{
    svector sv1;
    svector sv2;

    for (unsigned i = 0; i < 128000; ++i)
    {
        sv1.push_back(8);
    }

    // optimize memory allocation of sparse vector
    sv1.optimize();

    bm::sparse_vector_serial_layout<svector> sv_lay;
    bm::sparse_vector_serialize(sv1, sv_lay);

    // copy serialization buffer to some other location
    // to simulate data-base storage or network transaction
    //
    const unsigned char* buf = sv_lay.buf();
    size_t buf_size = sv_lay.size();
    cout << buf_size << endl;

    vector<unsigned char> tmp_buf(buf_size);
    ::memcpy(&tmp_buf[0], buf, buf_size);

    int res = bm::sparse_vector_deserialize(sv2, &tmp_buf[0]);
    if (res != 0)
    {
        cerr << "De-Serialization error!" << endl;
        return;
    }
    if (!sv1.equal(sv2) )
    {
        cerr << "Error! Please report a bug to BitMagic project support." << endl;
        return;
    }
}

/// Demo 2
/// - Reuseable serializer/deserilaizer classes
/// - Shows serializarion with XOR compression enabled
///
static
void SDemo2()
{
    svector sv1(bm::use_null); // NULL-able vector
    svector sv2(bm::use_null);

    for (unsigned i = 0; i < 128000; i+=2)
    {
        sv1.set(i, 8);
    }
    sv1.optimize();
    sv2 = sv1; // copy sv1

    sv_serializer_type sv_ser;
    sv_deserializer_type sv_dser;
    bm::sparse_vector_serial_layout<svector> sv_lay0;

    // the data pattern will allow XOR compression
    // lets try to enable it!

    sv_ser.enable_xor_compression();
    assert(sv_ser.is_xor_ref());
    sv_ser.serialize(sv1, sv_lay0);

    // Get BLOB pointer and size
    const unsigned char* buf = sv_lay0.data();
    size_t sz = sv_lay0.size();
    cout << "XOR compression enabled size=" << sz << endl;

    // deserialize from the memory pointer
    //
    {
        svector sv3(bm::use_null);
        sv_dser.deserialize(sv3, buf);
        assert(sv3.equal(sv1));
    }


    // disbale XOR compression
    // please note that we re-use serializer and deserializer instances
    // to save construction costs (memory allocations, etc)
    //

    sv_ser.disable_xor_compression();
    assert(!sv_ser.is_xor_ref());

    sv_ser.serialize(sv2, sv_lay0);

    buf = sv_lay0.data();
    sz = sv_lay0.size();
    cout << "XOR compression disabled size=" << sz << endl;

    // deserialize from the memory pointer
    //
    {
        svector sv3(bm::use_null);
        sv_dser.deserialize(sv3, buf);
        assert(sv3.equal(sv1));
    }

}

int main(void)
{
    try
    {
        cout << "Demo 1" << endl;
        SDemo1();

        cout << "Demo 2" << endl;
        SDemo2();
    }
    catch(std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    return 0;
}

