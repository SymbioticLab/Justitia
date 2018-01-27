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
#ifndef __T4_H__
#define __T4_H__

#include <stdint.h>
#include <assert.h>
#include <syslog.h>

/*
 * Try and minimize the changes from the kernel code that is pull in
 * here for kernel bypass ops.
 */
#define __u8 uint8_t
#define u8 uint8_t
#define __u16 uint16_t
#define __be16 uint16_t
#define u16 uint16_t
#define __u32 uint32_t
#define __be32 uint32_t
#define u32 uint32_t
#define __u64 uint64_t
#define __be64 uint64_t
#define u64 uint64_t
#define DECLARE_PCI_UNMAP_ADDR(a)
#define __iomem
#define cpu_to_be16 htons
#define cpu_to_be32 htonl
#define cpu_to_be64 htonll
#define be16_to_cpu ntohs
#define be32_to_cpu ntohl
#define be64_to_cpu ntohll
#define BUG_ON(c) assert(!(c))
#define unlikely
#define ROUND_UP(x, n) (((x) + (n) - 1u) & ~((n) - 1u))
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#if __BYTE_ORDER == __LITTLE_ENDIAN
#  define cpu_to_pci32(val) ((val))
#elif __BYTE_ORDER == __BIG_ENDIAN
#  define cpu_to_pci32(val) (__bswap_32((val)))
#else
#  error __BYTE_ORDER not defined
#endif

#define writel(v, a) do { *((volatile u32 *)(a)) = cpu_to_pci32(v); } while (0)

#include <arpa/inet.h> 			/* For htonl() and friends */
#include "t4_regs.h"
#include "t4_chip_type.h"
#include "t4fw_interface.h"
#include "t4_msg.h"

#ifdef DEBUG
#define DBGLOG(s)
#define PDBG(fmt, args...) do {syslog(LOG_DEBUG, fmt, ##args); } while (0)
#else
#define DBGLOG(s)
#define PDBG(fmt, args...) do {} while (0)
#endif

#define T4_MAX_READ_DEPTH 16
#define T4_QID_BASE 1024
#define T4_MAX_QIDS 256
#define T4_MAX_NUM_PD 65536
#define T4_EQ_STATUS_ENTRIES (L1_CACHE_BYTES > 64 ? 2 : 1)
#define T4_MAX_EQ_SIZE (65520 - T4_EQ_STATUS_ENTRIES)
#define T4_MAX_IQ_SIZE (65520 - 1)
#define T4_MAX_RQ_SIZE (8192 - T4_EQ_STATUS_ENTRIES)
#define T4_MAX_SQ_SIZE (T4_MAX_EQ_SIZE - 1)
#define T4_MAX_QP_DEPTH (T4_MAX_RQ_SIZE - 1)
#define T4_MAX_CQ_DEPTH (T4_MAX_IQ_SIZE - 1)
#define T4_MAX_NUM_STAG (1<<15)
#define T4_MAX_MR_SIZE (~0ULL - 1)
#define T4_PAGESIZE_MASK 0xffff000  /* 4KB-128MB */
#define T4_STAG_UNSET 0xffffffff
#define T4_FW_MAJ 0

struct t4_status_page {
	__be32 rsvd1;	/* flit 0 - hw owns */
	__be16 rsvd2;
	__be16 qid;
	__be16 cidx;
	__be16 pidx;
	u8 qp_err;	/* flit 1 - sw owns */
	u8 db_off;
	u8 cq_armed;
	u8 pad;
	u16 host_wq_pidx;
	u16 host_cidx;
	u16 host_pidx;
	u16 pad2;
	u32 srqidx;
};

#define T4_EQ_ENTRY_SIZE 64

#define T4_SQ_NUM_SLOTS 5
#define T4_SQ_NUM_BYTES (T4_EQ_ENTRY_SIZE * T4_SQ_NUM_SLOTS)
#define T4_MAX_SEND_SGE ((T4_SQ_NUM_BYTES - sizeof(struct fw_ri_send_wr) - sizeof(struct fw_ri_isgl)) / sizeof (struct fw_ri_sge))
#define T4_MAX_SEND_INLINE ((T4_SQ_NUM_BYTES - sizeof(struct fw_ri_send_wr) - sizeof(struct fw_ri_immd)))
#define T4_MAX_WRITE_INLINE ((T4_SQ_NUM_BYTES - sizeof(struct fw_ri_rdma_write_wr) - sizeof(struct fw_ri_immd)))
#define T4_MAX_WRITE_SGE ((T4_SQ_NUM_BYTES - sizeof(struct fw_ri_rdma_write_wr) - sizeof(struct fw_ri_isgl)) / sizeof (struct fw_ri_sge))
#define T4_MAX_FR_IMMD ((T4_SQ_NUM_BYTES - sizeof(struct fw_ri_fr_nsmr_wr) - sizeof(struct fw_ri_immd)))
#define T4_MAX_FR_DEPTH 255

#define T4_RQ_NUM_SLOTS 2
#define T4_RQ_NUM_BYTES (T4_EQ_ENTRY_SIZE * T4_RQ_NUM_SLOTS)
#define T4_MAX_RECV_SGE 4

union t4_wr {
	struct fw_ri_res_wr res;
	struct fw_ri_wr init;
	struct fw_ri_rdma_write_wr write;
	struct fw_ri_send_wr send;
	struct fw_ri_rdma_read_wr read;
	struct fw_ri_bind_mw_wr bind;
	struct fw_ri_fr_nsmr_wr fr;
	struct fw_ri_inv_lstag_wr inv;
	struct t4_status_page status;
	__be64 flits[T4_EQ_ENTRY_SIZE / sizeof(__be64) * T4_SQ_NUM_SLOTS];
} __attribute__((aligned(T4_EQ_ENTRY_SIZE)));

union t4_recv_wr {
	struct fw_ri_recv_wr recv;
	struct t4_status_page status;
	__be64 flits[T4_EQ_ENTRY_SIZE / sizeof(__be64) * T4_RQ_NUM_SLOTS];
};

static inline void init_wr_hdr(union t4_wr *wqe, u16 wrid,
			       enum fw_wr_opcodes opcode, u8 flags, u8 len16)
{
	wqe->send.opcode = (u8)opcode;
	wqe->send.flags = flags;
	wqe->send.wrid = wrid;
	wqe->send.r1[0] = 0;
	wqe->send.r1[1] = 0;
	wqe->send.r1[2] = 0;
	wqe->send.len16 = len16;
}

/* CQE/AE status codes */
#define T4_ERR_SUCCESS                     0x0
#define T4_ERR_STAG                        0x1	/* STAG invalid: either the */
						/* STAG is offlimt, being 0, */
						/* or STAG_key mismatch */
#define T4_ERR_PDID                        0x2	/* PDID mismatch */
#define T4_ERR_QPID                        0x3	/* QPID mismatch */
#define T4_ERR_ACCESS                      0x4	/* Invalid access right */
#define T4_ERR_WRAP                        0x5	/* Wrap error */
#define T4_ERR_BOUND                       0x6	/* base and bounds voilation */
#define T4_ERR_INVALIDATE_SHARED_MR        0x7	/* attempt to invalidate a  */
						/* shared memory region */
#define T4_ERR_INVALIDATE_MR_WITH_MW_BOUND 0x8	/* attempt to invalidate a  */
						/* shared memory region */
#define T4_ERR_ECC                         0x9	/* ECC error detected */
#define T4_ERR_ECC_PSTAG                   0xA	/* ECC error detected when  */
						/* reading PSTAG for a MW  */
						/* Invalidate */
#define T4_ERR_PBL_ADDR_BOUND              0xB	/* pbl addr out of bounds:  */
						/* software error */
#define T4_ERR_SWFLUSH			   0xC	/* SW FLUSHED */
#define T4_ERR_CRC                         0x10 /* CRC error */
#define T4_ERR_MARKER                      0x11 /* Marker error */
#define T4_ERR_PDU_LEN_ERR                 0x12 /* invalid PDU length */
#define T4_ERR_OUT_OF_RQE                  0x13 /* out of RQE */
#define T4_ERR_DDP_VERSION                 0x14 /* wrong DDP version */
#define T4_ERR_RDMA_VERSION                0x15 /* wrong RDMA version */
#define T4_ERR_OPCODE                      0x16 /* invalid rdma opcode */
#define T4_ERR_DDP_QUEUE_NUM               0x17 /* invalid ddp queue number */
#define T4_ERR_MSN                         0x18 /* MSN error */
#define T4_ERR_TBIT                        0x19 /* tag bit not set correctly */
#define T4_ERR_MO                          0x1A /* MO not 0 for TERMINATE  */
						/* or READ_REQ */
#define T4_ERR_MSN_GAP                     0x1B
#define T4_ERR_MSN_RANGE                   0x1C
#define T4_ERR_IRD_OVERFLOW                0x1D
#define T4_ERR_RQE_ADDR_BOUND              0x1E /* RQE addr out of bounds:  */
						/* software error */
#define T4_ERR_INTERNAL_ERR                0x1F /* internal error (opcode  */
						/* mismatch) */
/*
 * CQE defs
 */

/*
 * 64B CQE entries.
 */
struct t4_cqe {
	struct rss_header rss;
	union {
		struct {
			__be32 header;
			__be32 len;
			union {
				struct {
					__be32 stag;
					__be32 msn;
				} rcqe;
				struct {
					u32 nada1;
					u16 nada2;
					u16 cidx;
				} scqe;
				struct {
					__be32 wrid_hi;
					__be32 wrid_low;
				} gen;
				struct {
					__be32 stag;
					__be32 msn;
					__be32 reserved;
					__be32 abs_rqe_idx;
				} srcqe;
				struct {
					__be32 mo;
					__be32 msn;
					__u64 imm_data;
				} imm_data_rcqe;
			} u;
		} rdma;
		struct {
			__be32 opcode_qid;
			__be16 cidx;
			__be16 pidx;
			__be64 rss;
			__be32 fw4_opcode_qid;
			__be16 fw4_cidx;
			__be16 fw4_pidx;
		} raw;
		__be64 flits[3];
	} u;
	__be64 reserved[3];
	__be64 bits_type_ts;
};

/* macros for flit 0 of the rdma cqe */

#define S_CQE_QPID        12
#define M_CQE_QPID        0xFFFFF
#define G_CQE_QPID(x)     ((((x) >> S_CQE_QPID)) & M_CQE_QPID)
#define V_CQE_QPID(x)	  ((x)<<S_CQE_QPID)

#define S_CQE_SWCQE       11
#define M_CQE_SWCQE       0x1
#define G_CQE_SWCQE(x)    ((((x) >> S_CQE_SWCQE)) & M_CQE_SWCQE)
#define V_CQE_SWCQE(x)	  ((x)<<S_CQE_SWCQE)

#define S_CQE_STATUS      5
#define M_CQE_STATUS      0x1F
#define G_CQE_STATUS(x)   ((((x) >> S_CQE_STATUS)) & M_CQE_STATUS)
#define V_CQE_STATUS(x)   ((x)<<S_CQE_STATUS)

#define S_CQE_TYPE        4
#define M_CQE_TYPE        0x1
#define G_CQE_TYPE(x)     ((((x) >> S_CQE_TYPE)) & M_CQE_TYPE)
#define V_CQE_TYPE(x)     ((x)<<S_CQE_TYPE)

#define S_CQE_OPCODE      0
#define M_CQE_OPCODE      0xF
#define G_CQE_OPCODE(x)   ((((x) >> S_CQE_OPCODE)) & M_CQE_OPCODE)
#define V_CQE_OPCODE(x)   ((x)<<S_CQE_OPCODE)

#define SW_CQE(x)         (G_CQE_SWCQE(be32_to_cpu((x)->u.rdma.header)))
#define CQE_QPID(x)       (G_CQE_QPID(be32_to_cpu((x)->u.rdma.header)))
#define CQE_TYPE(x)       (G_CQE_TYPE(be32_to_cpu((x)->u.rdma.header)))
#define SQ_TYPE(x)	  (CQE_TYPE((x)))
#define RQ_TYPE(x)	  (!CQE_TYPE((x)))
#define CQE_STATUS(x)     (G_CQE_STATUS(be32_to_cpu((x)->u.rdma.header)))
#define CQE_OPCODE(x)     (G_CQE_OPCODE(be32_to_cpu((x)->u.rdma.header)))

#define CQE_SEND_OPCODE(x) ( \
	(CQE_OPCODE(x) == FW_RI_SEND) || \
	(CQE_OPCODE(x) == FW_RI_SEND_WITH_SE) || \
	(CQE_OPCODE(x) == FW_RI_SEND_WITH_INV) || \
	(CQE_OPCODE(x) == FW_RI_SEND_WITH_SE_INV))

#define CQE_LEN(x)        (be32_to_cpu((x)->u.rdma.len))

/* used for RQ completion processing */
#define CQE_WRID_STAG(x)  (be32_to_cpu((x)->u.rdma.u.rcqe.stag))
#define CQE_WRID_MSN(x)   (be32_to_cpu((x)->u.rdma.u.rcqe.msn))
#define CQE_ABS_RQE_IDX(x) (be32_to_cpu((x)->u.rdma.u.srcqe.abs_rqe_idx))
#define CQE_IMM_DATA(x)   ((x)->u.rdma.u.imm_data_rcqe.imm_data)

/* used for SQ completion processing */
#define CQE_WRID_SQ_IDX(x)	((x)->u.rdma.u.scqe.cidx)

/* generic accessor macros */
#define CQE_WRID_HI(x)		((x)->u.rdma.u.gen.wrid_hi)
#define CQE_WRID_LOW(x)		((x)->u.rdma.u.gen.wrid_low)

/* macros for flit 3 of the cqe */
#define S_CQE_GENBIT	63
#define M_CQE_GENBIT	0x1
#define G_CQE_GENBIT(x)	(((x) >> S_CQE_GENBIT) & M_CQE_GENBIT)
#define V_CQE_GENBIT(x) ((x)<<S_CQE_GENBIT)

#define S_CQE_OVFBIT	62
#define M_CQE_OVFBIT	0x1
#define G_CQE_OVFBIT(x)	((((x) >> S_CQE_OVFBIT)) & M_CQE_OVFBIT)

#define S_CQE_IQTYPE	60
#define M_CQE_IQTYPE	0x3
#define G_CQE_IQTYPE(x)	((((x) >> S_CQE_IQTYPE)) & M_CQE_IQTYPE)

#define M_CQE_TS	0x0fffffffffffffffULL
#define G_CQE_TS(x)	((x) & M_CQE_TS)

#define CQE_OVFBIT(x)	((unsigned)G_CQE_OVFBIT(be64_to_cpu((x)->bits_type_ts)))
#define CQE_GENBIT(x)	((unsigned)G_CQE_GENBIT(be64_to_cpu((x)->bits_type_ts)))
#define CQE_TS(x)	(G_CQE_TS(be64_to_cpu((x)->bits_type_ts)))

static inline int RAW_QPID(struct t4_cqe *cqe)
{
	if (cqe->rss.opcode == CPL_FW4_MSG)
		return G_EGR_QID(be32_to_cpu(cqe->u.raw.fw4_opcode_qid));
	else
		return G_EGR_QID(be32_to_cpu(cqe->u.raw.opcode_qid));
}

static inline int RAW_CIDX(struct t4_cqe *cqe)
{
	if (cqe->rss.opcode == CPL_FW4_MSG)
		return be16_to_cpu(cqe->u.raw.fw4_cidx);
	else
		return be16_to_cpu(cqe->u.raw.cidx);
}

static inline int RAW_PIDX(struct t4_cqe *cqe)
{
	if (cqe->rss.opcode == CPL_FW4_MSG)
		return be16_to_cpu(cqe->u.raw.fw4_pidx);
	else
		return be16_to_cpu(cqe->u.raw.pidx);
}

enum {
	RAW,
	RDMA,
};

static inline int CQE_QPTYPE(struct t4_cqe *cqe)
{
	if (cqe->rss.opcode == CPL_FW4_MSG ||
	    cqe->rss.opcode == CPL_SGE_EGR_UPDATE)
		return RAW;
	else
		return RDMA;
}

struct t4_swsqe {
	u64			wr_id;
	struct t4_cqe		cqe;
	__be32			read_len;
	int			opcode;
	int			complete;
	int			signaled;
	u16			idx;
	int			flushed;
};

enum {
	T4_SQ_ONCHIP =		(1<<0),
	T4_SQ_VLAN =		(1<<1),
	T4_SQ_WRITE_W_IMM =	(1<<2)
};

struct t4_sq {
	union t4_wr *queue;
	struct t4_swsqe *sw_sq;
	struct t4_swsqe *oldest_read;
	volatile u32 *udb;
	size_t memsize;
	u32 qid;
	u32 bar2_qid;
	void *ma_sync;
	u16 in_use;
	u16 size;
	u16 cidx;
	u16 pidx;
	u16 wq_pidx;
	u16 flags;
	short flush_cidx;
	int wc_reg_available;
};

struct t4_swrqe {
	u64 wr_id;
	int valid;
};

struct t4_rq {
	union  t4_recv_wr *queue;
	struct t4_swrqe *sw_rq;
	volatile u32 *udb;
	size_t memsize;
	u32 qid;
	u32 bar2_qid;
	u32 msn;
	u32 rqt_hwaddr;
	u16 rqt_size;
	u16 in_use;
	u16 size;
	u16 cidx;
	u16 pidx;
	u16 wq_pidx;
	int wc_reg_available;
};

struct t4_wq {
	struct t4_sq sq;
	struct t4_rq rq;
	struct c4iw_rdev *rdev;
	u32 qid_mask;
	int error;
	int flushed;
	u8 *db_offp;
	u8 *qp_errp;
	u32 *srqidxp;
};

static inline void t4_ma_sync(struct t4_wq *wq)
{
	wc_wmb();
	*((volatile u32 *)wq->sq.ma_sync) = 1;
}

static inline int t4_rqes_posted(struct t4_wq *wq)
{
	return wq->rq.in_use;
}

static inline int t4_rq_empty(struct t4_wq *wq)
{
	return wq->rq.in_use == 0;
}

static inline int t4_rq_full(struct t4_wq *wq)
{
	return wq->rq.in_use == (wq->rq.size - 1);
}

static inline u32 t4_rq_avail(struct t4_wq *wq)
{
	return wq->rq.size - 1 - wq->rq.in_use;
}

static inline void t4_rq_produce(struct t4_wq *wq, u8 len16)
{
	wq->rq.in_use++;
	if (++wq->rq.pidx == wq->rq.size)
		wq->rq.pidx = 0;
	wq->rq.wq_pidx += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
	if (wq->rq.wq_pidx >= wq->rq.size * T4_RQ_NUM_SLOTS)
		wq->rq.wq_pidx %= wq->rq.size * T4_RQ_NUM_SLOTS;
	if (!wq->error)
		wq->rq.queue[wq->rq.size].status.host_pidx = wq->rq.pidx;
}

static inline void t4_rq_consume(struct t4_wq *wq)
{
	wq->rq.in_use--;
	if (++wq->rq.cidx == wq->rq.size)
		wq->rq.cidx = 0;
	assert((wq->rq.cidx != wq->rq.pidx) || wq->rq.in_use == 0);
	if (!wq->error)
		wq->rq.queue[wq->rq.size].status.host_cidx = wq->rq.cidx;
}

struct t4_srq_pending_wr {
	u64 wr_id;
	union t4_recv_wr wqe;
	u8 len16;
};

struct t4_srq {
	union  t4_recv_wr *queue;
	struct t4_swrqe *sw_rq;
	volatile u32 *udb;
	size_t memsize;
	u32 qid;
	u32 bar2_qid;
	u32 msn;
	u32 rqt_hwaddr;
	u32 rqt_abs_idx;
	u16 in_use;
	u16 size;
	u16 cidx;
	u16 pidx;
	u16 wq_pidx;
	int wc_reg_available;
	struct t4_srq_pending_wr *pending_wrs;
	u16 pending_cidx;
	u16 pending_pidx;
	u16 pending_in_use;
	u16 ooo_count;
};

static inline u32 t4_srq_avail(struct t4_srq *srq)
{
	return srq->size - 1 - srq->in_use;
}

static inline int t4_srq_empty(struct t4_srq *srq)
{
	return srq->in_use == 0;
}

static inline int t4_srq_cidx_at_end(struct t4_srq *srq)
{
	assert(srq->cidx != srq->pidx);
	if (srq->cidx < srq->pidx)
		return srq->cidx == (srq->pidx - 1);
	else
		return srq->cidx == (srq->size - 1) && srq->pidx == 0;
}

static inline int t4_srq_wrs_pending(struct t4_srq *srq)
{
	return srq->pending_cidx != srq->pending_pidx;
}

static inline void t4_srq_produce(struct t4_srq *srq, u8 len16)
{
	srq->in_use++;
	assert(srq->in_use < srq->size);
	if (++srq->pidx == srq->size)
		srq->pidx = 0;
	assert(srq->cidx != srq->pidx); /* overflow */
	srq->wq_pidx += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
	if (srq->wq_pidx >= srq->size * T4_RQ_NUM_SLOTS)
		srq->wq_pidx %= srq->size * T4_RQ_NUM_SLOTS;
	srq->queue[srq->size].status.host_pidx = srq->pidx;
}

static inline void t4_srq_produce_pending_wr(struct t4_srq *srq)
{
	srq->pending_in_use++;
	srq->in_use++;
	assert(srq->pending_in_use < srq->size);
	assert(srq->in_use < srq->size);
	assert(srq->pending_pidx < srq->size);
	if (++srq->pending_pidx == srq->size)
		srq->pending_pidx = 0;
}

static inline void t4_srq_consume_pending_wr(struct t4_srq *srq)
{
	assert(srq->pending_in_use > 0);
	srq->pending_in_use--;
	assert(srq->in_use > 0);
	srq->in_use--;
	if (++srq->pending_cidx == srq->size)
		srq->pending_cidx = 0;
	assert((srq->pending_cidx != srq->pending_pidx) || srq->pending_in_use == 0);
}

static inline void t4_srq_produce_ooo(struct t4_srq *srq)
{
	assert(srq->in_use > 0);
	srq->in_use--;
	srq->ooo_count++;
	assert(srq->ooo_count < srq->size);
}

static inline void t4_srq_consume_ooo(struct t4_srq *srq)
{
	srq->cidx++;
	if (srq->cidx == srq->size)
		srq->cidx  = 0;
	srq->queue[srq->size].status.host_cidx = srq->cidx;
	assert(srq->ooo_count > 0);
	srq->ooo_count--;
}

static inline void t4_srq_consume(struct t4_srq *srq)
{
	assert(srq->in_use > 0);
	srq->in_use--;
	if (++srq->cidx == srq->size)
		srq->cidx = 0;
	assert((srq->cidx != srq->pidx) || srq->in_use == 0);
	srq->queue[srq->size].status.host_cidx = srq->cidx;
}

extern int t5_en_wc;

static inline void copy_wqe_to_udb(volatile u32 *udb_offset, void *wqe)
{
	u64 *src, *dst;
	int len16 = 4;

	src = (u64 *)wqe;
	dst = (u64 *)udb_offset;

	while (len16) {
		*dst++ = *src++;
		*dst++ = *src++;
		len16--;
	}
}

static inline void t4_ring_srq_db(struct t4_srq *srq, u16 inc, u8 len16,
				 union t4_recv_wr *wqe)
{
	wc_wmb();
	if (t5_en_wc && inc == 1 && srq->wc_reg_available) {
		PDBG("%s: WC srq->pidx = %d; len16=%d\n",
		     __func__, srq->pidx, len16);
		copy_wqe_to_udb(srq->udb + 14, wqe);
	} else {
		PDBG("%s: DB srq->pidx = %d; len16=%d\n",
		     __func__, srq->pidx, len16);
		writel(V_QID(srq->bar2_qid) | V_PIDX_T5(inc), srq->udb);
	}
	wc_wmb();
	return;
}

static inline int t4_sq_empty(struct t4_wq *wq)
{
	return wq->sq.in_use == 0;
}

static inline int t4_sq_full(struct t4_wq *wq)
{
	return wq->sq.in_use == (wq->sq.size - 1);
}

static inline u32 t4_sq_avail(struct t4_wq *wq)
{
	return wq->sq.size - 1 - wq->sq.in_use;
}

static inline int t4_sq_onchip(struct t4_wq *wq)
{
	return wq->sq.flags & T4_SQ_ONCHIP;
}

static inline void t4_sq_produce(struct t4_wq *wq, u8 len16)
{
	wq->sq.in_use++;
	if (++wq->sq.pidx == wq->sq.size)
		wq->sq.pidx = 0;
	wq->sq.wq_pidx += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
	if (wq->sq.wq_pidx >= wq->sq.size * T4_SQ_NUM_SLOTS)
		wq->sq.wq_pidx %= wq->sq.size * T4_SQ_NUM_SLOTS;
	if (!wq->error)
		wq->sq.queue[wq->sq.size].status.host_pidx = (wq->sq.pidx);
}

static inline void t4_sq_consume(struct t4_wq *wq)
{
	assert(wq->sq.in_use >= 1);
	if (wq->sq.cidx == wq->sq.flush_cidx)
		wq->sq.flush_cidx = -1;
	wq->sq.in_use--;
	if (++wq->sq.cidx == wq->sq.size)
		wq->sq.cidx = 0;
	assert((wq->sq.cidx != wq->sq.pidx) || wq->sq.in_use == 0);
	if (!wq->error)
		wq->sq.queue[wq->sq.size].status.host_cidx = wq->sq.cidx;
}

extern int ma_wr;

static inline void t4_ring_sq_db(struct t4_wq *wq, u16 inc, u8 t4, u8 len16,
				 union t4_wr *wqe)
{
	wc_wmb();
	if (!t4) {
		if (t5_en_wc && inc == 1 && wq->sq.wc_reg_available) {
			PDBG("%s: WC wq->sq.pidx = %d; len16=%d\n",
			     __func__, wq->sq.pidx, len16);
			copy_wqe_to_udb(wq->sq.udb + 14, wqe);
		} else {
			PDBG("%s: DB wq->sq.pidx = %d; len16=%d\n",
			     __func__, wq->sq.pidx, len16);
			writel(V_QID(wq->sq.bar2_qid) | V_PIDX_T5(inc), wq->sq.udb);
		}
		wc_wmb();
		return;
	}
	if (ma_wr) {
		if (t4_sq_onchip(wq)) {
			int i;
			for (i = 0; i < 16; i++)
				*(volatile u32 *)&wq->sq.queue[wq->sq.size].flits[2+i] = i;
		}
	} else {
		if (t4_sq_onchip(wq)) {
			int i;
			for (i = 0; i < 16; i++)
				*(u32 *)&wq->sq.queue[wq->sq.size].flits[2] = i;
		}
	}
	writel(V_QID(wq->sq.qid & wq->qid_mask) | V_PIDX(inc), wq->sq.udb);
}

static inline void t4_ring_rq_db(struct t4_wq *wq, u16 inc, u8 t4, u8 len16,
				 union t4_recv_wr *wqe)
{
	wc_wmb();
	if (!t4) {
		if (t5_en_wc && inc == 1 && wq->sq.wc_reg_available) {
			PDBG("%s: WC wq->rq.pidx = %d; len16=%d\n",
			     __func__, wq->rq.pidx, len16);
			copy_wqe_to_udb(wq->rq.udb + 14, wqe);
		} else {
			PDBG("%s: DB wq->rq.pidx = %d; len16=%d\n",
			     __func__, wq->rq.pidx, len16);
			writel(V_QID(wq->rq.bar2_qid) | V_PIDX_T5(inc), wq->rq.udb);
		}
		wc_wmb();
		return;
	}
	writel(V_QID(wq->rq.qid & wq->qid_mask) | V_PIDX(inc), wq->rq.udb);
}

static inline int t4_wq_in_error(struct t4_wq *wq)
{
	return wq->error || *wq->qp_errp;
}

static inline u32 t4_wq_srqidx(struct t4_wq *wq)
{
	u32 srqidx;

	if (!wq->srqidxp)
		return 0;
	srqidx = *wq->srqidxp;
	wq->srqidxp = 0;
	return srqidx;
}

static inline void t4_set_wq_in_error(struct t4_wq *wq)
{
	*wq->qp_errp = 1;
}

static inline int t4_wq_db_enabled(struct t4_wq *wq)
{
	return ! *wq->db_offp;
}

#define T4_TXQ_NUM_SLOTS 4
#define T4_TXQ_NUM_BYTES (T4_EQ_ENTRY_SIZE * T4_TXQ_NUM_SLOTS)
#define T4_MAX_TXQ_INLINE (T4_TXQ_NUM_BYTES - \
			   sizeof(struct cpl_tx_pkt) - \
			   sizeof(struct fw_eth_tx_pkt_wr))
struct txq_desc {
	__be64 flits[T4_EQ_ENTRY_SIZE / sizeof(__be64) *T4_TXQ_NUM_SLOTS];
} __attribute__((aligned(T4_EQ_ENTRY_SIZE)));

#define T4_MAX_TXQ_SGE 4

struct t4_swtxq {
	uint64_t wr_id;
	u16 wq_pidx;
	int signaled;
};

struct t4_txq {
	struct txq_desc *queue;
	struct t4_swtxq *sw_queue;
	volatile u32*db;
	void *ma_sync;
	size_t memsize;
	u32 qid;
	u32 bar2_qid;
	u16 size;
	u16 cidx;
	u16 pidx;
	u16 wq_pidx;
	u16 in_use;
	u32 tx_chan;
	u32 pf;
	u32 flags;
	u16 vlan_pri;
	int wc_reg_available;
};

static inline int t4_txq_onchip(struct t4_txq *t)
{
	return t->flags & T4_SQ_ONCHIP;
}

static inline void t4_txq_ma_sync(struct t4_txq *t)
{
	wc_wmb();
	*((volatile u32 *)t->ma_sync) = 1;
}

static inline void t4_txq_consume(struct t4_txq *t)
{
	t->in_use--;
	if (++t->cidx == t->size)
		t->cidx = 0;
	assert((t->cidx != t->pidx) || t->in_use == 0);
}

static inline void t4_hwtxq_produce(struct t4_txq *t, u8 len16)
{
	t->wq_pidx += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
	if (t->wq_pidx >= t->size * T4_TXQ_NUM_SLOTS)
		t->wq_pidx %= t->size * T4_TXQ_NUM_SLOTS;
}

static inline void t4_swtxq_produce(struct t4_txq *t)
{
	t->in_use++;
	if (++t->pidx == t->size)
		t->pidx = 0;
}

static inline u32 t4_txq_avail(struct t4_txq *t)
{
	return t->size - 1 - t->in_use;
}


static inline void t4_ring_txq_db(struct t4_txq *t, int n, int t4,
				  struct txq_desc *wqe)
{
	wc_wmb();
	if (!t4) {
		if (t5_en_wc && n == 1 && t->wc_reg_available) {
			copy_wqe_to_udb(t->db + 14, wqe);
		} else  {
			writel(V_QID(t->bar2_qid) | V_PIDX_T5(n), t->db);
		}
		wc_wmb();
		return;
	}
	if (ma_wr) {
		if (t4_txq_onchip(t)) {
			int i;
			for (i = 0; i < 16; i++)
				*(volatile u32 *)&t->queue[t->size].flits[2+i] = i;
		}
	} else {
		if (t4_txq_onchip(t)) {
			int i;
			for (i = 0; i < 16; i++)
				*(u32 *)&t->queue[t->size].flits[2] = i;
		}
	}
	writel(V_QID(t->qid) | V_PIDX(n), t->db);
}

static inline int t4_txq_db_enabled(struct t4_txq *t)
{
	return !((struct t4_status_page *)&t->queue[t->size])->db_off;
}

struct t4_raw_fl {
	u64 *queue;
	u64 *sw_queue;
	volatile u32 *db;
	size_t memsize;
	u32 qid;
	u32 bar2_qid;
	u16 size;
	u16 cidx;
	u16 pidx;
	u16 in_use;
	u16 pend_cred;
	u8 first_skipped;
	u8 packed;
	u8 *db_offp;
};

static inline void t4_ring_fl_db(struct t4_raw_fl *f, int chip_version)
{
	if (f->pend_cred >= 8) {
		u32 val = 0;

		switch (chip_version) {
		case CHELSIO_T4:
			val = V_PIDX(f->pend_cred / 8) | F_DBPRIO;
			break;
		case CHELSIO_T5:
			val = F_DBPRIO | F_DBTYPE;
		case CHELSIO_T6:
		default:
			val |= V_PIDX_T5(f->pend_cred / 8);
			break;
		}

		wc_wmb();
		writel(val | V_QID(f->bar2_qid), f->db);
		f->pend_cred &= 7;
	}
}

static inline void t4_raw_fl_consume(struct t4_raw_fl *f)
{
	f->in_use--;
	if (++f->cidx == f->size)
		f->cidx = 0;
	assert((f->cidx != f->pidx) || f->in_use == 0);
}

static inline void t4_raw_fl_produce(struct t4_raw_fl *f)
{
	f->in_use++;
	if (++f->pidx == f->size)
		f->pidx = 0;
}

static inline u32 t4_raw_fl_avail(struct t4_raw_fl *f)
{
	return f->size ? f->size - 1 - f->in_use : 0;
}

static inline u32 t4_raw_fl_max_wr(struct t4_raw_fl *f)
{
	return f->size - 1;
}

static inline int t4_fl_db_enabled(struct t4_raw_fl *f)
{
	return ! *f->db_offp;
}

/*
 * IQE defs
 */
struct t4_iqe {
	struct rss_header rss_hdr;	/* flit 0 */
	struct cpl_rx_pkt rx_pkt;	/* flits 1..2 */
	__be64 reserved1;		/* flit 3 */
	__be64 reserved2;		/* flit 4 */
	__be64 reserved3;		/* flit 5 */
	__be64 newbuf_dma_len;		/* flit 6 */
	__be64 bits_type_ts;		/* flit 7 */
};

/* macros for flit 6 of the iqe */

#define S_IQE_DATA_HDR_NEWBUF		61
#define M_IQE_DATA_HDR_NEWBUF		1
#define G_IQE_DATA_HDR_NEWBUF(x)	(((x) >> S_IQE_DATA_HDR_NEWBUF) & \
					 M_IQE_DATA_HDR_NEWBUF)
#define V_IQE_DATA_HDR_NEWBUF(x)	((x)<<S_IQE_DATA_HDR_NEWBUF)

#define S_IQE_DATA_HDR_DMA_LEN		60
#define M_IQE_DATA_HDR_DMA_LEN		0x7fffffffUL
#define G_IQE_DATA_HDR_DMA_LEN(x)	(((x) >> S_IQE_DATA_HDR_DMA_LEN) & \
					 M_IQE_DATA_HDR_DMA_LEN)
#define V_IQE_DATA_HDR_DMA_LEN(x)	((x)<<S_IQE_DATA_HDR_DMA_LEN)

#define S_IQE_DATA_NEWBUF	31
#define M_IQE_DATA_NEWBUF	1
#define G_IQE_DATA_NEWBUF(x)	(((x) >> S_IQE_DATA_NEWBUF) & M_IQE_DATA_NEWBUF)
#define V_IQE_DATA_NEWBUF(x)	((x)<<S_IQE_DATA_NEWBUF)

#define S_IQE_DATA_DMA_LEN	30
#define M_IQE_DATA_DMA_LEN	0x7fffffffUL
#define G_IQE_DATA_DMA_LEN(x)	(((x) >> S_IQE_DATA_DMA_LEN) & \
				 M_IQE_DATA_DMA_LEN)
#define V_IQE_DATA_DMA_LEN(x)	((x)<<S_IQE_DATA_DMA_LEN)

#define IQE_DATA_DMA_LEN(x)	((unsigned)G_IQE_DATA_DMA_LEN( \
				 be64_to_cpu((x)->newbuf_dma_len)))
#define IQE_DATA_NEWBUF(x)	((unsigned)G_IQE_DATA_NEWBUF( \
				 be64_to_cpu((x)->newbuf_dma_len)))

/* macros for flit 7 of the cqe */
#define S_IQE_GENBIT	63
#define M_IQE_GENBIT	0x1
#define G_IQE_GENBIT(x)	(((x) >> S_IQE_GENBIT) & M_IQE_GENBIT)
#define V_IQE_GENBIT(x) ((x)<<S_IQE_GENBIT)

#define S_IQE_OVFBIT	62
#define M_IQE_OVFBIT	0x1
#define G_IQE_OVFBIT(x)	((((x) >> S_IQE_OVFBIT)) & M_IQE_OVFBIT)

#define S_IQE_IQTYPE	60
#define M_IQE_IQTYPE	0x3
#define G_IQE_IQTYPE(x)	((((x) >> S_IQE_IQTYPE)) & M_IQE_IQTYPE)

#define M_IQE_TS	0x0fffffffffffffffULL
#define G_IQE_TS(x)	((x) & M_IQE_TS)

#define IQE_OVFBIT(x)	((unsigned)G_IQE_OVFBIT(be64_to_cpu((x)->bits_type_ts)))
#define IQE_GENBIT(x)	((unsigned)G_IQE_GENBIT(be64_to_cpu((x)->bits_type_ts)))
#define IQE_TS(x)	(G_IQE_TS(be64_to_cpu((x)->bits_type_ts)))

struct t4_iq {
	struct t4_iqe *queue;
	volatile u32 *gts;
	size_t memsize;
	u64 bits_type_ts;
	u16 size;
	u16 cidx;
	u16 cidx_inc;
	u16 qid;
	u8 gen;
	u8 error;
	u8 shared;
	u32 bar2_qid;
};

static inline int t4_valid_iqe(struct t4_iq *iq, struct t4_iqe *iqe)
{
	return (IQE_GENBIT(iqe) == iq->gen);
}

static inline int t4_next_iqe(struct t4_iq *iq, struct t4_iqe **iqe)
{
	int ret;
#ifdef OVERFLOW_DETECTION
	u16 prev_cidx;
#endif
	if (iq->error)
		return -ENODATA;

#ifdef OVERFLOW_DETECTION
	if (iq->cidx == 0)
		prev_cidx = iq->size - 1;
	else
		prev_cidx = iq->cidx - 1;
	if (iq->queue[prev_cidx].bits_type_ts != iq->bits_type_ts) {
		ret = -EOVERFLOW;
		syslog(LOG_NOTICE, "cxgb4 iq overflow iqid %u\n", iq->qid);
		iq->error = 1;
		assert(0);
	} else
#endif
	if (t4_valid_iqe(iq, &iq->queue[iq->cidx])) {
		rmb();
		*iqe = &iq->queue[iq->cidx];
		ret = 0;
	} else
		ret = -ENODATA;
	return ret;
}

static inline int t4_iq_notempty(struct t4_iq *iq)
{
	return t4_valid_iqe(iq, &iq->queue[iq->cidx]);
}

static inline void t4_iq_consume(struct t4_iq *iq)
{
#ifdef OVERFLOW_DETECTION
	iq->bits_type_ts = iq->queue[iq->cidx].bits_type_ts;
#endif

	if (++iq->cidx_inc == (iq->size >> 4) || iq->cidx_inc == M_CIDXINC) {
		uint32_t val;

		val = V_CIDXINC(iq->cidx_inc) | V_TIMERREG(7) |
			V_INGRESSQID(iq->bar2_qid);
		writel(val, iq->gts);
		iq->cidx_inc = 0;
	}
	if (++iq->cidx == iq->size) {
		iq->cidx = 0;
		iq->gen ^= 1;
	}
}

static inline int t4_iq_armed(struct t4_iq *iq)
{
	return ((struct t4_status_page *)&iq->queue[iq->size])->cq_armed;
}

static inline void t4_iq_arm(struct t4_iq *iq)
{
	((struct t4_status_page *)&iq->queue[iq->size])->cq_armed = 1;
}

static inline int t4_arm_iq(struct t4_iq *iq)
{
	u32 val;

	if (t4_iq_armed(iq) && !iq->cidx_inc)
		return 0;
	t4_iq_arm(iq);
	while (iq->cidx_inc > M_CIDXINC) {
		val = V_SEINTARM(0) | V_CIDXINC(M_CIDXINC) | V_TIMERREG(7) |
		      V_INGRESSQID(iq->bar2_qid);
		writel(val, iq->gts);
		iq->cidx_inc -= M_CIDXINC;
	}
	val = V_CIDXINC(iq->cidx_inc) | V_TIMERREG(6) | V_INGRESSQID(iq->bar2_qid);
	writel(val, iq->gts);
	iq->cidx_inc = 0;
	return 0;
}

struct t4_txq;

/*
 * This struct holds out of order IQEs polled from shared IQs.  The IQE and needed data
 * are stored in the associated CQ to be returned when the ULP polls that CQ.
 */
struct t4_swiqe {
	struct t4_iqe iqe;
	u64 wr_id;
	u32 qid;
};

struct t4_cq {
	struct t4_cqe *queue;
	struct t4_cqe *sw_queue;
	struct t4_swiqe *swiq_queue; /* stores SRQ IQEs polled out of order */
	struct c4iw_rdev *rdev;
	volatile u32 *ugts;
	size_t memsize;
	u64 bits_type_ts;
	u32 cqid;
	u32 qid_mask;
	u16 size; /* including status page */
	u16 cidx;
	u16 sw_pidx;
	u16 sw_cidx;
	u16 sw_in_use;
	u16 swiq_pidx;
	u16 swiq_cidx;
	u16 swiq_in_use;
	u16 cidx_inc;
	u8 gen;
	u8 error;
};

static inline int t4_valid_cqe(struct t4_cq *cq, struct t4_cqe *cqe)
{
	return (CQE_GENBIT(cqe) == cq->gen);
}

static inline int t4_cq_notempty(struct t4_cq *cq)
{
	return cq->sw_in_use || t4_valid_cqe(cq, &cq->queue[cq->cidx]);
}

static inline int t4_cq_armed(struct t4_cq *cq)
{
	return ((struct t4_status_page *)&cq->queue[cq->size])->cq_armed;
}

static inline void t4_cq_arm(struct t4_cq *cq)
{
	((struct t4_status_page *)&cq->queue[cq->size])->cq_armed = 1;
}

static inline int t4_arm_cq(struct t4_cq *cq, int se)
{
	u32 val;

	if (t4_cq_armed(cq) && !cq->cidx_inc)
		return 0;
	t4_cq_arm(cq);
	while (cq->cidx_inc > M_CIDXINC) {
		val = V_SEINTARM(0) | V_CIDXINC(M_CIDXINC) | V_TIMERREG(7) |
		      V_INGRESSQID(cq->cqid & cq->qid_mask);
		writel(val, cq->ugts);
		cq->cidx_inc -= M_CIDXINC;
	}
	val = V_SEINTARM(se) | V_CIDXINC(cq->cidx_inc) | V_TIMERREG(6) |
	      V_INGRESSQID(cq->cqid & cq->qid_mask);
	writel(val, cq->ugts);
	cq->cidx_inc = 0;
	return 0;
}

static inline void t4_swcq_produce(struct t4_cq *cq)
{
	cq->sw_in_use++;
	if (cq->sw_in_use == cq->size) {
		syslog(LOG_NOTICE, "cxgb4 sw cq overflow cqid %u\n", cq->cqid);
		cq->error = 1;
		assert(0);
	}
	if (++cq->sw_pidx == cq->size)
		cq->sw_pidx = 0;
}

static inline void t4_swcq_consume(struct t4_cq *cq)
{
	assert(cq->sw_in_use >= 1);
	cq->sw_in_use--;
	if (++cq->sw_cidx == cq->size)
		cq->sw_cidx = 0;
}

static inline void t4_swiq_produce(struct t4_cq *cq)
{
	cq->swiq_in_use++;
	if (cq->swiq_in_use == cq->size) {
		syslog(LOG_NOTICE, "cxgb4 sw cq overflow cqid %u\n", cq->cqid);
		cq->error = 1;
		assert(0);
	}
	if (++cq->swiq_pidx == cq->size)
		cq->swiq_pidx = 0;
}

static inline void t4_swiq_consume(struct t4_cq *cq)
{
	assert(cq->swiq_in_use >= 1);
	cq->swiq_in_use--;
	if (++cq->swiq_cidx == cq->size)
		cq->swiq_cidx = 0;
}

static inline void t4_hwcq_consume(struct t4_cq *cq)
{
#ifdef OVERFLOW_DETECTION
	cq->bits_type_ts = cq->queue[cq->cidx].bits_type_ts;
#endif
	if (++cq->cidx_inc == (cq->size >> 4) || cq->cidx_inc == M_CIDXINC) {
		uint32_t val;

		val = V_SEINTARM(0) | V_CIDXINC(cq->cidx_inc) | V_TIMERREG(7) |
			V_INGRESSQID(cq->cqid & cq->qid_mask);
		writel(val, cq->ugts);
		cq->cidx_inc = 0;
	}
	if (++cq->cidx == cq->size) {
		cq->cidx = 0;
		cq->gen ^= 1;
	}
	((struct t4_status_page *)&cq->queue[cq->size])->host_cidx = cq->cidx;
}

static inline int t4_next_hw_cqe(struct t4_cq *cq, struct t4_cqe **cqe)
{
	int ret;
#ifdef OVERFLOW_DETECTION
	u16 prev_cidx;

	if (cq->cidx == 0)
		prev_cidx = cq->size - 1;
	else
		prev_cidx = cq->cidx - 1;
	if (cq->queue[prev_cidx].bits_type_ts != cq->bits_type_ts) {
		ret = -EOVERFLOW;
		syslog(LOG_NOTICE, "cxgb4 cq overflow cqid %u\n", cq->cqid);
		cq->error = 1;
		assert(0);
	} else
#endif
	if (t4_valid_cqe(cq, &cq->queue[cq->cidx])) {
		rmb();
		*cqe = &cq->queue[cq->cidx];
		ret = 0;
	} else
		ret = -ENODATA;
	return ret;
}

static inline struct t4_cqe *t4_next_sw_cqe(struct t4_cq *cq)
{
	if (cq->sw_in_use == cq->size) {
		syslog(LOG_NOTICE, "cxgb4 sw cq overflow cqid %u\n", cq->cqid);
		cq->error = 1;
		assert(0);
		return NULL;
	}
	if (cq->sw_in_use)
		return &cq->sw_queue[cq->sw_cidx];
	return NULL;
}

static inline int t4_next_cqe(struct t4_cq *cq, struct t4_cqe **cqe)
{
	int ret = 0;

	if (cq->error)
		ret = -ENODATA;
	else if (cq->sw_in_use)
		*cqe = &cq->sw_queue[cq->sw_cidx];
	else ret = t4_next_hw_cqe(cq, cqe);
	return ret;
}

static inline int t4_cq_in_error(struct t4_cq *cq)
{
	return ((struct t4_status_page *)&cq->queue[cq->size])->qp_err;
}

static inline void t4_set_cq_in_error(struct t4_cq *cq)
{
	((struct t4_status_page *)&cq->queue[cq->size])->qp_err = 1;
}

static inline void t4_reset_cq_in_error(struct t4_cq *cq)
{
	((struct t4_status_page *)&cq->queue[cq->size])->qp_err = 0;
}

struct t4_dev_status_page 
{
	u8 db_off;
	u8 wc_supported;
	u16 pad2;
	u32 pad3;
	u64 qp_start;
	u64 qp_size;
	u64 cq_start;
	u64 cq_size;
};
#endif
