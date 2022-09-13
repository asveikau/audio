
LIBOGG_ROOT:=$(LIBAUDIO_ROOT)third_party/libogg/
LIBOGG_CFLAGS += \
   -I$(LIBOGG_ROOT)include
LIBOGG_SRC:=\
   $(LIBOGG_ROOT)src/bitwise.c \
   $(LIBOGG_ROOT)src/framing.c

$(LIBOGG_ROOT)include/ogg/config_types.h:
	(echo '#include <stdint.h>'; \
	echo typedef int16_t ogg_int16_t\;; \
	echo typedef uint16_t ogg_uint16_t\;; \
	echo typedef int32_t ogg_int32_t\;; \
	echo typedef uint32_t ogg_uint32_t\;; \
	echo typedef int64_t ogg_int64_t\;; \
        echo typedef uint64_t ogg_uint64_t\;) \
	> "$@"

GENERATED_HEADERS += \
	$(LIBOGG_ROOT)include/ogg/config_types.h

LIBOGG_DESCR:=libogg
LIB_DESCRS+=LIBOGG_DESCR

LIBAUDIO_SUBLIBS+=LIBOGG_SRC

OPENCORE_AUDIO_ROOT:=$(LIBAUDIO_ROOT)third_party/opencore-audio/

OPENCORE_AUDIO_CFLAGS += \
   -DC_EQUIVALENT \
   -DOSCL_DISABLE_INLINES \
   -DNDEBUG \
   -I$(OPENCORE_AUDIO_ROOT)oscl/include

OPENCORE_AUDIO_CXXFLAGS += $(OPENCORE_AUDIO_CFLAGS)

OPENCORE_MP3_SRC += \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_alias_reduction.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_crc.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_dct_16.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_dct_6.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_dct_9.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_decode_header.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_decode_huff_cw.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_decoder.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_dequantize_sample.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_equalizer.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_framedecoder.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_get_main_data_size.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_get_scale_factors.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_get_side_info.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_getbits.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_huffman_decoding.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_huffman_parsing.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_imdct_synth.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_mdct_18.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_mdct_6.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_mpeg2_get_scale_data.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_mpeg2_get_scale_factors.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_mpeg2_stereo_proc.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_normalize.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_poly_phase_synthesis.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_polyphase_filter_window.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_reorder.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_seek_synch.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_stereo_proc.cpp \
   $(OPENCORE_AUDIO_ROOT)mp3/dec/src/pvmp3_tables.cpp

ifdef USE_OPENCORE_MP3
OPENCORE_MP3_DESCR:=OpenCore MP3 Decoder
LIB_DESCRS+=OPENCORE_MP3_DESCR
LIBAUDIO_SUBLIBS+=OPENCORE_MP3_SRC
OPENCORE_AUDIO_CFLAGS += \
   -I$(OPENCORE_AUDIO_ROOT)mp3/dec/include
endif

OPENCORE_AMR_SRC_A += \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/a_refl.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/agc.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/amrdecode.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/b_cn_cod.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/bgnscd.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/c_g_aver.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/d_gain_c.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/d_gain_p.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/d_plsf_3.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/d_plsf_5.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/d_plsf.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/d1035pf.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/d2_11pf.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/d2_9pf.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/d3_14pf.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/d4_17pf.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/d8_31pf.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/dec_amr.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/dec_gain.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/dec_input_format_tab.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/dec_lag3.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/dec_lag6.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/decoder_gsm_amr.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/dtx_dec.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/ec_gains.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/ex_ctrl.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/if2_to_ets.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/int_lsf.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/lsp_avg.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/ph_disp.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/post_pro.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/preemph.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/pstfilt.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/qgain475_tab.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/sp_dec.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/src/wmf_to_ets.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/agc2_amr_wb.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/band_pass_6k_7k.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/dec_acelp_2p_in_64.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/dec_acelp_4p_in_64.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/dec_alg_codebook.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/dec_gain2_amr_wb.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/decoder_amr_wb.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/deemphasis_32.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/dtx_decoder_amr_wb.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/get_amr_wb_bits.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/highpass_400hz_at_12k8.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/highpass_50hz_at_12k8.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/homing_amr_wb_dec.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/interpolate_isp.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/isf_extrapolation.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/isp_az.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/isp_isf.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/lagconceal.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/low_pass_filt_7k.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/median5.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/mime_io.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/noise_gen_amrwb.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/normalize_amr_wb.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/oversamp_12k8_to_16k.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/phase_dispersion.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/pit_shrp.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/pred_lt4.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/preemph_amrwb_dec.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/pvamrwb_math_op.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/pvamrwbdecoder.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/q_gain2_tab.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/qisf_ns_tab.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/qisf_ns.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/qpisf_2s_tab.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/qpisf_2s.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/scale_signal.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/synthesis_amr_wb.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/voice_factor.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/wb_syn_filt.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/src/weight_amrwb_lpc.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/add.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/az_lsp.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/bitno_tab.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/bitreorder_tab.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/bits2prm.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/bytesused.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/c2_9pf_tab.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/copy.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/div_32.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/div_s.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/extract_h.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/extract_l.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/gains_tbl.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/gc_pred.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/gmed_n.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/gray_tbl.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/grid_tbl.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/int_lpc.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/inv_sqrt_tbl.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/inv_sqrt.cpp
OPENCORE_AMR_SRC_B += \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/l_abs.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/l_deposit_h.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/l_deposit_l.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/l_shr_r.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/log2_norm.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/log2_tbl.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/log2.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/lsfwt.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/lsp_az.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/lsp_lsf_tbl.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/lsp_lsf.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/lsp_tab.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/lsp.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/mult_r.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/negate.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/norm_l.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/norm_s.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/overflow_tbl.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/ph_disp_tab.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/pow2_tbl.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/pow2.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/pred_lt.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/q_plsf_3_tbl.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/q_plsf_3.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/q_plsf_5_tbl.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/q_plsf_5.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/q_plsf.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/qua_gain_tbl.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/reorder.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/residu.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/round.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/set_zero.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/shr_r.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/shr.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/sqrt_l_tbl.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/sqrt_l.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/sub.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/syn_filt.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/vad1.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/weight_a.cpp \
   $(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/src/window_tab.cpp

ifdef USE_OPENCORE_AMR
OPENCORE_AMR_DESCR:=OpenCore AMR Decoder
LIB_DESCRS+=OPENCORE_AMR_DESCR
LIBAUDIO_SUBLIBS+=OPENCORE_AMR_SRC_A OPENCORE_AMR_SRC_B
OPENCORE_AUDIO_CFLAGS += \
   -I$(OPENCORE_AUDIO_ROOT)gsm_amr/common/dec/include \
   -I$(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/common/include \
   -I$(OPENCORE_AUDIO_ROOT)gsm_amr/amr_nb/dec/include \
   -I$(OPENCORE_AUDIO_ROOT)gsm_amr/amr_wb/dec/include \
   -I${OPENCORE_AUDIO_ROOT}gsm_amr/amr_wb/dec/src 
endif

OPENCORE_AAC_SRC_A += \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/analysis_sub_band.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/apply_ms_synt.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/apply_tns.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/buf_getbits.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/byte_align.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/calc_auto_corr.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/calc_gsfb_table.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/calc_sbr_anafilterbank.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/calc_sbr_envelope.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/calc_sbr_synfilterbank.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/check_crc.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/dct16.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/dct64.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/decode_huff_cw_binary.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/decode_noise_floorlevels.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/decoder_aac.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/deinterleave.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/digit_reversal_tables.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/dst16.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/dst32.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/dst8.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/esc_iquant_scaling.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/extractframeinfo.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/fft_rx4_long.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/fft_rx4_short.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/fft_rx4_tables_fxp.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/find_adts_syncword.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/fwd_long_complex_rot.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/fwd_short_complex_rot.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/gen_rand_vector.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/get_adif_header.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/get_adts_header.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/get_audio_specific_config.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/get_dse.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/get_ele_list.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/get_ga_specific_config.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/get_ics_info.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/get_prog_config.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/get_pulse_data.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/get_sbr_bitstream.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/get_sbr_startfreq.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/get_sbr_stopfreq.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/get_tns.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/getfill.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/getgroup.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/getics.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/getmask.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/hcbtables_binary.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/huffcb.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/huffdecode.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/hufffac.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/huffspec_fxp.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/idct16.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/idct32.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/idct8.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/imdct_fxp.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/infoinit.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/init_sbr_dec.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/intensity_right.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/inv_long_complex_rot.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/inv_short_complex_rot.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/iquant_table.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/long_term_prediction.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/long_term_synthesis.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/lt_decode.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/mdct_fxp.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/mdct_tables_fxp.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/mdst.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/mix_radix_fft.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ms_synt.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pns_corr.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pns_intensity_right.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pns_left.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_all_pass_filter_coeff.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_all_pass_fract_delay_filter.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_allocate_decoder.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_applied.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_bstr_decoding.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_channel_filtering.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_decode_bs_utils.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_decorrelate.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_fft_rx8.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_hybrid_analysis.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_hybrid_filter_bank_allocation.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_hybrid_synthesis.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_init_stereo_mixing.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_pwr_transient_detection.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_read_data.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/ps_stereo_processing.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pulse_nc.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pv_div.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pv_log2.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pv_normalize.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pv_pow2.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pv_sine.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pv_sqrt.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pvmp4audiodecoderconfig.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pvmp4audiodecoderframe.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pvmp4audiodecodergetmemrequirements.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pvmp4audiodecoderinitlibrary.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pvmp4audiodecoderresetbuffer.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/pvmp4setaudioconfig.cpp
OPENCORE_AAC_SRC_B += \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/q_normalize.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/qmf_filterbank_coeff.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_aliasing_reduction.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_applied.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_code_book_envlevel.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_crc_check.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_create_limiter_bands.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_dec.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_decode_envelope.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_decode_huff_cw.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_downsample_lo_res.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_envelope_calc_tbl.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_envelope_unmapping.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_extract_extended_data.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_find_start_andstop_band.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_generate_high_freq.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_get_additional_data.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_get_cpe.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_get_dir_control_data.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_get_envelope.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_get_header_data.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_get_noise_floor_data.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_get_sce.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_inv_filt_levelemphasis.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_open.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_read_data.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_requantize_envelope_data.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_reset_dec.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sbr_update_freq_scale.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/set_mc_info.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/sfb.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/shellsort.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/synthesis_sub_band.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/tns_ar_filter.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/tns_decode_coef.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/tns_inv_filter.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/trans4m_freq_2_time_fxp.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/trans4m_time_2_freq_fxp.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/unpack_idx.cpp \
   $(OPENCORE_AUDIO_ROOT)aac/dec/src/window_tables_fxp.cpp

ifdef USE_OPENCORE_AAC
OPENCORE_AAC_DESCR:=OpenCore AAC Decoder
LIB_DESCRS+=OPENCORE_AAC_DESCR
LIBAUDIO_SUBLIBS+=OPENCORE_AAC_SRC_A OPENCORE_AAC_SRC_B
OPENCORE_AUDIO_CFLAGS += \
   -I$(OPENCORE_AUDIO_ROOT)aac/dec/include
ifneq (0, $(shell echo $(CXX) | grep -c g++))
OPENCORE_AUDIO_CFLAGS += \
   -Wno-narrowing
endif
#   -DAAC_PLUS -DPARAMETRICSTEREO -DHQ_SBR
endif

LIBOPUS_ROOT:=$(LIBAUDIO_ROOT)third_party/libopus/
LIBOPUS_CFLAGS+= \
   -I$(LIBOPUS_ROOT)include \
   -I$(LIBOPUS_ROOT)silk \
   -I$(LIBOPUS_ROOT)celt \
   -DOPUS_BUILD
ifeq ($(PLATFORM), windows)
LIBOPUS_CFLAGS += -DUSE_ALLOCA
else
LIBOPUS_CFLAGS += -DVAR_ARRAYS
LIBOPUS_CFLAGS += -DHAVE_LRINT
endif
LIBOPUS_BASE_SRC=\
   $(LIBOPUS_ROOT)src/analysis.c \
   $(LIBOPUS_ROOT)src/mlp_data.c \
   $(LIBOPUS_ROOT)src/mlp.c \
   $(LIBOPUS_ROOT)src/opus_decoder.c \
   $(LIBOPUS_ROOT)src/opus_encoder.c \
   $(LIBOPUS_ROOT)src/opus_multistream_decoder.c \
   $(LIBOPUS_ROOT)src/opus_multistream_encoder.c \
   $(LIBOPUS_ROOT)src/opus_multistream.c \
   $(LIBOPUS_ROOT)src/opus.c \
   $(LIBOPUS_ROOT)src/repacketizer.c \
   $(LIBOPUS_ROOT)celt/bands.c \
   $(LIBOPUS_ROOT)celt/celt_decoder.c \
   $(LIBOPUS_ROOT)celt/celt_encoder.c \
   $(LIBOPUS_ROOT)celt/celt_lpc.c \
   $(LIBOPUS_ROOT)celt/celt.c \
   $(LIBOPUS_ROOT)celt/cwrs.c \
   $(LIBOPUS_ROOT)celt/entcode.c \
   $(LIBOPUS_ROOT)celt/entdec.c \
   $(LIBOPUS_ROOT)celt/entenc.c \
   $(LIBOPUS_ROOT)celt/kiss_fft.c \
   $(LIBOPUS_ROOT)celt/laplace.c \
   $(LIBOPUS_ROOT)celt/mathops.c \
   $(LIBOPUS_ROOT)celt/mdct.c \
   $(LIBOPUS_ROOT)celt/modes.c \
   $(LIBOPUS_ROOT)celt/pitch.c \
   $(LIBOPUS_ROOT)celt/quant_bands.c \
   $(LIBOPUS_ROOT)celt/rate.c \
   $(LIBOPUS_ROOT)celt/vq.c \
   $(LIBOPUS_ROOT)silk/A2NLSF.c \
   $(LIBOPUS_ROOT)silk/ana_filt_bank_1.c \
   $(LIBOPUS_ROOT)silk/biquad_alt.c \
   $(LIBOPUS_ROOT)silk/bwexpander_32.c \
   $(LIBOPUS_ROOT)silk/bwexpander.c \
   $(LIBOPUS_ROOT)silk/check_control_input.c \
   $(LIBOPUS_ROOT)silk/CNG.c \
   $(LIBOPUS_ROOT)silk/code_signs.c \
   $(LIBOPUS_ROOT)silk/control_audio_bandwidth.c \
   $(LIBOPUS_ROOT)silk/control_codec.c \
   $(LIBOPUS_ROOT)silk/control_SNR.c \
   $(LIBOPUS_ROOT)silk/dec_API.c \
   $(LIBOPUS_ROOT)silk/decode_core.c \
   $(LIBOPUS_ROOT)silk/decode_frame.c \
   $(LIBOPUS_ROOT)silk/decode_indices.c \
   $(LIBOPUS_ROOT)silk/decode_parameters.c \
   $(LIBOPUS_ROOT)silk/decode_pitch.c \
   $(LIBOPUS_ROOT)silk/decode_pulses.c \
   $(LIBOPUS_ROOT)silk/decoder_set_fs.c \
   $(LIBOPUS_ROOT)silk/enc_API.c \
   $(LIBOPUS_ROOT)silk/encode_indices.c \
   $(LIBOPUS_ROOT)silk/encode_pulses.c \
   $(LIBOPUS_ROOT)silk/gain_quant.c \
   $(LIBOPUS_ROOT)silk/HP_variable_cutoff.c \
   $(LIBOPUS_ROOT)silk/init_decoder.c \
   $(LIBOPUS_ROOT)silk/init_encoder.c \
   $(LIBOPUS_ROOT)silk/inner_prod_aligned.c \
   $(LIBOPUS_ROOT)silk/interpolate.c \
   $(LIBOPUS_ROOT)silk/lin2log.c \
   $(LIBOPUS_ROOT)silk/log2lin.c \
   $(LIBOPUS_ROOT)silk/LP_variable_cutoff.c \
   $(LIBOPUS_ROOT)silk/LPC_analysis_filter.c \
   $(LIBOPUS_ROOT)silk/LPC_fit.c \
   $(LIBOPUS_ROOT)silk/LPC_inv_pred_gain.c \
   $(LIBOPUS_ROOT)silk/NLSF_decode.c \
   $(LIBOPUS_ROOT)silk/NLSF_del_dec_quant.c \
   $(LIBOPUS_ROOT)silk/NLSF_encode.c \
   $(LIBOPUS_ROOT)silk/NLSF_stabilize.c \
   $(LIBOPUS_ROOT)silk/NLSF_unpack.c \
   $(LIBOPUS_ROOT)silk/NLSF_VQ_weights_laroia.c \
   $(LIBOPUS_ROOT)silk/NLSF_VQ.c \
   $(LIBOPUS_ROOT)silk/NLSF2A.c \
   $(LIBOPUS_ROOT)silk/NSQ_del_dec.c \
   $(LIBOPUS_ROOT)silk/NSQ.c \
   $(LIBOPUS_ROOT)silk/pitch_est_tables.c \
   $(LIBOPUS_ROOT)silk/PLC.c \
   $(LIBOPUS_ROOT)silk/process_NLSFs.c \
   $(LIBOPUS_ROOT)silk/quant_LTP_gains.c \
   $(LIBOPUS_ROOT)silk/resampler_down2_3.c \
   $(LIBOPUS_ROOT)silk/resampler_down2.c \
   $(LIBOPUS_ROOT)silk/resampler_private_AR2.c \
   $(LIBOPUS_ROOT)silk/resampler_private_down_FIR.c \
   $(LIBOPUS_ROOT)silk/resampler_private_IIR_FIR.c \
   $(LIBOPUS_ROOT)silk/resampler_private_up2_HQ.c \
   $(LIBOPUS_ROOT)silk/resampler_rom.c \
   $(LIBOPUS_ROOT)silk/resampler.c \
   $(LIBOPUS_ROOT)silk/shell_coder.c \
   $(LIBOPUS_ROOT)silk/sigm_Q15.c \
   $(LIBOPUS_ROOT)silk/sort.c \
   $(LIBOPUS_ROOT)silk/stereo_decode_pred.c \
   $(LIBOPUS_ROOT)silk/stereo_encode_pred.c \
   $(LIBOPUS_ROOT)silk/stereo_find_predictor.c \
   $(LIBOPUS_ROOT)silk/stereo_LR_to_MS.c \
   $(LIBOPUS_ROOT)silk/stereo_MS_to_LR.c \
   $(LIBOPUS_ROOT)silk/stereo_quant_pred.c \
   $(LIBOPUS_ROOT)silk/sum_sqr_shift.c \
   $(LIBOPUS_ROOT)silk/table_LSF_cos.c \
   $(LIBOPUS_ROOT)silk/tables_gain.c \
   $(LIBOPUS_ROOT)silk/tables_LTP.c \
   $(LIBOPUS_ROOT)silk/tables_NLSF_CB_NB_MB.c \
   $(LIBOPUS_ROOT)silk/tables_NLSF_CB_WB.c \
   $(LIBOPUS_ROOT)silk/tables_other.c \
   $(LIBOPUS_ROOT)silk/tables_pitch_lag.c \
   $(LIBOPUS_ROOT)silk/tables_pulses_per_block.c \
   $(LIBOPUS_ROOT)silk/VAD.c \
   $(LIBOPUS_ROOT)silk/VQ_WMat_EC.c

LIBOPUS_SRC += $(LIBOPUS_BASE_SRC)

LIBOPUS_FLOAT_SRC += \
   $(LIBOPUS_ROOT)silk/float/apply_sine_window_FLP.c \
   $(LIBOPUS_ROOT)silk/float/autocorrelation_FLP.c \
   $(LIBOPUS_ROOT)silk/float/burg_modified_FLP.c \
   $(LIBOPUS_ROOT)silk/float/bwexpander_FLP.c \
   $(LIBOPUS_ROOT)silk/float/corrMatrix_FLP.c \
   $(LIBOPUS_ROOT)silk/float/encode_frame_FLP.c \
   $(LIBOPUS_ROOT)silk/float/energy_FLP.c \
   $(LIBOPUS_ROOT)silk/float/find_LPC_FLP.c \
   $(LIBOPUS_ROOT)silk/float/find_LTP_FLP.c \
   $(LIBOPUS_ROOT)silk/float/find_pitch_lags_FLP.c \
   $(LIBOPUS_ROOT)silk/float/find_pred_coefs_FLP.c \
   $(LIBOPUS_ROOT)silk/float/inner_product_FLP.c \
   $(LIBOPUS_ROOT)silk/float/k2a_FLP.c \
   $(LIBOPUS_ROOT)silk/float/LPC_analysis_filter_FLP.c \
   $(LIBOPUS_ROOT)silk/float/LPC_inv_pred_gain_FLP.c \
   $(LIBOPUS_ROOT)silk/float/LTP_analysis_filter_FLP.c \
   $(LIBOPUS_ROOT)silk/float/LTP_scale_ctrl_FLP.c \
   $(LIBOPUS_ROOT)silk/float/noise_shape_analysis_FLP.c \
   $(LIBOPUS_ROOT)silk/float/pitch_analysis_core_FLP.c \
   $(LIBOPUS_ROOT)silk/float/process_gains_FLP.c \
   $(LIBOPUS_ROOT)silk/float/regularize_correlations_FLP.c \
   $(LIBOPUS_ROOT)silk/float/residual_energy_FLP.c \
   $(LIBOPUS_ROOT)silk/float/scale_copy_vector_FLP.c \
   $(LIBOPUS_ROOT)silk/float/scale_vector_FLP.c \
   $(LIBOPUS_ROOT)silk/float/schur_FLP.c \
   $(LIBOPUS_ROOT)silk/float/sort_FLP.c \
   $(LIBOPUS_ROOT)silk/float/warped_autocorrelation_FLP.c \
   $(LIBOPUS_ROOT)silk/float/wrappers_FLP.c

LIBOPUS_FIXED_SRC += \
   $(LIBOPUS_ROOT)silk/fixed/apply_sine_window_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/autocorr_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/burg_modified_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/corrMatrix_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/encode_frame_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/find_LPC_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/find_LTP_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/find_pitch_lags_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/find_pred_coefs_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/k2a_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/k2a_Q16_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/LTP_analysis_filter_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/LTP_scale_ctrl_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/noise_shape_analysis_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/pitch_analysis_core_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/process_gains_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/regularize_correlations_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/residual_energy_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/residual_energy16_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/schur_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/vector_ops_FIX.c \
   $(LIBOPUS_ROOT)silk/fixed/warped_autocorrelation_FIX.c

ifdef USE_FLOAT 
LIBOPUS_CLFAGS += -I$(LIBOPUS_ROOT)silk/float
LIBOPUS_SRC += $(LIBOPUS_FLOAT_SRC)
else
LIBOPUS_CFLAGS += -DFIXED_POINT -I$(LIBOPUS_ROOT)silk/fixed
LIBOPUS_SRC += $(LIBOPUS_FIXED_SRC)
endif

LIBOPUS_DESCR:=Opus Codec
LIB_DESCRS+=LIBOPUS_DESCR
LIBAUDIO_SUBLIBS+=LIBOPUS_SRC

LIBOPUSFILE_ROOT:=$(LIBAUDIO_ROOT)third_party/libopusfile/
LIBOPUSFILE_CFLAGS= \
   -I$(LIBOGG_ROOT)include \
   -I$(LIBOPUS_ROOT)include \
   -I$(LIBOPUSFILE_ROOT)include
LIBOPUSFILE_SRC:=\
   $(LIBOPUSFILE_ROOT)src/info.c \
   $(LIBOPUSFILE_ROOT)src/internal.c \
   $(LIBOPUSFILE_ROOT)src/opusfile.c \
   $(LIBOPUSFILE_ROOT)src/stream.c

LIBOPUSFILE_DESCR:=libopusfile
LIB_DESCRS+=LIBOPUSFILE_DESCR
LIBAUDIO_SUBLIBS+=LIBOPUSFILE_SRC

LIBVORBIS_ROOT:=$(LIBAUDIO_ROOT)third_party/libvorbis/
LIBVORBIS_CFLAGS= \
   -I$(LIBOGG_ROOT)/include \
   -I$(LIBVORBIS_ROOT)include \
   -I$(LIBVORBIS_ROOT)lib
LIBVORBIS_SRC:=\
   $(LIBVORBIS_ROOT)lib/analysis.c \
   $(LIBVORBIS_ROOT)lib/bitrate.c \
   $(LIBVORBIS_ROOT)lib/block.c \
   $(LIBVORBIS_ROOT)lib/codebook.c \
   $(LIBVORBIS_ROOT)lib/envelope.c \
   $(LIBVORBIS_ROOT)lib/floor0.c \
   $(LIBVORBIS_ROOT)lib/floor1.c \
   $(LIBVORBIS_ROOT)lib/info.c \
   $(LIBVORBIS_ROOT)lib/lpc.c \
   $(LIBVORBIS_ROOT)lib/lsp.c \
   $(LIBVORBIS_ROOT)lib/mapping0.c \
   $(LIBVORBIS_ROOT)lib/mdct.c \
   $(LIBVORBIS_ROOT)lib/psy.c \
   $(LIBVORBIS_ROOT)lib/registry.c \
   $(LIBVORBIS_ROOT)lib/res0.c \
   $(LIBVORBIS_ROOT)lib/sharedbook.c \
   $(LIBVORBIS_ROOT)lib/smallft.c \
   $(LIBVORBIS_ROOT)lib/synthesis.c \
   $(LIBVORBIS_ROOT)lib/vorbisenc.c \
   $(LIBVORBIS_ROOT)lib/vorbisfile.c \
   $(LIBVORBIS_ROOT)lib/window.c

LIBVORBIS_DESCR:=Vorbis Codec
LIB_DESCRS+=LIBVORBIS_DESCR
LIBAUDIO_SUBLIBS+=LIBVORBIS_SRC

LIBFLAC_ROOT:=$(LIBAUDIO_ROOT)third_party/libflac/src/libFLAC/
LIBFLAC_VERSION:=$(shell cd $(LIBFLAC_ROOT); git describe --tags)
LIBFLAC_CFLAGS += \
   -DHAVE_LROUND \
   -DHAVE_STDINT_H \
   -DFLAC__HAS_OGG \
   -DFLAC__NO_DLL \
   -I$(LIBOGG_ROOT)/include \
   -I$(LIBFLAC_ROOT)/include \
   -I$(LIBFLAC_ROOT)/../../include
ifeq ($(PLATFORM), windows)
LIBFLAC_CFLAGS += -DPACKAGE_VERSION='\"$(LIBFLAC_VERSION)\"'
else
LIBFLAC_CFLAGS += -DPACKAGE_VERSION='"$(LIBFLAC_VERSION)"'
ifneq ($(PLATFORM), sunos)
LIBFLAC_CFLAGS += -DHAVE_SYS_PARAM_H
endif
endif
LIBFLAC_SRC:= \
   $(LIBFLAC_ROOT)bitmath.c \
   $(LIBFLAC_ROOT)bitreader.c \
   $(LIBFLAC_ROOT)bitwriter.c \
   $(LIBFLAC_ROOT)cpu.c \
   $(LIBFLAC_ROOT)crc.c \
   $(LIBFLAC_ROOT)fixed.c \
   $(LIBFLAC_ROOT)format.c \
   $(LIBFLAC_ROOT)lpc.c \
   $(LIBFLAC_ROOT)md5.c \
   $(LIBFLAC_ROOT)memory.c \
   $(LIBFLAC_ROOT)metadata_iterators.c \
   $(LIBFLAC_ROOT)metadata_object.c \
   $(LIBFLAC_ROOT)ogg_decoder_aspect.c \
   $(LIBFLAC_ROOT)ogg_encoder_aspect.c \
   $(LIBFLAC_ROOT)ogg_helper.c \
   $(LIBFLAC_ROOT)ogg_mapping.c \
   $(LIBFLAC_ROOT)stream_decoder.c \
   $(LIBFLAC_ROOT)stream_encoder_framing.c \
   $(LIBFLAC_ROOT)stream_encoder.c \
   $(LIBFLAC_ROOT)window.c

LIBFLAC_DESCR:=FLAC Codec
LIB_DESCRS+=LIBFLAC_DESCR
LIBAUDIO_SUBLIBS+=LIBFLAC_SRC

LIBKISSFFT_ROOT:=$(LIBAUDIO_ROOT)third_party/kiss_fft/
LIBKISSFFT_CFLAGS:=-I$(LIBKISSFFT_ROOT)
LIBKISSFFT_SRC := \
   $(LIBKISSFFT_ROOT)kiss_fft.c \
   $(LIBKISSFFT_ROOT)tools/kiss_fftr.c

LIBKISSFFT_DESCR:=KISS FFT
LIB_DESCRS+=LIBKISSFFT_DESCR
LIBAUDIO_SUBLIBS+=LIBKISSFFT_SRC

LIBRESAMPLER_ROOT:=$(LIBAUDIO_ROOT)third_party/libspeex-resample/
LIBRESAMPLER_CFLAGS:=-I$(LIBRESAMPLER_ROOT) -DOUTSIDE_SPEEX -DFIXED_POINT -DRANDOM_PREFIX=libaudio -DEXPORT=
ifeq ($(PLATFORM), windows)
LIBRESAMPLER_CFLAGS+=-DUSE_ALLOCA
else
LIBRESAMPLER_CFLAGS+=-DVAR_ARRAYS
endif
LIBRESAMPLER_SRC = \
   $(LIBRESAMPLER_ROOT)resample.c

LIBRESAMPLER_DESCR:=libspeex resampler
LIB_DESCRS+=LIBRESAMPLER_DESCR
LIBAUDIO_SUBLIBS+=LIBRESAMPLER_SRC

LIBALAC_ROOT:=$(LIBAUDIO_ROOT)third_party/libalac/codec/
LIBALAC_CFLAGS:=-DPRAGMA_STRUCT_PACKPUSH=1
LIBALAC_SRC := \
   $(LIBALAC_ROOT)ag_dec.c \
   $(LIBALAC_ROOT)ALACEncoder.cpp  \
   $(LIBALAC_ROOT)matrix_dec.c \
   $(LIBALAC_ROOT)ag_enc.c \
   $(LIBALAC_ROOT)dp_dec.c \
   $(LIBALAC_ROOT)matrix_enc.c \
   $(LIBALAC_ROOT)ALACBitUtilities.c \
   $(LIBALAC_ROOT)dp_enc.c \
   $(LIBALAC_ROOT)ALACDecoder.cpp \
   $(LIBALAC_ROOT)EndianPortable.c

ifdef USE_ALAC
LIBALAC_DESCR:=Apple Lossless Codec
LIB_DESCRS+=LIBALAC_DESCR
LIBAUDIO_SUBLIBS+=LIBALAC_SRC
LIBAUDIO_CFLAGS+=-I$(LIBALAC_ROOT)
endif

LIBAUDIO_CXXFLAGS += \
   ${OPENCORE_AUDIO_CXXFLAGS} ${LIBFLAC_CFLAGS} ${LIBVORBIS_CFLAGS} \
   ${LIBOPUSFILE_CFLAGS}
