/*
 * Copyright (c) 2006-2017 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef IWCH_ABI_H
#define IWCH_ABI_H

#include <infiniband/kern-abi.h>

struct c4iw_alloc_ucontext_resp {
	struct ibv_get_context_resp ibv_resp;
	__u64 status_page_key;
	__u32 status_page_size;
};

struct c4iw_alloc_pd_resp {
	struct ibv_alloc_pd_resp ibv_resp;
	uint32_t pdid;
};

struct c4iw_create_cq_req {
	struct ibv_create_cq ibv_req;
	__u64 cqe_size;
};

struct c4iw_create_cq_resp {
	struct ibv_create_cq_resp ibv_resp;
	__u64 key;
	__u64 gts_key;
	__u64 memsize;
	__u32 cqid;
	__u32 size;
	__u32 qid_mask;
};

enum {
	C4IW_QPF_ONCHIP 	= (1<<0),
	C4IW_QPF_WRITE_W_IMM 	= (1<<1),
};

struct c4iw_create_qp_resp_v0 {
	struct ibv_create_qp_resp ibv_resp;
	__u64 sq_key;
	__u64 rq_key;
	__u64 sq_db_gts_key;
	__u64 rq_db_gts_key;
	__u64 sq_memsize;
	__u64 rq_memsize;
	__u32 sqid;
	__u32 rqid;
	__u32 sq_size;
	__u32 rq_size;
	__u32 qid_mask;
};

struct c4iw_create_qp_resp {
	struct ibv_create_qp_resp ibv_resp;
	__u64 ma_sync_key;
	__u64 sq_key;
	__u64 rq_key;
	__u64 sq_db_gts_key;
	__u64 rq_db_gts_key;
	__u64 sq_memsize;
	__u64 rq_memsize;
	__u32 sqid;
	__u32 rqid;
	__u32 sq_size;
	__u32 rq_size;
	__u32 qid_mask;
	__u32 flags;
};

enum {
	FL_PACKED_MODE = 1,
	FL_CONG_DROP_MODE = 2
};

struct c4iw_create_raw_qp_req {
	struct ibv_create_qp ibv_req;
	__u32 port;
	__u32 vlan_pri;
	__u32 nfids;
	__u32 flags;
};

struct c4iw_create_raw_qp_resp {
	struct ibv_create_qp_resp ibv_resp;
	union {
		__u64 ma_sync_key;
		__u64 txq_bar2_key;
	};
	__u64 db_key;
	__u64 txq_key;
	__u64 fl_key;
	__u64 iq_key;
	__u64 txq_memsize;
	__u64 fl_memsize;
	__u64 iq_memsize;
	__u32 txq_id;
	__u32 fl_id;
	__u32 iq_id;
	__u32 txq_size;
	__u32 fl_size;
	__u32 iq_size;
	__u32 tx_chan;
	__u32 pf;
	__u32 flags;
	__u32 fid;
	__u64 fl_bar2_key;
	__u64 iq_bar2_key;
};

struct c4iw_reg_mr_req {
	struct ibv_reg_mr ibv_cmd;
	__u64 pbl_ptr;
};

struct c4iw_reg_mr_resp {
	struct ibv_reg_mr_resp ibv_resp;
	__u32 page_size;
};

struct c4iw_create_raw_srq_req {
	struct ibv_create_srq ibv_req;
	__u32 port;
	__u32 flags;
};

struct c4iw_create_raw_srq_resp {
	struct ibv_create_srq_resp ibv_resp;
	__u64 db_key;
	__u64 fl_key;
	__u64 iq_key;
	__u64 fl_memsize;
	__u64 iq_memsize;
	__u32 fl_id;
	__u32 iq_id;
	__u32 fl_size;
	__u32 iq_size;
	__u32 flags;
	__u32 qid_mask;
	__u64 fl_bar2_key;
	__u64 iq_bar2_key;
};

struct c4iw_create_srq_resp {
	struct ibv_create_srq_resp ibv_resp;
	__u64 srq_key;
	__u64 srq_db_gts_key;
	__u64 srq_memsize;
	__u32 srqid;
	__u32 srq_size;
	__u32 rqt_abs_idx;
	__u32 qid_mask;
	__u32 flags;
};

enum {
	T4_SRQ_LIMIT_SUPPORT = (1<<0), /* HW supports SRQ_LIMIT_REACHED event */
};

#endif				/* IWCH_ABI_H */
