
#ifndef __vakit_vah264_encoder_h
#define __vakit_vah264_encoder_h

extern "C"
{
#include <va/va.h>
#include <va/va_enc_h264.h>
#include <va/va_drm.h>
}

#include "cppkit/ck_types.h"
#include "cppkit/ck_socket.h"
#include "cppkit/ck_memory.h"
#include "avkit/av_packet.h"
#include "avkit/options.h"

#include <unistd.h>
#include <fcntl.h>

namespace vakit
{

const size_t NUM_REFERENCE_FRAMES = 16;

class vah264_encoder
{
public:
    enum vah264_encoder_frame_type
    {
        FRAME_TYPE_KEY,
        FRAME_TYPE_PARTIAL,
        FRAME_TYPE_AUTO_GOP
    };

    vah264_encoder( const struct avkit::codec_options& options, bool annexB = true );
    virtual ~vah264_encoder() throw();

    void encode_yuv420p( std::shared_ptr<avkit::av_packet> input,
                         vah264_encoder_frame_type type = FRAME_TYPE_AUTO_GOP );

    std::shared_ptr<avkit::av_packet> get();

    bool last_was_key() const;

    struct avkit::codec_options get_options() const;

    std::shared_ptr<cppkit::ck_memory> get_extra_data() const;

private:

    int32_t _compute_current_frame_type( uint32_t currentFrameNum,
                                         int32_t intraPeriod,
                                         vah264_encoder_frame_type type ) const;

    void _update_reference_frames();
    void _update_ref_pic_list();
    void _render_sequence();
    int32_t _calc_poc( int32_t picOrderCntLSB );
    void _render_picture( bool done );
    void _render_slice();
    void _upload_image( uint8_t* yv12, VAImage& image, uint16_t width, uint16_t height );

    bool _annexB;
    int _fd;
    VADisplay _display;
    VAProfile _h264Profile;
    VAConfigID _configID;
    VASurfaceID _srcSurfaceID;
    VABufferID _codedBufID;
    VASurfaceID _refSurfaceIDs[NUM_REFERENCE_FRAMES];
    VAContextID _contextID;
    VAEncSequenceParameterBufferH264 _seqParam;
    VAEncPictureParameterBufferH264 _picParam;
    VAEncSliceParameterBufferH264 _sliceParam;
    VAPictureH264 _currentCurrPic;
    VAPictureH264 _referenceFrames[NUM_REFERENCE_FRAMES];
    VAPictureH264 _refPicListP[32];
    uint32_t _numShortTerm;
    int32_t _constraintSetFlag;
    int32_t _h264EntropyMode;
    int32_t _frameWidth;
    int32_t _frameWidthMBAligned;
    int32_t _frameHeight;
    int32_t _frameHeightMBAligned;
    uint32_t _frameBitRate;
    int32_t _intraPeriod;
    uint64_t _currentIDRDisplay;
    uint32_t _currentFrameNum;
    int32_t _currentFrameType;
    uint32_t _timeBaseNum;
    uint32_t _timeBaseDen;
    std::shared_ptr<cppkit::ck_memory> _extraData;
    std::shared_ptr<avkit::av_packet> _pkt;
    struct avkit::codec_options _options;
};

}

#endif
