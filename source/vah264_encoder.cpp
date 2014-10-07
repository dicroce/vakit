
#include "vakit/vah264_encoder.h"
#include "vakit/bitstream.h"
#include "vakit/nal_types.h"
#include "cppkit/ck_exception.h"

using namespace vakit;
using namespace std;
using namespace cppkit;
using namespace avkit;

const size_t MAX_FRAME_NUM = (2<<16);
const size_t MAX_PIC_ORDER_CNT_LSB = (2<<8);
const size_t LOG_2_MAX_FRAME_NUM = 16;
const size_t LOG_2_MAX_PIC_ORDER_CNT_LSB = 8;
const int32_t H264_MAXREF = (1<<16|1);

const int32_t FRAME_P = 0;
const int32_t FRAME_I = 2;
const int32_t FRAME_IDR = 7;

vah264_encoder::vah264_encoder( const struct avkit::codec_options& options,
                                const cppkit::ck_string& devicePath,
                                bool annexB ) :
    _devicePath( devicePath ),
    _annexB( annexB ),
    _fd( open( _devicePath.c_str(), O_RDWR ) ),
    _display( (VADisplay)vaGetDisplayDRM( _fd ) ),
    _h264Profile( VAProfileH264High ),
    _configID( 0 ),
    _srcSurfaceID( 0 ),
    _codedBufID( 0 ),
    _refSurfaceIDs(),
    _contextID(0),
    _seqParam(),
    _picParam(),
    _sliceParam(),
    _currentCurrPic(),
    _referenceFrames(),
    _refPicListP(),
    _constraintSetFlag( 0 ),
    _h264EntropyMode( 1 ), /* cabac */
    _frameWidth( 0 ),
    _frameWidthMBAligned( 0 ),
    _frameHeight( 0 ),
    _frameHeightMBAligned( 0 ),
    _frameBitRate( 0 ),
    _intraPeriod( 15 ),
    _currentIDRDisplay( 0 ),
    _currentFrameNum( 0 ),
    _currentFrameType( 0 ),
    _timeBaseNum( 0 ),
    _timeBaseDen( 0 ),
    _extraData(),
    _pkt(),
    _options( options )
{
    if( !options.width.is_null() )
    {
        _frameWidth = options.width.value();
        _frameWidthMBAligned = (_frameWidth + 15) & (~15);
    }
    else CK_THROW(( "Required option missing: width" ));

    if( !options.height.is_null() )
    {
        _frameHeight = options.height.value();
        _frameHeightMBAligned = (_frameHeight + 15) & (~15);
    }
    else CK_THROW(( "Required option missing: height" ));

    if( !options.bit_rate.is_null() )
        _frameBitRate = options.bit_rate.value() / 1000;
    else CK_THROW(( "Required option missing: bit_rate" ));

    if( !options.gop_size.is_null() )
        _intraPeriod = options.gop_size.value();

    if( !options.time_base_num.is_null() )
        _timeBaseNum = options.time_base_num.value();
    else CK_THROW(("Required option missing: time_base.num"));

    if( !options.time_base_den.is_null() )
        _timeBaseDen = options.time_base_den.value();
    else CK_THROW(("Required option missing: time_base.den"));

    int major_ver = 0, minor_ver = 0;
    VAStatus status = vaInitialize( _display, &major_ver, &minor_ver );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaInitialize (%s).", vaErrorStr(status) ));

    switch( _h264Profile )
    {
    case VAProfileH264Baseline:
        _constraintSetFlag |= (1 << 0); /* Annex A.2.1 */
        _h264EntropyMode = 0;
        break;
    case VAProfileH264ConstrainedBaseline:
        _constraintSetFlag |= (1 << 0 | 1 << 1); /* Annex A.2.2 */
        _h264EntropyMode = 0;
        break;
    case VAProfileH264Main:
        _constraintSetFlag |= (1 << 1); /* Annex A.2.2 */
        break;
    case VAProfileH264High:
        _constraintSetFlag |= (1 << 3); /* Annex A.2.4 */
        break;
    default:
        _h264Profile = VAProfileH264Baseline;
        _constraintSetFlag |= (1 << 0); /* Annex A.2.1 */
        _h264EntropyMode = 0;
        break;
    }

    VAConfigAttrib configAttrib[VAConfigAttribTypeMax];
    int configAttribNum = 0;

    configAttrib[configAttribNum].type = VAConfigAttribRTFormat;
    configAttrib[configAttribNum].value = VA_RT_FORMAT_YUV420;
    configAttribNum++;

    configAttrib[configAttribNum].type = VAConfigAttribRateControl;
    configAttrib[configAttribNum].value = VA_RC_CBR;
    configAttribNum++;

    status = vaCreateConfig( _display,
                             _h264Profile,
                             VAEntrypointEncSlice,
                             &configAttrib[0],
                             configAttribNum,
                             &_configID );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaCreateConfig (%s).", vaErrorStr(status) ));


    // Configuration below this point can be thought of as specific to a particular
    // encoder channel. I point this out because we might want to split this out
    // someday.

    /* create source surfaces */
    status = vaCreateSurfaces( _display,
                               VA_RT_FORMAT_YUV420,
                               _frameWidthMBAligned,
                               _frameHeightMBAligned,
                               &_srcSurfaceID,
                               1,
                               NULL,
                               0 );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaCreateSurfaces (%s).", vaErrorStr(status) ));

    /* create reference surfaces */
    status = vaCreateSurfaces( _display,
                               VA_RT_FORMAT_YUV420,
                               _frameWidthMBAligned,
                               _frameHeightMBAligned,
                               &_refSurfaceIDs[0],
                               NUM_REFERENCE_FRAMES,
                               NULL,
                               0 );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaCreateSurfaces (%s).", vaErrorStr(status) ));

    VASurfaceID* tmp_surfaceid = (VASurfaceID*)calloc( 2, sizeof(VASurfaceID) );
    memcpy( tmp_surfaceid, &_srcSurfaceID, sizeof(VASurfaceID) );
    memcpy( tmp_surfaceid + 1, &_refSurfaceIDs, sizeof(VASurfaceID) );

    /* Create a context for this encode pipe */
    status = vaCreateContext( _display,
                              _configID,
                              _frameWidthMBAligned,
                              _frameHeightMBAligned,
                              VA_PROGRESSIVE,
                              tmp_surfaceid,
                              2,
                              &_contextID );

    free(tmp_surfaceid);

    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaCreateContext (%s).", vaErrorStr(status) ));

    status = vaCreateBuffer( _display,
                             _contextID,
                             VAEncCodedBufferType,
                             (_frameWidthMBAligned * _frameHeightMBAligned * 400) / (16*16),
                             1,
                             NULL,
                             &_codedBufID );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaCreateBuffer (%s).", vaErrorStr(status) ));
}

vah264_encoder::~vah264_encoder() throw()
{
    vaDestroyBuffer( _display, _codedBufID );

    vaDestroyContext( _display, _contextID );

    vaDestroySurfaces( _display, &_refSurfaceIDs[0], NUM_REFERENCE_FRAMES );

    vaDestroySurfaces( _display, &_srcSurfaceID, 1 );

    vaDestroyConfig( _display, _configID );

    vaTerminate( _display );

    close( _fd );
}

void vah264_encoder::encode_yuv420p( shared_ptr<av_packet> input,
                                     avkit::encoder_frame_type type )
{
    VAImage image;
    vaDeriveImage( _display, _srcSurfaceID, &image );

    _upload_image( input->map(), image, _frameWidth, _frameHeight );

    _currentFrameType = _compute_current_frame_type( _currentFrameNum,
                                                     _intraPeriod,
                                                     type );

    if( _currentFrameType == FRAME_IDR )
    {
        _numShortTerm = 0;
        _currentFrameNum = 0;
        _currentIDRDisplay = _currentFrameNum;
    }

    VAStatus status = vaBeginPicture( _display, _contextID, _srcSurfaceID );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaBeginPicture (%s).", vaErrorStr(status) ));

    if( _currentFrameType == FRAME_IDR )
    {
        if( _currentFrameNum == 0 )
            _render_sequence();

        _render_picture( false );

        if( !_extraData )
        {
            bitstream seqBS;
            build_packed_seq_buffer( seqBS,
                                     _seqParam,
                                     _h264Profile,
                                     _constraintSetFlag,
                                     _timeBaseNum,
                                     _timeBaseDen,
                                     _frameBitRate );

            bitstream ppsBS;
            build_packed_pic_buffer( ppsBS, _picParam );

            _extraData = make_shared<ck_memory>();

            memcpy( _extraData->extend_data(seqBS.size()).get_ptr(), seqBS.map(), seqBS.size() );
            memcpy( _extraData->extend_data(ppsBS.size()).get_ptr(), ppsBS.map(), ppsBS.size() );
        }
    }
    else _render_picture( false );

    _render_slice();

    status = vaEndPicture( _display, _contextID );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaEndPicture (%s).", vaErrorStr(status) ));

    _update_reference_frames();

    status = vaSyncSurface( _display, _srcSurfaceID );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaSyncSurface (%s).", vaErrorStr(status) ));

    VACodedBufferSegment* bufList = NULL;

    status = vaMapBuffer( _display, _codedBufID, (void **)(&bufList) );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaMapBuffer (%s).", vaErrorStr(status) ));

    VACodedBufferSegment* current = bufList;
    uint32_t accumSize = 0;
    while( current != NULL )
    {
        accumSize += current->size;
        current = (VACodedBufferSegment*)current->next;
    }

    _pkt = make_shared<av_packet>( accumSize );
    uint8_t* dst = _pkt->map();

    while( bufList != NULL )
    {
        memcpy( dst, bufList->buf, bufList->size );
        dst += bufList->size;
        bufList = (VACodedBufferSegment*)bufList->next;
    }

    _pkt->set_data_size( accumSize );

    vaUnmapBuffer( _display, _codedBufID );
}

shared_ptr<av_packet> vah264_encoder::get()
{
    return _pkt;
}

bool vah264_encoder::last_was_key() const
{
    return (_currentFrameType == FRAME_IDR) ? true : (_currentFrameType == FRAME_I) ? true : false;
}

struct codec_options vah264_encoder::get_options() const
{
    return _options;
}

shared_ptr<ck_memory> vah264_encoder::get_extra_data() const
{
    return _extraData;
}

int32_t vah264_encoder::_compute_current_frame_type( uint32_t currentFrameNum,
                                                     int32_t intraPeriod,
                                                     avkit::encoder_frame_type type ) const
{
    if( type == FRAME_TYPE_AUTO_GOP )
    {
        if( (currentFrameNum % intraPeriod) == 0 )
        {
            if( currentFrameNum == 0 )
                return FRAME_IDR;
            else return FRAME_I;
        }

        return FRAME_P;
    }
    else
    {
        if( type == FRAME_TYPE_KEY )
        {
            if( currentFrameNum == 0 )
                return FRAME_IDR;
            else return FRAME_I;
        }
        else return FRAME_P;
    }
}

void vah264_encoder::_update_reference_frames()
{
    _currentCurrPic.flags = VA_PICTURE_H264_SHORT_TERM_REFERENCE;
    _numShortTerm++;

    if( _numShortTerm > NUM_REFERENCE_FRAMES )
        _numShortTerm = NUM_REFERENCE_FRAMES;

    for( int i = _numShortTerm - 1; i > 0; i-- )
        _referenceFrames[i] = _referenceFrames[i-1];

    _referenceFrames[0] = _currentCurrPic;

    _currentFrameNum++;

    if( _currentFrameNum > MAX_FRAME_NUM )
        _currentFrameNum = 0;
}

void vah264_encoder::_update_ref_pic_list()
{
    uint32_t current_poc = _currentCurrPic.TopFieldOrderCnt;

    if( _currentFrameType == FRAME_P )
        memcpy( _refPicListP, _referenceFrames, _numShortTerm * sizeof(VAPictureH264));
}

void vah264_encoder::_render_sequence()
{
    VABufferID seq_param_buf, rc_param_buf, misc_param_tmpbuf, render_id[3];
    VAStatus status;
    VAEncMiscParameterBuffer *misc_param, *misc_param_tmp;
    VAEncMiscParameterRateControl *misc_rate_ctrl;

    _seqParam.level_idc = 41 /*SH_LEVEL_3*/;
    _seqParam.picture_width_in_mbs = _frameWidthMBAligned / 16;
    _seqParam.picture_height_in_mbs = _frameHeightMBAligned / 16;
    _seqParam.bits_per_second = _frameBitRate * 1024 * 8;
    _seqParam.intra_period = _intraPeriod;
    _seqParam.intra_idr_period = _intraPeriod;
    _seqParam.ip_period = 1;

    _seqParam.max_num_ref_frames = NUM_REFERENCE_FRAMES;
    _seqParam.seq_fields.bits.frame_mbs_only_flag = 1;
    _seqParam.time_scale = _timeBaseDen * 2;
    _seqParam.num_units_in_tick = _timeBaseNum;
    _seqParam.seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 = LOG_2_MAX_PIC_ORDER_CNT_LSB - 4;
    _seqParam.seq_fields.bits.log2_max_frame_num_minus4 = LOG_2_MAX_FRAME_NUM - 4;
    _seqParam.seq_fields.bits.frame_mbs_only_flag = 1;
    _seqParam.seq_fields.bits.chroma_format_idc = 1;
    _seqParam.seq_fields.bits.direct_8x8_inference_flag = 1;

    if( _frameWidth != _frameWidthMBAligned || _frameHeight != _frameHeightMBAligned )
    {
        _seqParam.frame_cropping_flag = 1;
        _seqParam.frame_crop_left_offset = 0;
        _seqParam.frame_crop_right_offset = (_frameWidthMBAligned - _frameWidth) / 2;
        _seqParam.frame_crop_top_offset = 0;
        _seqParam.frame_crop_bottom_offset = (_frameHeightMBAligned - _frameHeight) / 2;
    }

    status = vaCreateBuffer( _display,
                             _contextID,
                             VAEncSequenceParameterBufferType,
                             sizeof(_seqParam),
                             1,
                             &_seqParam,
                             &seq_param_buf );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaCreateBuffer (%s).", vaErrorStr(status) ));


    // Rate control...

    status = vaCreateBuffer( _display,
                             _contextID,
                             VAEncMiscParameterBufferType,
                             sizeof(VAEncMiscParameterBuffer) + sizeof(VAEncMiscParameterRateControl),
                             1,
                             NULL,
                             &rc_param_buf );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaCreateBuffer (%s).", vaErrorStr(status) ));

    vaMapBuffer( _display, rc_param_buf,(void **)&misc_param);
    if( !misc_param )
        CK_THROW(( "Unable to vaMapBuffer." ));

    misc_param->type = VAEncMiscParameterTypeRateControl;
    misc_rate_ctrl = (VAEncMiscParameterRateControl *)misc_param->data;

    memset( misc_rate_ctrl, 0, sizeof(*misc_rate_ctrl) );

    misc_rate_ctrl->bits_per_second = _frameBitRate * 1024 * 8;
    misc_rate_ctrl->initial_qp = 26;
    misc_rate_ctrl->min_qp = 1;

    vaUnmapBuffer( _display, rc_param_buf );

    // HRD...

    VABufferID misc_parameter_hrd_buf_id;

    VAEncMiscParameterHRD *misc_hrd_param = NULL;
    status = vaCreateBuffer( _display,
                             _contextID,
                             VAEncMiscParameterBufferType,
                             sizeof(VAEncMiscParameterBuffer) + sizeof(VAEncMiscParameterRateControl),
                             1,
                             NULL,
                             &misc_parameter_hrd_buf_id );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaCreateBuffer (%s).", vaErrorStr(status) ));

    vaMapBuffer( _display,
                 misc_parameter_hrd_buf_id,
                 (void **)&misc_param );
    if( !misc_param )
        CK_THROW(( "Unable to vaMapBuffer." ));

    misc_param->type = VAEncMiscParameterTypeHRD;
    misc_hrd_param = (VAEncMiscParameterHRD *)misc_param->data;

    misc_hrd_param->initial_buffer_fullness = _frameBitRate * 1024 * 4;
    misc_hrd_param->buffer_size = _frameBitRate * 1024 * 8;

    vaUnmapBuffer( _display, misc_parameter_hrd_buf_id );

    // Push buffers to hardware...

    render_id[0] = seq_param_buf;
    render_id[1] = rc_param_buf;
    render_id[2] = misc_parameter_hrd_buf_id;

    // According to documentation, vaRenderPicture recycles the buffers given to it
    // so we apparently do not owe vaDestroyBuffers() for the above buffers.

    status = vaRenderPicture( _display, _contextID, &render_id[0], 3 );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaRenderPicture (%s).", vaErrorStr(status) ));
}

int32_t vah264_encoder::_calc_poc( int32_t picOrderCntLSB )
{
    static int picOrderCntMsbRef = 0, picOrderCntLsbRef = 0;
    int prevPicOrderCntMsb = 0, prevPicOrderCntLsb = 0;
    int picOrderCntMsb = 0, topFieldOrderCnt = 0;

    if( _currentFrameType == FRAME_IDR )
        prevPicOrderCntMsb = prevPicOrderCntLsb = 0;
    else {
        prevPicOrderCntMsb = picOrderCntMsbRef;
        prevPicOrderCntLsb = picOrderCntLsbRef;
    }

    if( (picOrderCntLSB < prevPicOrderCntLsb) &&
        ((prevPicOrderCntLsb - picOrderCntLSB) >= (int)(MAX_PIC_ORDER_CNT_LSB / 2)))
        picOrderCntMsb = prevPicOrderCntMsb + MAX_PIC_ORDER_CNT_LSB;
    else if( (picOrderCntLSB > prevPicOrderCntLsb) &&
             ((picOrderCntLSB - prevPicOrderCntLsb) > (int)(MAX_PIC_ORDER_CNT_LSB / 2)))
        picOrderCntMsb = prevPicOrderCntMsb - MAX_PIC_ORDER_CNT_LSB;
    else
        picOrderCntMsb = prevPicOrderCntMsb;

    topFieldOrderCnt = picOrderCntMsb + picOrderCntLSB;

    picOrderCntMsbRef = picOrderCntMsb;
    picOrderCntLsbRef = picOrderCntLSB;

    return topFieldOrderCnt;
}

void vah264_encoder::_render_picture( bool done )
{
    VABufferID pic_param_buf;

    _picParam.CurrPic.picture_id = _refSurfaceIDs[(_currentFrameNum % NUM_REFERENCE_FRAMES)];
    _picParam.CurrPic.frame_idx = _currentFrameNum;
    _picParam.CurrPic.flags = 0;
    _picParam.CurrPic.TopFieldOrderCnt =
        _calc_poc((_currentFrameNum - _currentIDRDisplay) % MAX_PIC_ORDER_CNT_LSB);
    _picParam.CurrPic.BottomFieldOrderCnt = _picParam.CurrPic.TopFieldOrderCnt;

    _currentCurrPic = _picParam.CurrPic;

    memcpy( _picParam.ReferenceFrames,
            _referenceFrames,
            _numShortTerm*sizeof(VAPictureH264) );

    for (int i = _numShortTerm; i < NUM_REFERENCE_FRAMES; i++)
    {
        _picParam.ReferenceFrames[i].picture_id = VA_INVALID_SURFACE;
        _picParam.ReferenceFrames[i].flags = VA_PICTURE_H264_INVALID;
    }

    _picParam.pic_fields.bits.idr_pic_flag = (_currentFrameType == FRAME_IDR);
    _picParam.pic_fields.bits.reference_pic_flag = 1;
    _picParam.pic_fields.bits.entropy_coding_mode_flag = _h264EntropyMode;
    _picParam.pic_fields.bits.deblocking_filter_control_present_flag = 1;
    _picParam.frame_num = _currentFrameNum;
    _picParam.coded_buf = _codedBufID;
    _picParam.last_picture = (done)?1:0;
    _picParam.pic_init_qp = 26;

    VAStatus status = vaCreateBuffer( _display,
                                      _contextID,
                                      VAEncPictureParameterBufferType,
                                      sizeof(_picParam),
                                      1,
                                      &_picParam,
                                      &pic_param_buf );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaCreateBuffer (%s).", vaErrorStr(status) ));

    status = vaRenderPicture( _display,
                              _contextID,
                              &pic_param_buf,
                              1 );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaRenderPicture (%s).", vaErrorStr(status) ));
}

void vah264_encoder::_render_slice()
{
    VABufferID slice_param_buf;

    _update_ref_pic_list();

    /* one frame, one slice */
    _sliceParam.macroblock_address = 0;
    _sliceParam.num_macroblocks = _frameWidthMBAligned * _frameHeightMBAligned/(16*16); /*Measured by MB*/
    _sliceParam.slice_type = (_currentFrameType == FRAME_IDR)?2:_currentFrameType;
    _sliceParam.slice_qp_delta = 0;

    if( _currentFrameType == FRAME_IDR )
    {
        if( _currentFrameNum != 0 )
            ++_sliceParam.idr_pic_id;
    }
    else if( _currentFrameType == FRAME_P )
    {
        int refpiclist0_max = H264_MAXREF & 0xffff;
        memcpy( _sliceParam.RefPicList0,
                _refPicListP,
                refpiclist0_max*sizeof(VAPictureH264) );

        for (int i = refpiclist0_max; i < 32; i++)
        {
            _sliceParam.RefPicList0[i].picture_id = VA_INVALID_SURFACE;
            _sliceParam.RefPicList0[i].flags = VA_PICTURE_H264_INVALID;
        }
    }

    _sliceParam.slice_alpha_c0_offset_div2 = 0;
    _sliceParam.slice_beta_offset_div2 = 0;
    _sliceParam.direct_spatial_mv_pred_flag = 1;
    _sliceParam.pic_order_cnt_lsb = (_currentFrameNum - _currentIDRDisplay) % MAX_PIC_ORDER_CNT_LSB;

    VAStatus status = vaCreateBuffer( _display,
                                      _contextID,
                                      VAEncSliceParameterBufferType,
                                      sizeof(_sliceParam),
                                      1,
                                      &_sliceParam,
                                      &slice_param_buf );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaCreateBuffer (%s).", vaErrorStr(status) ));

    status = vaRenderPicture( _display,
                              _contextID,
                              &slice_param_buf,
                              1 );
    if( status != VA_STATUS_SUCCESS )
        CK_THROW(( "Unable to vaCreateBuffer (%s).", vaErrorStr(status) ));
}

void vah264_encoder::_upload_image( uint8_t* yv12, VAImage& image, uint16_t width, uint16_t height )
{
    assert( image.num_planes == 2 );
    assert( image.pitches[0] == width );
    assert( image.pitches[1] == width );

    unsigned char* p = NULL;
    vaMapBuffer( _display, image.buf, (void **)&p );
    if( !p )
        CK_THROW(( "Unable to vaMapBuffer." ));

    uint8_t* sy = yv12;
    uint8_t* su = sy + (width * height);
    uint8_t* sv = su + ((width/2)*(height/2));

    uint8_t* dy = p + image.offsets[0];
    uint8_t* duv = p + image.offsets[1];

    memcpy( dy, sy, (width*height) );

    for( size_t i = 0; i < ((width/2)*(height/2)); i++ )
    {
        *duv = *su;
        duv++;
        su++;

        *duv = *sv;
        duv++;
        sv++;
    }

    vaUnmapBuffer( _display, image.buf );
}
