/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
--         Copyright (c) 2007-2010, Hantro OY. All rights reserved.           --
--                                                                            --
-- This software is confidential and proprietary and may be used only as      --
--   expressly authorized by VeriSilicon in a written licensing agreement.    --
--                                                                            --
--         This entire notice must be reproduced on all copies                --
--                       and may not be removed.                              --
--                                                                            --
--------------------------------------------------------------------------------
-- Redistribution and use in source and binary forms, with or without         --
-- modification, are permitted provided that the following conditions are met:--
--   * Redistributions of source code must retain the above copyright notice, --
--       this list of conditions and the following disclaimer.                --
--   * Redistributions in binary form must reproduce the above copyright      --
--       notice, this list of conditions and the following disclaimer in the  --
--       documentation and/or other materials provided with the distribution. --
--   * Neither the names of Google nor the names of its contributors may be   --
--       used to endorse or promote products derived from this software       --
--       without specific prior written permission.                           --
--------------------------------------------------------------------------------
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"--
-- AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE  --
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE --
-- ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE  --
-- LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR        --
-- CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF       --
-- SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   --
-- INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN    --
-- CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)    --
-- ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE --
-- POSSIBILITY OF SUCH DAMAGE.                                                --
--------------------------------------------------------------------------------
------------------------------------------------------------------------------*/

#ifndef SOFTWARE_LINUX_DWL_DWL_DEFS_H_
#define SOFTWARE_LINUX_DWL_DWL_DEFS_H_

#define DWL_MPEG2_E      31 /* 1 bit  */
#define DWL_VC1_E        29 /* 2 bits */
#define DWL_JPEG_E       28 /* 1 bit  */
#define DWL_HJPEG_E      17 /* 1 bit  */
#define DWL_AV1_E        16 /* 1 bit  */
#define DWL_MPEG4_E      26 /* 2 bits */
#define DWL_H264_E       24 /* 2 bits */
#define DWL_H264HIGH10_E 20 /* 1 bits */
#define DWL_AVS2_E       18 /* 2 bits */
#define DWL_VP6_E        23 /* 1 bit  */
#define DWL_RV_E         26 /* 2 bits */
#define DWL_VP8_E        23 /* 1 bit  */
#define DWL_VP7_E        24 /* 1 bit  */
#define DWL_WEBP_E       19 /* 1 bit  */
#define DWL_AVS_E        22 /* 1 bit  */
#define DWL_G1_PP_E      16 /* 1 bit  */
#define DWL_G2_PP_E      31 /* 1 bit  */
#define DWL_PP_E         31 /* 1 bit  */
#define DWL_HEVC_E       26 /* 3 bits */
#define DWL_VP9_E        29 /* 3 bits */

#define DWL_H264_PIPELINE_E 31 /* 1 bit */
#define DWL_JPEG_PIPELINE_E 30 /* 1 bit */

#define DWL_G2_HEVC_E    0  /* 1 bits */
#define DWL_G2_VP9_E     1  /* 1 bits */
#define DWL_G2_RFC_E        2  /* 1 bits */
#define DWL_RFC_E        17  /* 2 bits */
#define DWL_G2_DS_E         3  /* 1 bits */
#define DWL_DS_E         28  /* 3 bits */
#define DWL_HEVC_VER     8  /* 4 bits */
#define DWL_VP9_PROFILE  12 /* 3 bits */
#define DWL_RING_E       16 /* 1 bits */

#define HANTRODEC_IRQ_STAT_DEC       1
#define HANTRODEC_IRQ_STAT_DEC_OFF   (HANTRODEC_IRQ_STAT_DEC * 4)
#define BIGOCEAN_IRQ_STAT_DEC       2
#define BIGOCEAN_IRQ_STAT_DEC_OFF   (HANTRODEC_IRQ_STAT_DEC * 4)

#define HANTRODECPP_SYNTH_CFG        60
#define HANTRODECPP_SYNTH_CFG_OFF    (HANTRODECPP_SYNTH_CFG * 4)
#define HANTRODEC_SYNTH_CFG          50
#define HANTRODEC_SYNTH_CFG_OFF      (HANTRODEC_SYNTH_CFG * 4)
#define HANTRODEC_SYNTH_CFG_2        54
#define HANTRODEC_SYNTH_CFG_2_OFF    (HANTRODEC_SYNTH_CFG_2 * 4)
#define HANTRODEC_SYNTH_CFG_3        56
#define HANTRODEC_SYNTH_CFG_3_OFF    (HANTRODEC_SYNTH_CFG_3 * 4)
#define HANTRODEC_CFG_STAT           23
#define HANTRODEC_CFG_STAT_OFF       (HANTRODEC_CFG_STAT * 4)
#define HANTRODECPP_CFG_STAT         260
#define HANTRODECPP_CFG_STAT_OFF     (HANTRODECPP_CFG_STAT * 4)


#define HANTRODEC_DEC_E              0x01
#define HANTRODEC_PP_E               0x01
#define HANTRODEC_DEC_ABORT          0x20
#define HANTRODEC_DEC_IRQ_DISABLE    0x10
#define HANTRODEC_DEC_IRQ            0x100

/* Legacy from G1 */
#define HANTRO_IRQ_STAT_DEC          1
#define HANTRO_IRQ_STAT_DEC_OFF      (HANTRO_IRQ_STAT_DEC * 4)
#define HANTRO_IRQ_STAT_PP           60
#define HANTRO_IRQ_STAT_PP_OFF       (HANTRO_IRQ_STAT_PP * 4)

#define HANTROPP_SYNTH_CFG           100
#define HANTROPP_SYNTH_CFG_OFF       (HANTROPP_SYNTH_CFG * 4)
#define HANTRODEC_SYNTH_CFG          50
#define HANTRODEC_SYNTH_CFG_OFF      (HANTRODEC_SYNTH_CFG * 4)
#define HANTRODEC_SYNTH_CFG_2        54
#define HANTRODEC_SYNTH_CFG_2_OFF    (HANTRODEC_SYNTH_CFG_2 * 4)

/* VC8000D HW build id */
#define HANTRODEC_HW_BUILD_ID        309
#define HANTRODEC_HW_BUILD_ID_OFF    (HANTRODEC_HW_BUILD_ID * 4)

#define HANTRO_DEC_E                 0x01
#define HANTRO_PP_E                  0x01
#define HANTRO_DEC_ABORT             0x20
#define HANTRO_DEC_IRQ_DISABLE       0x10
#define HANTRO_PP_IRQ_DISABLE        0x10
#define HANTRO_DEC_IRQ               0x100
#define HANTRO_PP_IRQ                0x100

#endif /* SOFTWARE_LINUX_DWL_DWL_DEFS_H_ */
