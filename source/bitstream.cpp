
#include "vakit/bitstream.h"
#include "cppkit/ck_socket.h"

using namespace cppkit;
using namespace vakit;

static const uint32_t BITSTREAM_ALLOCATE_STEPPING = 4096;

bitstream::bitstream() :
    _bitOffset( 0 ),
    _maxSizeInDword( BITSTREAM_ALLOCATE_STEPPING ),
    _buffer( (uint32_t*)calloc( _maxSizeInDword * sizeof( int32_t ), 1 ) )
{
    if( !_buffer )
        CK_THROW(( "Unable to allocate _buffer." ));
}

bitstream::~bitstream() throw()
{
    free( _buffer );
}

void bitstream::end()
{
    int pos = (_bitOffset >> 5);
    int bitOffset = (_bitOffset & 0x1f);
    int bitLeft = 32 - bitOffset;

    if (bitOffset)
        _buffer[pos] = x_htonl( (_buffer[pos] << bitLeft) );
}

void bitstream::put_ui( uint32_t val, int32_t size_in_bits )
{
    int pos = (_bitOffset >> 5);
    int bitOffset = (_bitOffset & 0x1f);
    int bitLeft = 32 - bitOffset;

    if (!size_in_bits)
        return;

    _bitOffset += size_in_bits;

    if (bitLeft > size_in_bits)
        _buffer[pos] = (_buffer[pos] << size_in_bits | val);
    else
    {
        size_in_bits -= bitLeft;
        _buffer[pos] = (_buffer[pos] << bitLeft) | (val >> size_in_bits);
        _buffer[pos] = x_htonl(_buffer[pos]);

        if (pos + 1 == _maxSizeInDword)
        {
            _maxSizeInDword += BITSTREAM_ALLOCATE_STEPPING;
            _buffer = (unsigned int*)realloc(_buffer, _maxSizeInDword * sizeof(unsigned int));
            if( !_buffer )
                CK_THROW(( "Unable to reallocate buffer." ));
        }

        _buffer[pos + 1] = val;
    }
}

void bitstream::put_ue( int32_t val )
{
    int sizeInBits = 0;
    int tmpVal = ++val;

    while (tmpVal) {
        tmpVal >>= 1;
        sizeInBits++;
    }

    put_ui( 0, sizeInBits - 1); // leading zero
    put_ui( val, sizeInBits );
}

void bitstream::put_se( int32_t val )
{
    unsigned int newVal;

    if (val <= 0)
        newVal = -2 * val;
    else
        newVal = 2 * val - 1;

    put_ue( newVal );
}

void bitstream::byte_aligning( int32_t bit )
{
    int bitOffset = (_bitOffset & 0x7);
    int bitLeft = 8 - bitOffset;
    int new_val;

    if (!bitOffset)
        return;

    if( (bit != 0) && (bit != 1) )
        CK_THROW(( "Asked to byte align invalid bit." ));

    if (bit)
        new_val = (1 << bitLeft) - 1;
    else
        new_val = 0;

    put_ui( new_val, bitLeft );
}

uint8_t* bitstream::map()
{
    return (uint8_t*)_buffer;
}

size_t bitstream::size()
{
   return ((_bitOffset % 8) == 0) ? _bitOffset / 8 : (_bitOffset / 8) + 1;
}

size_t bitstream::size_in_bits()
{
    return _bitOffset;
}
