
#ifndef __vakit_bitstream_h
#define __vakit_bitstream_h

#include "cppkit/ck_types.h"

namespace vakit
{

class bitstream
{
public:
    bitstream();
    virtual ~bitstream() throw();

    void end();
    void put_ui( uint32_t val, int32_t size_in_bits );
    void put_ue( int32_t val );
    void put_se( int32_t val );
    void byte_aligning( int32_t bit );

    uint8_t* map();
    size_t size();
    size_t size_in_bits();

private:
    int32_t _bitOffset;
    int32_t _maxSizeInDword;
    uint32_t* _buffer;
};

}

#endif
