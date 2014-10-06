
#ifndef __vakit_nal_types_h
#define __vakit_nal_types_h

#include "vakit/bitstream.h"

extern "C"
{
#include <va/va.h>
#include <va/va_enc_h264.h>
}

namespace vakit
{

int build_packed_pic_buffer( bitstream& bs,
                             VAEncPictureParameterBufferH264& pps,
                             bool annexB = true );

int build_packed_seq_buffer( bitstream& bs,
                             VAEncSequenceParameterBufferH264& sps,
                             const VAProfile& h264Profile,
                             int32_t constraintSetFlag,
                             uint32_t numUnitsInTick,
                             uint32_t timeScale,
                             uint32_t frameBitrate,
                             bool annexB = true );

}

#endif
