
#include "vakit/nal_types.h"
#include <assert.h>

namespace vakit
{

static const int NAL_REF_IDC_NONE = 0;
static const int NAL_REF_IDC_HIGH = 3;
static const int NAL_SPS = 7;
static const int NAL_PPS = 8;
static const int PROFILE_IDC_BASELINE = 66;
static const int PROFILE_IDC_MAIN = 77;
static const int PROFILE_IDC_HIGH = 100;

void rbsp_trailing_bits( bitstream& bs )
{
    bs.put_ui( 1, 1 );
    bs.byte_aligning( 0 );
}

void nal_start_code_prefix( bitstream& bs )
{
    bs.put_ui( 0x00000001, 32 );
}

void nal_header( bitstream& bs, int32_t nalRefIDC, int32_t nalUnitType )
{
    bs.put_ui( 0, 1 );
    bs.put_ui( nalRefIDC, 2 );
    bs.put_ui( nalUnitType, 5 );
}

void sps_rbsp( bitstream& bs,
               VAEncSequenceParameterBufferH264& sps,
               const VAProfile& h264Profile,
               int constraintSetFlag,
               uint32_t numUnitsInTick,
               uint32_t timeScale,
               uint32_t frameBitrate )
{
    int profileIDC = PROFILE_IDC_BASELINE;

    if (h264Profile  == VAProfileH264High)
        profileIDC = PROFILE_IDC_HIGH;
    else if (h264Profile  == VAProfileH264Main)
        profileIDC = PROFILE_IDC_MAIN;

    bs.put_ui(profileIDC, 8);               /* profile_idc */
    bs.put_ui(!!(constraintSetFlag & 1), 1);                         /* constraint_set0_flag */
    bs.put_ui(!!(constraintSetFlag & 2), 1);                         /* constraint_set1_flag */
    bs.put_ui(!!(constraintSetFlag & 4), 1);                         /* constraint_set2_flag */
    bs.put_ui(!!(constraintSetFlag & 8), 1);                         /* constraint_set3_flag */
    bs.put_ui(0, 4);                         /* reserved_zero_4bits */
    bs.put_ui(sps.level_idc, 8);      /* level_idc */
    bs.put_ue(sps.seq_parameter_set_id);      /* seq_parameter_set_id */

    if ( profileIDC == PROFILE_IDC_HIGH) {
        bs.put_ue(1);        /* chroma_format_idc = 1, 4:2:0 */
        bs.put_ue(0);        /* bit_depth_luma_minus8 */
        bs.put_ue(0);        /* bit_depth_chroma_minus8 */
        bs.put_ui(0, 1);     /* qpprime_y_zero_transform_bypass_flag */
        bs.put_ui(0, 1);     /* seq_scaling_matrix_present_flag */
    }

    bs.put_ue(sps.seq_fields.bits.log2_max_frame_num_minus4); /* log2_max_frame_num_minus4 */
    bs.put_ue(sps.seq_fields.bits.pic_order_cnt_type);        /* pic_order_cnt_type */

    if (sps.seq_fields.bits.pic_order_cnt_type == 0)
        /* log2_max_pic_order_cnt_lsb_minus4 */
        bs.put_ue(sps.seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4);
    else {
        assert(0);
    }

    bs.put_ue(sps.max_num_ref_frames);        /* num_ref_frames */
    bs.put_ui(0, 1);                                 /* gaps_in_frame_num_value_allowed_flag */

    bs.put_ue(sps.picture_width_in_mbs - 1);  /* pic_width_in_mbs_minus1 */
    bs.put_ue(sps.picture_height_in_mbs - 1); /* pic_height_in_map_units_minus1 */
    bs.put_ui(sps.seq_fields.bits.frame_mbs_only_flag, 1);    /* frame_mbs_only_flag */

    if (!sps.seq_fields.bits.frame_mbs_only_flag) {
        assert(0);
    }

    /* direct_8x8_inference_flag */
    bs.put_ui(sps.seq_fields.bits.direct_8x8_inference_flag, 1);
    bs.put_ui(sps.frame_cropping_flag, 1);            /* frame_cropping_flag */

    if (sps.frame_cropping_flag) {
        bs.put_ue(sps.frame_crop_left_offset);        /* frame_crop_left_offset */
        bs.put_ue(sps.frame_crop_right_offset);       /* frame_crop_right_offset */
        bs.put_ue(sps.frame_crop_top_offset);         /* frame_crop_top_offset */
        bs.put_ue(sps.frame_crop_bottom_offset);      /* frame_crop_bottom_offset */
    }

    //if ( frame_bit_rate < 0 ) { //TODO EW: the vui header isn't correct
    if ( 0 ) {
        bs.put_ui(0, 1); /* vui_parameters_present_flag */
    } else {
        bs.put_ui(1, 1); /* vui_parameters_present_flag */
        bs.put_ui(0, 1); /* aspect_ratio_info_present_flag */
        bs.put_ui(0, 1); /* overscan_info_present_flag */
        bs.put_ui(0, 1); /* video_signal_type_present_flag */
        bs.put_ui(0, 1); /* chroma_loc_info_present_flag */
        bs.put_ui(1, 1); /* timing_info_present_flag */
        {
            bs.put_ui(numUnitsInTick, 32);
            bs.put_ui(timeScale * 2, 32);
            bs.put_ui(1, 1);
        }
        bs.put_ui(1, 1); /* nal_hrd_parameters_present_flag */
        {
            // hrd_parameters
            bs.put_ue(0);    /* cpb_cnt_minus1 */
            bs.put_ui(0, 4); /* bit_rate_scale */
            bs.put_ui(0, 4); /* cpb_size_scale */

            bs.put_ue(frameBitrate - 1); /* bit_rate_value_minus1[0] */
            bs.put_ue(frameBitrate*8 - 1); /* cpb_size_value_minus1[0] */
            bs.put_ui(1, 1);  /* cbr_flag[0] */

            bs.put_ui(23, 5);   /* initial_cpb_removal_delay_length_minus1 */
            bs.put_ui(23, 5);   /* cpb_removal_delay_length_minus1 */
            bs.put_ui(23, 5);   /* dpb_output_delay_length_minus1 */
            bs.put_ui(23, 5);   /* time_offset_length  */
        }
        bs.put_ui(0, 1);   /* vcl_hrd_parameters_present_flag */
        bs.put_ui(0, 1);   /* low_delay_hrd_flag */

        bs.put_ui(0, 1); /* pic_struct_present_flag */
        bs.put_ui(0, 1); /* bitstream_restriction_flag */
    }

    rbsp_trailing_bits(bs);     /* rbsp_trailing_bits */
}

void pps_rbsp( bitstream& bs,
               VAEncPictureParameterBufferH264& pps )
{
    bs.put_ue(pps.pic_parameter_set_id);      /* pic_parameter_set_id */
    bs.put_ue(pps.seq_parameter_set_id);      /* seq_parameter_set_id */

    bs.put_ui(pps.pic_fields.bits.entropy_coding_mode_flag, 1);  /* entropy_coding_mode_flag */

    bs.put_ui(0, 1);                         /* pic_order_present_flag: 0 */

    bs.put_ue(0);                            /* num_slice_groups_minus1 */

    bs.put_ue(pps.num_ref_idx_l0_active_minus1);      /* num_ref_idx_l0_active_minus1 */
    bs.put_ue(pps.num_ref_idx_l1_active_minus1);      /* num_ref_idx_l1_active_minus1 1 */

    bs.put_ui(pps.pic_fields.bits.weighted_pred_flag, 1);     /* weighted_pred_flag: 0 */
    bs.put_ui(pps.pic_fields.bits.weighted_bipred_idc, 2);	/* weighted_bipred_idc: 0 */

    bs.put_se(pps.pic_init_qp - 26);  /* pic_init_qp_minus26 */
    bs.put_se(0);                            /* pic_init_qs_minus26 */
    bs.put_se(0);                            /* chroma_qp_index_offset */

    bs.put_ui(pps.pic_fields.bits.deblocking_filter_control_present_flag, 1); /* deblocking_filter_control_present_flag */
    bs.put_ui(0, 1);                         /* constrained_intra_pred_flag */
    bs.put_ui(0, 1);                         /* redundant_pic_cnt_present_flag */

    /* more_rbsp_data */
    bs.put_ui(pps.pic_fields.bits.transform_8x8_mode_flag, 1);    /*transform_8x8_mode_flag */
    bs.put_ui(0, 1);                         /* pic_scaling_matrix_present_flag */
    bs.put_se(pps.second_chroma_qp_index_offset );    /*second_chroma_qp_index_offset */

    rbsp_trailing_bits(bs);
}

int build_packed_pic_buffer( bitstream& bs,
                             VAEncPictureParameterBufferH264& pps,
                             bool annexB )
{
    if( annexB )
        nal_start_code_prefix( bs );

    nal_header( bs, NAL_REF_IDC_HIGH, NAL_PPS );

    pps_rbsp( bs, pps );

    bs.end();

    return bs.size_in_bits();
}

int build_packed_seq_buffer( bitstream& bs,
                             VAEncSequenceParameterBufferH264& sps,
                             const VAProfile& h264Profile,
                             int32_t constraintSetFlag,
                             uint32_t numUnitsInTick,
                             uint32_t timeScale,
                             uint32_t frameBitrate,
                             bool annexB )

{
    if( annexB )
        nal_start_code_prefix( bs );

    nal_header( bs, NAL_REF_IDC_HIGH, NAL_SPS );

    sps_rbsp( bs,
              sps,
              h264Profile,
              constraintSetFlag,
              numUnitsInTick,
              timeScale,
              frameBitrate );

    bs.end();

    return bs.size_in_bits();
}

}
