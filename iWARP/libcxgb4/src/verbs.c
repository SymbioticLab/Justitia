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
#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <assert.h>

#include "libcxgb4.h"
#include "cxgb4-abi.h"

#define MASKED(x) (void *)((unsigned long)(x) & c4iw_page_mask)

int c4iw_query_device(struct ibv_context *context, struct ibv_device_attr *attr)
{
	struct ibv_query_device cmd;
	uint64_t raw_fw_ver;
	u8 major, minor, sub_minor, build;
	int ret;

	ret = ibv_cmd_query_device(context, attr, &raw_fw_ver, &cmd,
				   sizeof cmd);
	if (ret)
		return ret;

	major = (raw_fw_ver >> 24) & 0xff;
	minor = (raw_fw_ver >> 16) & 0xff;
	sub_minor = (raw_fw_ver >> 8) & 0xff;
	build = raw_fw_ver & 0xff;

	snprintf(attr->fw_ver, sizeof attr->fw_ver,
		 "%d.%d.%d.%d", major, minor, sub_minor, build);

	return 0;
}

int c4iw_query_port(struct ibv_context *context, uint8_t port,
		    struct ibv_port_attr *attr)
{
	struct ibv_query_port cmd;

	return ibv_cmd_query_port(context, port, attr, &cmd, sizeof cmd);
}

struct ibv_pd *c4iw_alloc_pd(struct ibv_context *context)
{
	struct ibv_alloc_pd cmd;
	struct c4iw_alloc_pd_resp resp;
	struct c4iw_pd *pd;

	pd = malloc(sizeof *pd);
	if (!pd)
		return NULL;

	if (ibv_cmd_alloc_pd(context, &pd->ibv_pd, &cmd, sizeof cmd,
			     &resp.ibv_resp, sizeof resp)) {
		free(pd);
		return NULL;
	}

	return &pd->ibv_pd;
}

int c4iw_free_pd(struct ibv_pd *pd)
{
	int ret;

	ret = ibv_cmd_dealloc_pd(pd);
	if (ret)
		return ret;

	free(pd);
	return 0;
}

static struct ibv_mr *__c4iw_reg_mr(struct ibv_pd *pd, void *addr,
				    size_t length, uint64_t hca_va,
				    ENUM_IBV_ACCESS_FLAGS access)
{
	struct c4iw_mr *mhp;
	struct c4iw_reg_mr_req cmd;
	struct c4iw_reg_mr_resp resp;
	struct c4iw_dev *dev = to_c4iw_dev(pd->context->device);
	int pbl_depth;
	int size;

	pbl_depth = length / c4iw_page_size;
	if (length % c4iw_page_size)
		pbl_depth++;
	if (((u64)(uintptr_t)addr) & (c4iw_page_size - 1))
		pbl_depth++;
	size = sizeof *mhp + pbl_depth * sizeof(uint64_t);
	mhp = malloc(size);
	if (!mhp)
		return NULL;

	cmd.pbl_ptr = (uint64_t)(uintptr_t)mhp->sw_pbl;
	resp.page_size = 1;
	if (ibv_cmd_reg_mr(pd, addr, length, hca_va,
			   access, &mhp->ibv_mr, &cmd.ibv_cmd, sizeof cmd,
			   &resp.ibv_resp, sizeof resp)) {
		free(mhp);
		return NULL;
	}

	if (resp.page_size == 1) {
		fprintf(stderr,
			"libcxgb4: downlevel iw_cxgb4 driver! (nonfatal)\n");
		mhp->page_size = c4iw_page_size;
	} else
		mhp->page_size = resp.page_size;
	mhp->page_mask = ~(mhp->page_size - 1);
	mhp->page_shift = long_log2(mhp->page_size);
	mhp->va_fbo = hca_va;
	mhp->len = length;

	PDBG("%s stag 0x%x va_fbo 0x%" PRIx64 " len %d\n",
	     __func__, mhp->ibv_mr.rkey, mhp->va_fbo, mhp->len);

	pthread_spin_lock(&dev->lock);
	dev->mmid2ptr[c4iw_mmid(mhp->ibv_mr.lkey)] = mhp;
	pthread_spin_unlock(&dev->lock);
	INC_STAT(mr);
	return &mhp->ibv_mr;
}

struct ibv_mr *c4iw_reg_mr(struct ibv_pd *pd, void *addr,
			   size_t length, ENUM_IBV_ACCESS_FLAGS access)
{
	PDBG("%s addr %p length %ld\n", __func__, addr, length);
	return __c4iw_reg_mr(pd, addr, length, (uintptr_t) addr, access);
}

int c4iw_dereg_mr(struct ibv_mr *mr)
{
	int ret;
	struct c4iw_dev *dev = to_c4iw_dev(mr->pd->context->device);

	ret = ibv_cmd_dereg_mr(mr);
	if (ret)
		return ret;

	pthread_spin_lock(&dev->lock);
	dev->mmid2ptr[c4iw_mmid(mr->lkey)] = NULL;
	pthread_spin_unlock(&dev->lock);

	free(to_c4iw_mr(mr));

	return 0;
}

struct ibv_cq *c4iw_create_cq(struct ibv_context *context, int cqe,
			      struct ibv_comp_channel *channel, int comp_vector)
{
	struct c4iw_create_cq_req cmd;
	struct c4iw_create_cq_resp resp;
	struct c4iw_cq *chp;
	struct c4iw_dev *dev = to_c4iw_dev(context->device);
	int ret;

	if (dev->abi_version < 5) {
		fprintf(stderr, "libcxgb4 FATAL ERROR: downlevel iw_cxgb4 "
			"module.  Cannot support RDMA with this driver/lib"
			" combination.  Update your drivers!\n");
		return NULL;
	}

	chp = calloc(1, sizeof *chp);
	if (!chp) {
		return NULL;
	}

	cmd.cqe_size = sizeof *chp->cq.queue;
	ret = ibv_cmd_create_cq(context, cqe, channel, comp_vector,
				&chp->ibv_cq, &cmd.ibv_req, sizeof cmd,
				&resp.ibv_resp, sizeof resp);
	if (ret)
		goto err1;

	pthread_spin_init(&chp->lock, PTHREAD_PROCESS_PRIVATE);
#ifdef STALL_DETECTION
	gettimeofday(&chp->time, NULL);
#endif
	chp->rhp = dev;
	chp->cq.qid_mask = resp.qid_mask;
	chp->cq.cqid = resp.cqid;
	chp->cq.size = resp.size;
	chp->cq.memsize = resp.memsize;
	chp->cq.gen = 1;
	chp->cq.queue = mmap(NULL, chp->cq.memsize, PROT_READ|PROT_WRITE,
			     MAP_SHARED, context->cmd_fd, resp.key);
	if (chp->cq.queue == MAP_FAILED)
		goto err2;

	chp->cq.ugts = mmap(NULL, c4iw_page_size, PROT_WRITE, MAP_SHARED,
			   context->cmd_fd, resp.gts_key);
	if (chp->cq.ugts == MAP_FAILED)
		goto err3;

	if (dev_is_t4(chp->rhp))
		chp->cq.ugts += 1;
	else
		chp->cq.ugts += 5;
	chp->cq.sw_queue = calloc(chp->cq.size, sizeof *chp->cq.queue);
	if (!chp->cq.sw_queue)
		goto err4;

	chp->cq.swiq_queue = calloc(chp->cq.size, sizeof *chp->cq.swiq_queue);
	if (!chp->cq.swiq_queue)
		goto err5;


	PDBG("%s cqid 0x%x key %" PRIx64 " va %p memsize %lu gts_key %"
	       PRIx64 " va %p qid_mask 0x%x\n",
	       __func__, chp->cq.cqid, resp.key, chp->cq.queue,
	       chp->cq.memsize, resp.gts_key, chp->cq.ugts, chp->cq.qid_mask);

	pthread_spin_lock(&dev->lock);
	dev->cqid2ptr[chp->cq.cqid] = chp;
	pthread_spin_unlock(&dev->lock);
	INC_STAT(cq);
	return &chp->ibv_cq;
err5:
	free(chp->cq.sw_queue);
err4:
	munmap(MASKED(chp->cq.ugts), c4iw_page_size);
err3:
	munmap(chp->cq.queue, chp->cq.memsize);
err2:
	(void)ibv_cmd_destroy_cq(&chp->ibv_cq);
err1:
	free(chp);
	return NULL;
}

int c4iw_resize_cq(struct ibv_cq *ibcq, int cqe)
{
#if 0
	int ret;

	struct ibv_resize_cq cmd;
	struct ibv_resize_cq_resp resp;
	ret = ibv_cmd_resize_cq(ibcq, cqe, &cmd, sizeof cmd, &resp,
				sizeof resp);
	PDBG("%s ret %d\n", __func__, ret);
	return ret;
#else
	return -ENOSYS;
#endif
}

int c4iw_destroy_cq(struct ibv_cq *ibcq)
{
	int ret;
	struct c4iw_cq *chp = to_c4iw_cq(ibcq);
	struct c4iw_dev *dev = to_c4iw_dev(ibcq->context->device);

	chp->cq.error = 1;
	ret = ibv_cmd_destroy_cq(ibcq);
	if (ret) {
		return ret;
	}
	munmap(MASKED(chp->cq.ugts), c4iw_page_size);
	munmap(chp->cq.queue, chp->cq.memsize);

	pthread_spin_lock(&dev->lock);
	dev->cqid2ptr[chp->cq.cqid] = NULL;
	pthread_spin_unlock(&dev->lock);

	free(chp->cq.swiq_queue);
	free(chp->cq.sw_queue);
	free(chp);
	return 0;
}

static struct ibv_srq *create_raw_srq(struct ibv_pd *pd,
				      struct ibv_srq_init_attr *attr)
{
	struct c4iw_create_raw_srq_req cmd;
	struct c4iw_create_raw_srq_resp resp;
	struct c4iw_raw_srq *srq;
	struct c4iw_dev *dev = to_c4iw_dev(pd->context->device);
	struct c4iw_context *ctx = to_c4iw_context(pd->context);
	int ret;

	PDBG("%s enter qp\n", __func__);
	if (dev->abi_version < 6) {
		fprintf(stderr, "libcxgb4 FATAL ERROR: downlevel iw_cxgb4 "
			"module.  Cannot support WD queues with this driver/lib"
			" combination.  Update your drivers!\n");
		goto err1;
	}
	srq = calloc(1, sizeof *srq);
	if (!srq)
		goto err1;

	cmd.port = attr->attr.srq_limit & 0xf;
	if ((attr->attr.srq_limit >> 4) & 1) {
		cmd.flags = FL_PACKED_MODE;
		srq->fl.packed = 1;
	} else
		cmd.flags = 0;
	ret = ibv_cmd_create_srq(pd, &srq->ibv_srq, attr, &cmd.ibv_req,
				sizeof cmd, &resp.ibv_resp, sizeof resp);
	if (ret)
		goto err2;
	srq->rhp = dev;
	srq->fl.size = resp.fl_size;
	srq->fl.memsize = resp.fl_memsize;
	srq->fl.qid = resp.fl_id;
	srq->iq.size = resp.iq_size;
	srq->iq.memsize = resp.iq_memsize;
	srq->iq.gen = 1;
	srq->iq.qid = resp.iq_id;
	srq->qid_mask = resp.qid_mask;
	pthread_spin_init(&srq->lock, PTHREAD_PROCESS_PRIVATE);

	srq->iq.queue = mmap(NULL, srq->iq.memsize,
			     PROT_WRITE, MAP_SHARED,
			     pd->context->cmd_fd, resp.iq_key);
	if (srq->iq.queue == MAP_FAILED)
		goto err4;
	PDBG("%s iq qid %u iq.size %u, iq.memsize %u\n", __func__,
	     srq->iq.qid, (unsigned)srq->iq.size, (unsigned)srq->iq.memsize);

	srq->fl.queue = mmap(NULL, srq->fl.memsize,
			     PROT_WRITE, MAP_SHARED,
			     pd->context->cmd_fd, resp.fl_key);
	if (srq->fl.queue == MAP_FAILED)
		goto err5;
	PDBG("%s fl qid %u fl.size %u, fl.memsize %u\n", __func__,
	     srq->fl.qid, (unsigned)srq->fl.size, (unsigned)srq->fl.memsize);

	if (dev_is_t4(srq->rhp)) {
		srq->fl.db = mmap(NULL, c4iw_page_size,
				     PROT_WRITE, MAP_SHARED,
				     pd->context->cmd_fd, resp.db_key);
		if (srq->fl.db == MAP_FAILED)
			goto err6;
		srq->fl.bar2_qid = srq->fl.qid;
		srq->iq.gts = srq->fl.db + 1;
		srq->iq.bar2_qid = srq->iq.qid;
	} else {
		srq->fl.db = mmap(NULL, c4iw_page_size, PROT_WRITE,
					  MAP_SHARED, pd->context->cmd_fd,
					  resp.fl_bar2_key);
		if (srq->fl.db == MAP_FAILED)
			goto err6;
		srq->fl.db += 2;
		srq->fl.bar2_qid = srq->fl.qid & srq->qid_mask;

		srq->iq.gts = mmap(NULL, c4iw_page_size, PROT_WRITE,
					  MAP_SHARED, pd->context->cmd_fd,
					  resp.iq_bar2_key);
		if (srq->iq.gts == MAP_FAILED)
			goto err7;
		srq->iq.gts += 3;
		srq->iq.bar2_qid = srq->iq.qid & srq->qid_mask;
	}

	srq->fl.sw_queue = calloc(srq->fl.size, sizeof(uint64_t));
	if (!srq->fl.sw_queue)
		goto err8;

	srq->iq.shared = 1;
	srq->type = C4IW_SRQ_RAW;

	if (ctx->status_page_size)
		srq->fl.db_offp = &ctx->status_page->db_off;
	else
		srq->fl.db_offp = &((struct t4_status_page *)
			&srq->fl.queue[srq->fl.size])->db_off;

	return &srq->ibv_srq;
err8:
	if (!dev_is_t4(srq->rhp))
		munmap(MASKED(srq->iq.gts), c4iw_page_size);
err7:
	munmap((void *)srq->fl.db, c4iw_page_size);
err6:
	munmap((void *)srq->fl.queue, srq->fl.memsize);
err5:
	munmap((void *)srq->iq.queue, srq->iq.memsize);
err4:
	(void)ibv_cmd_destroy_srq(&srq->ibv_srq);
err2:
	free(srq);
err1:
	return NULL;
}

static struct ibv_srq *create_srq(struct ibv_pd *pd,
				  struct ibv_srq_init_attr *attr)
{
	struct ibv_create_srq cmd;
	struct c4iw_create_srq_resp resp;
	struct c4iw_srq *srq;
	struct c4iw_dev *dev = to_c4iw_dev(pd->context->device);
	int ret;
	void *dbva;
	unsigned long segment_offset;

	PDBG("%s enter\n", __func__);
	srq = calloc(1, sizeof *srq);
	if (!srq)
		goto err;

	ret = ibv_cmd_create_srq(pd, &srq->ibv_srq, attr, &cmd,
				 sizeof cmd, &resp.ibv_resp, sizeof resp);
	if (ret)
		goto err_free_srq_mem;

	PDBG("%s srq id 0x%x srq key %" PRIx64 " srq db/gts key %" PRIx64
		" qid_mask 0x%x\n", __func__,
		resp.srqid, resp.srq_key, resp.srq_db_gts_key,
		resp.qid_mask);

	srq->type = C4IW_SRQ_BASIC;
	srq->rhp = dev;
	srq->wq.qid = resp.srqid;
	srq->wq.size = resp.srq_size;
	srq->wq.memsize = resp.srq_memsize;
	srq->wq.rqt_abs_idx = resp.rqt_abs_idx;
	srq->flags = resp.flags;
	pthread_spin_init(&srq->lock, PTHREAD_PROCESS_PRIVATE);

	dbva = mmap(NULL, c4iw_page_size, PROT_WRITE, MAP_SHARED,
		    pd->context->cmd_fd, resp.srq_db_gts_key);
	if (dbva == MAP_FAILED)
		goto err_destroy_srq;
	srq->wq.udb = dbva;

	segment_offset = 128 * (srq->wq.qid & resp.qid_mask);
	if (segment_offset < c4iw_page_size) {
		srq->wq.udb += segment_offset / 4;
		srq->wq.wc_reg_available = 1;
	} else
		srq->wq.bar2_qid = srq->wq.qid & resp.qid_mask;
	srq->wq.udb += 2;

	srq->wq.queue = mmap(NULL, srq->wq.memsize,
			    PROT_WRITE, MAP_SHARED,
			    pd->context->cmd_fd, resp.srq_key);
	if (srq->wq.queue == MAP_FAILED)
		goto err_unmap_udb;

	srq->wq.sw_rq = calloc(srq->wq.size, sizeof (struct t4_swrqe));
	if (!srq->wq.sw_rq)
		goto err_unmap_queue;
	srq->wq.pending_wrs = calloc(srq->wq.size, sizeof *srq->wq.pending_wrs);
	if (!srq->wq.pending_wrs)
		goto err_free_sw_rq;

	PDBG("%s srq dbva %p srq qva %p srq depth %u srq memsize %lu\n",
	     __func__, srq->wq.udb, srq->wq.queue,
	     srq->wq.size, srq->wq.memsize);

	INC_STAT(srq);
	return &srq->ibv_srq;
err_free_sw_rq:
	free(srq->wq.sw_rq);
err_unmap_queue:
	munmap((void *)srq->wq.queue, srq->wq.memsize);
err_unmap_udb:
	munmap(MASKED(srq->wq.udb), c4iw_page_size);
err_destroy_srq:
	(void)ibv_cmd_destroy_srq(&srq->ibv_srq);
err_free_srq_mem:
	free(srq);
err:
	return NULL;
}

struct ibv_srq *c4iw_create_srq(struct ibv_pd *pd,
				struct ibv_srq_init_attr *attr)
{
	/*
	 * XOX - top bit on srq_limit indicates WD SRQ!
	 */
	if ((attr->attr.srq_limit >> 31) & 1)
		return create_raw_srq(pd, attr);
	return create_srq(pd, attr);
}


static int modify_srq(struct ibv_srq *ibsrq, struct ibv_srq_attr *attr,
		      ENUM_IBV_SRQ_ATTR_MASK attr_mask)
{
	struct c4iw_srq *srq = to_c4iw_srq(ibsrq);
	struct ibv_modify_srq cmd;
	int ret;

	/* XXX no support for this yet */
	if (attr_mask & IBV_SRQ_MAX_WR)
		return ENOSYS;

	ret = ibv_cmd_modify_srq(ibsrq, attr, attr_mask, &cmd, sizeof cmd);
	if (!ret) {
		if (attr_mask & IBV_SRQ_LIMIT) {
			srq->armed = 1;
			srq->srq_limit = attr->srq_limit;
		}
	}
	return ret;
}

static int modify_raw_srq(struct ibv_srq *srq, struct ibv_srq_attr *attr,
			  ENUM_IBV_SRQ_ATTR_MASK attr_mask)
{
	return ENOSYS;
}

int c4iw_modify_srq(struct ibv_srq *ibsrq, struct ibv_srq_attr *attr,
		    ENUM_IBV_SRQ_ATTR_MASK attr_mask)
{
	struct c4iw_srq *srq = to_c4iw_srq(ibsrq);

	if (srq->type == C4IW_SRQ_RAW)
		return modify_raw_srq(ibsrq, attr, attr_mask);
	return modify_srq(ibsrq, attr, attr_mask);
}

static int query_raw_srq(struct ibv_srq *ibsrq, struct ibv_srq_attr *attr)
{
	struct c4iw_raw_srq *srq = to_c4iw_raw_srq(ibsrq);

	attr->srq_limit = srq->iq.qid;
	attr->max_wr = t4_raw_fl_max_wr(&srq->fl);
	attr->max_sge = 4;
	return 0;
}

static int query_srq(struct ibv_srq *ibsrq, struct ibv_srq_attr *attr)
{
	struct ibv_query_srq cmd;

	return ibv_cmd_query_srq(ibsrq, attr, &cmd, sizeof cmd);
}

int c4iw_query_srq(struct ibv_srq *ibsrq, struct ibv_srq_attr *attr)
{
	struct c4iw_srq *srq = to_c4iw_srq(ibsrq);

	if (srq->type == C4IW_SRQ_RAW)
		return query_raw_srq(ibsrq, attr);
	return query_srq(ibsrq, attr);
}

static int destroy_raw_srq(struct ibv_srq *ibsrq)
{
	int ret;
	struct c4iw_raw_srq *srq = to_c4iw_raw_srq(ibsrq);

	PDBG("%s enter iqid %u\n", __func__, srq->iq.qid);
	ret = ibv_cmd_destroy_srq(ibsrq);
	if (ret)
		return ret;
	munmap(MASKED(srq->fl.db), c4iw_page_size);
	if (!dev_is_t4(srq->rhp))
		munmap(MASKED(srq->iq.gts), c4iw_page_size);
	munmap(srq->fl.queue, srq->fl.memsize);
	munmap(srq->iq.queue, srq->iq.memsize);

	free(srq->fl.sw_queue);
	free(srq);
	return 0;
}

static int destroy_srq(struct ibv_srq *ibsrq)
{
	int ret;
	struct c4iw_srq *srq = to_c4iw_srq(ibsrq);

	PDBG("%s enter qp %p\n", __func__, ibsrq);

	ret = ibv_cmd_destroy_srq(ibsrq);
	if (ret) {
		return ret;
	}
	munmap(MASKED(srq->wq.udb), c4iw_page_size);
	munmap(srq->wq.queue, srq->wq.memsize);

	free(srq->wq.pending_wrs);
	free(srq->wq.sw_rq);
	free(srq);
	return 0;
}

int c4iw_destroy_srq(struct ibv_srq *ibsrq)
{
	struct c4iw_srq *srq = to_c4iw_srq(ibsrq);

	if (srq->type == C4IW_SRQ_RAW)
		return destroy_raw_srq(ibsrq);
	return destroy_srq(ibsrq);
}

static struct ibv_qp *create_qp_v0(struct ibv_pd *pd,
				   struct ibv_qp_init_attr *attr)
{
	printf("DEBUG create_qp_v0: Enter\n");
	struct ibv_create_qp cmd;
	struct c4iw_create_qp_resp_v0 resp;
	struct c4iw_qp *qhp;
	struct c4iw_dev *dev = to_c4iw_dev(pd->context->device);
	int ret;
	void *dbva;

	PDBG("%s enter qp\n", __func__);
	qhp = calloc(1, sizeof *qhp);
	if (!qhp)
		goto err1;

	ret = ibv_cmd_create_qp(pd, &qhp->ibv_qp, attr, &cmd,
				sizeof cmd, &resp.ibv_resp, sizeof resp);
	if (ret)
		goto err2;

	PDBG("%s sqid 0x%x sq key %" PRIx64 " sq db/gts key %" PRIx64
	       " rqid 0x%x rq key %" PRIx64 " rq db/gts key %" PRIx64
	       " qid_mask 0x%x\n",
		__func__,
		resp.sqid, resp.sq_key, resp.sq_db_gts_key,
		resp.rqid, resp.rq_key, resp.rq_db_gts_key, resp.qid_mask);

	qhp->wq.qid_mask = resp.qid_mask;
	qhp->rhp = dev;
	qhp->wq.sq.qid = resp.sqid;
	qhp->wq.sq.size = resp.sq_size;
	qhp->wq.sq.memsize = resp.sq_memsize;
	qhp->wq.sq.flags = 0;
	qhp->wq.rq.msn = 1;
	qhp->wq.rq.qid = resp.rqid;
	qhp->wq.rq.size = resp.rq_size;
	qhp->wq.rq.memsize = resp.rq_memsize;
	pthread_spin_init(&qhp->lock, PTHREAD_PROCESS_PRIVATE);

	dbva = mmap(NULL, c4iw_page_size, PROT_WRITE, MAP_SHARED,
		    pd->context->cmd_fd, resp.sq_db_gts_key);
	if (dbva == MAP_FAILED)
		goto err3;

	qhp->wq.sq.udb = dbva;
	qhp->wq.sq.queue = mmap(NULL, qhp->wq.sq.memsize,
			    PROT_WRITE, MAP_SHARED,
			    pd->context->cmd_fd, resp.sq_key);
	if (qhp->wq.sq.queue == MAP_FAILED)
		goto err4;

	dbva = mmap(NULL, c4iw_page_size, PROT_WRITE, MAP_SHARED,
		    pd->context->cmd_fd, resp.rq_db_gts_key);
	if (dbva == MAP_FAILED)
		goto err5;
	qhp->wq.rq.udb = dbva;
	qhp->wq.rq.queue = mmap(NULL, qhp->wq.rq.memsize,
			    PROT_WRITE, MAP_SHARED,
			    pd->context->cmd_fd, resp.rq_key);
	if (qhp->wq.rq.queue == MAP_FAILED)
		goto err6;

	qhp->wq.sq.sw_sq = calloc(qhp->wq.sq.size, sizeof (struct t4_swsqe));
	if (!qhp->wq.sq.sw_sq)
		goto err7;

	qhp->wq.rq.sw_rq = calloc(qhp->wq.rq.size, sizeof (struct t4_swrqe));
	if (!qhp->wq.rq.sw_rq)
		goto err8;

	PDBG("%s sq dbva %p sq qva %p sq depth %u sq memsize %lu "
	       " rq dbva %p rq qva %p rq depth %u rq memsize %lu\n",
	     __func__,
	     qhp->wq.sq.udb, qhp->wq.sq.queue,
	     qhp->wq.sq.size, qhp->wq.sq.memsize,
	     qhp->wq.rq.udb, qhp->wq.rq.queue,
	     qhp->wq.rq.size, qhp->wq.rq.memsize);

	qhp->sq_sig_all = attr->sq_sig_all;

	pthread_spin_lock(&dev->lock);
	dev->qpid2ptr[qhp->wq.sq.qid] = qhp;
	pthread_spin_unlock(&dev->lock);
	INC_STAT(qp);
	return &qhp->ibv_qp;
err8:
	free(qhp->wq.sq.sw_sq);
err7:
	munmap((void *)qhp->wq.rq.queue, qhp->wq.rq.memsize);
err6:
	munmap(MASKED(qhp->wq.rq.udb), c4iw_page_size);
err5:
	munmap((void *)qhp->wq.sq.queue, qhp->wq.sq.memsize);
err4:
	munmap(MASKED(qhp->wq.sq.udb), c4iw_page_size);
err3:
	(void)ibv_cmd_destroy_qp(&qhp->ibv_qp);
err2:
	free(qhp);
err1:
	return NULL;
}

static struct ibv_qp *create_raw_qp(struct ibv_pd *pd,
				    struct ibv_qp_init_attr *attr)
{
	struct c4iw_create_raw_qp_req cmd;
	struct c4iw_create_raw_qp_resp resp;
	struct c4iw_raw_qp *rqp;
	struct c4iw_dev *dev = to_c4iw_dev(pd->context->device);
	struct c4iw_cq *rchp = to_c4iw_cq(attr->recv_cq);
	struct c4iw_cq *schp = to_c4iw_cq(attr->send_cq);
	struct c4iw_context *ctx = to_c4iw_context(pd->context);
	int ret;

	PDBG("%s enter qp\n", __func__);

	if (dev->abi_version < 6) {
		fprintf(stderr, "libcxgb4 FATAL ERROR: downlevel iw_cxgb4 "
			"module.  Cannot support WD queues with this driver/lib"
			" combination.  Update your drivers!\n");
		goto err1;
	}

	rqp = calloc(1, sizeof *rqp);
	if (!rqp)
		goto err1;

	/*
	 * XXX overload sq_sig_all:
	 * bits		function
	 * ----------------------------------------------------------------
	 * 0:0		whether to signal all wrs on this sq (intended use)
	 * 3:1		number of filter ids to allocate
	 * 4:4		enable FL packing mode
	 * 5:5		enable CONG_DROP in the FL SGE context
	 * 7:6		reserved
	 * 11:8		device port number
	 * 15:12	reserved
	 * 31:16	vlan/pri
	 */
	cmd.flags = 0;
	if ((attr->sq_sig_all >> 4) & 1) {
		cmd.flags |= FL_PACKED_MODE;
		rqp->fl.packed = 1;
	}
	if ((attr->sq_sig_all >> 5) & 1) {
		cmd.flags |= FL_CONG_DROP_MODE;
	}
	cmd.port = (attr->sq_sig_all >> 8) & 0xf; /* XXX */
	cmd.vlan_pri = (attr->sq_sig_all >> 16) & 0xffff; /* XXX */
	cmd.nfids = (attr->sq_sig_all >> 1) & 7;
	resp.fl_bar2_key = 1;
	ret = ibv_cmd_create_qp(pd, &rqp->ibv_qp, attr, &cmd.ibv_req,
				sizeof cmd, &resp.ibv_resp, sizeof resp);
	if (ret)
		goto err2;

	/*
	 * Verify that the driver filled in resp.fl_bar2_key.  This
	 * ensures the driver supports new non-kdb/kgts usage for T5 without
	 * having to bump the ABI for that change.
	 */
	if (!dev_is_t4(dev) && resp.fl_bar2_key == 1) {
		fprintf(stderr, "libcxgb4 FATAL ERROR: downlevel iw_cxgb4 "
			"module.  Cannot support WD queues with this driver/lib"
			" combination.  Update your drivers!\n");
		goto err2;
	}
	
	rqp->rhp = dev;
	rqp->txq.size = resp.txq_size;
	rqp->txq.memsize = resp.txq_memsize;
	rqp->txq.qid = resp.txq_id;
	rqp->fl.size = resp.fl_size;
	rqp->fl.memsize = resp.fl_memsize;
	rqp->fl.qid = resp.fl_id;
	rqp->iq.size = resp.iq_size;
	rqp->iq.memsize = resp.iq_memsize;
	rqp->iq.gen = 1;
	rqp->iq.qid = resp.iq_id;
	rqp->txq.pf = resp.pf;
	rqp->txq.tx_chan = resp.tx_chan;
	rqp->txq.flags = resp.flags & C4IW_QPF_ONCHIP ? T4_SQ_ONCHIP : 0;
	rqp->txq.vlan_pri = cmd.vlan_pri;
	rqp->fid = resp.fid;
	if ((cmd.vlan_pri & 0xfff) != 0xfff)
		rqp->txq.flags |= T4_SQ_VLAN;
	pthread_spin_init(&rqp->lock, PTHREAD_PROCESS_PRIVATE);

	rqp->txq.queue = mmap(NULL, rqp->txq.memsize,
			     PROT_WRITE, MAP_SHARED,
			     pd->context->cmd_fd, resp.txq_key);
	if (rqp->txq.queue == MAP_FAILED)
		goto err3;
	PDBG("%s txq qid %u txq.size %u, txq.memsize %u\n", __func__,
	     rqp->txq.qid, (unsigned)rqp->txq.size, (unsigned)rqp->txq.memsize);

	if (!attr->srq) {
		rqp->iq.queue = mmap(NULL, rqp->iq.memsize,
				     PROT_WRITE, MAP_SHARED,
				     pd->context->cmd_fd, resp.iq_key);
		if (rqp->iq.queue == MAP_FAILED)
			goto err4;
		PDBG("%s iq qid %u iq.size %u, iq.memsize %u\n", __func__,
		     rqp->iq.qid, (unsigned)rqp->iq.size, (unsigned)rqp->iq.memsize);

		rqp->fl.queue = mmap(NULL, rqp->fl.memsize,
				     PROT_WRITE, MAP_SHARED,
				     pd->context->cmd_fd, resp.fl_key);
		if (rqp->fl.queue == MAP_FAILED)
			goto err5;
		PDBG("%s fl qid %u fl.size %u, fl.memsize %u\n", __func__,
		     rqp->fl.qid, (unsigned)rqp->fl.size, (unsigned)rqp->fl.memsize);
	}

	if (dev_is_t4(rqp->rhp)) {
		rqp->fl.db = mmap(NULL, c4iw_page_size,
				     PROT_WRITE, MAP_SHARED,
				     pd->context->cmd_fd, resp.db_key);
		if (rqp->fl.db == MAP_FAILED)
			goto err6;

		rqp->txq.db = rqp->fl.db;
		rqp->iq.gts = rqp->fl.db + 1;
		rqp->iq.bar2_qid = rqp->iq.qid;
		rqp->fl.bar2_qid = rqp->fl.qid;
		if (t4_txq_onchip(&rqp->txq)) {
			rqp->txq.ma_sync = mmap(NULL, c4iw_page_size, PROT_WRITE,
						  MAP_SHARED, pd->context->cmd_fd,
						  resp.ma_sync_key);
			if (rqp->txq.ma_sync == MAP_FAILED)
				goto err7;
			rqp->txq.ma_sync += (A_PCIE_MA_SYNC & (c4iw_page_size - 1));
		}
	} else {
		unsigned long segment_offset;

		rqp->qid_mask = rchp->cq.qid_mask;
		rqp->txq.db = mmap(NULL, c4iw_page_size, PROT_WRITE,
					  MAP_SHARED, pd->context->cmd_fd,
					  resp.txq_bar2_key);
		if (rqp->txq.db == MAP_FAILED)
			goto err8;

		segment_offset = 128 * (rqp->txq.qid & rqp->qid_mask);
		if (segment_offset < c4iw_page_size) {
			rqp->txq.db += segment_offset / 4;
			rqp->txq.wc_reg_available = 1;
		} else
			rqp->txq.bar2_qid = rqp->txq.qid & rqp->qid_mask;
		rqp->txq.db += 2;

		if (!attr->srq) {
			rqp->fl.db = mmap(NULL, c4iw_page_size, PROT_WRITE,
						  MAP_SHARED, pd->context->cmd_fd,
						  resp.fl_bar2_key);
			if (rqp->fl.db == MAP_FAILED)
				goto err9;
			rqp->fl.db += 2;
			rqp->fl.bar2_qid = rqp->fl.qid & rqp->qid_mask;

			rqp->iq.gts = mmap(NULL, c4iw_page_size, PROT_WRITE,
						  MAP_SHARED, pd->context->cmd_fd,
						  resp.iq_bar2_key);
			if (rqp->iq.gts == MAP_FAILED)
				goto err10;
			rqp->iq.gts += 3;
			rqp->iq.bar2_qid = rqp->iq.qid & rqp->qid_mask;
		}
	}
	rqp->txq.sw_queue = calloc(rqp->txq.size, sizeof *rqp->txq.sw_queue);
	if (!rqp->txq.sw_queue)
		goto err11;

	if (!attr->srq) {
		rqp->fl.sw_queue = calloc(rqp->fl.size, sizeof(uint64_t));
		if (!rqp->fl.sw_queue)
			goto err12;
	}

	if (ctx->status_page_size)
		rqp->fl.db_offp = &ctx->status_page->db_off;
	else
		rqp->fl.db_offp = &((struct t4_status_page *)
				   &rqp->fl.queue[rqp->fl.size])->db_off;

	rqp->sq_sig_all = attr->sq_sig_all & 1;
	rqp->rcq = rchp;
	rqp->scq = schp;
	if (attr->srq) {
		rqp->srq = to_c4iw_raw_srq(attr->srq);
		rchp->iq = &rqp->srq->iq;
	} else {
		rchp->iq = &rqp->iq;
	}

	pthread_spin_lock(&dev->lock);
	dev->qpid2ptr[rqp->txq.qid] = (void *)rqp;
	dev->fid2ptr[rqp->fid] = rqp;
	dev->fid2ptr[rqp->fid + dev->nfids] = rqp;
	pthread_spin_unlock(&dev->lock);
	return &rqp->ibv_qp;
err12:
	free(rqp->txq.sw_queue);
err11:
	if (!dev_is_t4(rqp->rhp) && !attr->srq)
		munmap((void *)MASKED(rqp->iq.gts), c4iw_page_size);
err10:
	if (!dev_is_t4(rqp->rhp) && !attr->srq)
		munmap((void *)MASKED(rqp->fl.db), c4iw_page_size);
err9:
	if (!dev_is_t4(rqp->rhp))
		munmap((void *)MASKED(rqp->txq.db), c4iw_page_size);
err8:
	if (t4_txq_onchip(&rqp->txq))
		munmap((void *)MASKED(rqp->txq.ma_sync), c4iw_page_size);
err7:
	if (dev_is_t4(rqp->rhp))
		munmap(MASKED(rqp->fl.db), c4iw_page_size);
err6:
	if (!attr->srq)
		munmap((void *)rqp->fl.queue, rqp->fl.memsize);
err5:
	if (!attr->srq)
		munmap((void *)rqp->iq.queue, rqp->iq.memsize);
err4:
	munmap((void *)rqp->txq.queue, rqp->txq.memsize);
err3:
	(void)ibv_cmd_destroy_qp(&rqp->ibv_qp);
err2:
	free(rqp);
err1:
	return NULL;
}

static struct ibv_qp *create_rc_qp(struct ibv_pd *pd,
				struct ibv_qp_init_attr *attr)
{
	printf("DEBUG create_rc_qp: Enter\n");
	struct ibv_create_qp cmd;
	struct c4iw_create_qp_resp resp;
	struct c4iw_qp *qhp;
	struct c4iw_dev *dev = to_c4iw_dev(pd->context->device);
	struct c4iw_context *ctx = to_c4iw_context(pd->context);
	int ret;
	void *dbva;

	PDBG("%s enter qp\n", __func__);
	qhp = calloc(1, sizeof *qhp);
	if (!qhp)
		goto err1;

	ret = ibv_cmd_create_qp(pd, &qhp->ibv_qp, attr, &cmd,
				sizeof cmd, &resp.ibv_resp, sizeof resp);
	if (ret)
		goto err2;

	PDBG("%s sqid 0x%x sq key %" PRIx64 " sq db/gts key %" PRIx64
	       " rqid 0x%x rq key %" PRIx64 " rq db/gts key %" PRIx64
	       " qid_mask 0x%x\n",
		__func__,
		resp.sqid, resp.sq_key, resp.sq_db_gts_key,
		resp.rqid, resp.rq_key, resp.rq_db_gts_key, resp.qid_mask);

	qhp->wq.qid_mask = resp.qid_mask;
	qhp->rhp = dev;
	qhp->wq.sq.qid = resp.sqid;
	qhp->wq.sq.size = resp.sq_size;
	qhp->wq.sq.memsize = resp.sq_memsize;
	qhp->wq.sq.flags = resp.flags & C4IW_QPF_ONCHIP ? T4_SQ_ONCHIP : 0;
	if (resp.flags & C4IW_QPF_WRITE_W_IMM)
		qhp->wq.sq.flags |= T4_SQ_WRITE_W_IMM;
	qhp->wq.sq.flush_cidx = -1;
	qhp->srq = to_c4iw_srq(attr->srq);
	qhp->wq.rq.msn = 1;
	if (!attr->srq) {
		qhp->wq.rq.qid = resp.rqid;
		qhp->wq.rq.size = resp.rq_size;
		qhp->wq.rq.memsize = resp.rq_memsize;
	}
	pthread_spin_init(&qhp->lock, PTHREAD_PROCESS_PRIVATE);

	dbva = mmap(NULL, c4iw_page_size, PROT_WRITE, MAP_SHARED,
		    pd->context->cmd_fd, resp.sq_db_gts_key);
	if (dbva == MAP_FAILED)
		goto err3;
	qhp->wq.sq.udb = dbva;
	if (!dev_is_t4(qhp->rhp)) {
		unsigned long segment_offset = 128 * (qhp->wq.sq.qid & qhp->wq.qid_mask);

		if (segment_offset < c4iw_page_size) {
			qhp->wq.sq.udb += segment_offset / 4;
			qhp->wq.sq.wc_reg_available = 1;
		} else
			qhp->wq.sq.bar2_qid = qhp->wq.sq.qid & qhp->wq.qid_mask;
		qhp->wq.sq.udb += 2;
	}

	qhp->wq.sq.queue = mmap(NULL, qhp->wq.sq.memsize,
			    PROT_WRITE, MAP_SHARED,
			    pd->context->cmd_fd, resp.sq_key);
	if (qhp->wq.sq.queue == MAP_FAILED)
		goto err4;

	if (!attr->srq) {
		dbva = mmap(NULL, c4iw_page_size, PROT_WRITE, MAP_SHARED,
			    pd->context->cmd_fd, resp.rq_db_gts_key);
		if (dbva == MAP_FAILED)
			goto err5;
		qhp->wq.rq.udb = dbva;
		if (!dev_is_t4(qhp->rhp)) {
			unsigned long segment_offset = 128 * (qhp->wq.rq.qid & qhp->wq.qid_mask);

			if (segment_offset < c4iw_page_size) {
				qhp->wq.rq.udb += segment_offset / 4;
				qhp->wq.rq.wc_reg_available = 1;
			} else
				qhp->wq.rq.bar2_qid = qhp->wq.rq.qid & qhp->wq.qid_mask;
			qhp->wq.rq.udb += 2;
		}
		qhp->wq.rq.queue = mmap(NULL, qhp->wq.rq.memsize,
				    PROT_WRITE, MAP_SHARED,
				    pd->context->cmd_fd, resp.rq_key);
		if (qhp->wq.rq.queue == MAP_FAILED)
			goto err6;
	}

	qhp->wq.sq.sw_sq = calloc(qhp->wq.sq.size, sizeof (struct t4_swsqe));
	if (!qhp->wq.sq.sw_sq)
		goto err7;

	if (!attr->srq) {
		qhp->wq.rq.sw_rq = calloc(qhp->wq.rq.size, sizeof (struct t4_swrqe));
		if (!qhp->wq.rq.sw_rq)
			goto err8;
	}

	if (t4_sq_onchip(&qhp->wq)) {
		qhp->wq.sq.ma_sync = mmap(NULL, c4iw_page_size, PROT_WRITE,
					  MAP_SHARED, pd->context->cmd_fd,
					  resp.ma_sync_key);
		if (qhp->wq.sq.ma_sync == MAP_FAILED)
			goto err9;
		qhp->wq.sq.ma_sync += (A_PCIE_MA_SYNC & (c4iw_page_size - 1));
	}

	if (ctx->status_page_size) {
		qhp->wq.db_offp = &ctx->status_page->db_off;
	} else {
		if (!attr->srq)
			qhp->wq.db_offp = 
				&qhp->wq.rq.queue[qhp->wq.rq.size].status.db_off;
	}
	if (!attr->srq)
		qhp->wq.qp_errp = &qhp->wq.rq.queue[qhp->wq.rq.size].status.qp_err;
	else {
		qhp->wq.qp_errp = &qhp->wq.sq.queue[qhp->wq.sq.size].status.qp_err;
		qhp->wq.srqidxp = &qhp->wq.sq.queue[qhp->wq.sq.size].status.srqidx;
	}

	PDBG("%s sq dbva %p sq qva %p sq depth %u sq memsize %lu "
	       " rq dbva %p rq qva %p rq depth %u rq memsize %lu\n",
	     __func__,
	     qhp->wq.sq.udb, qhp->wq.sq.queue,
	     qhp->wq.sq.size, qhp->wq.sq.memsize,
	     qhp->wq.rq.udb, qhp->wq.rq.queue,
	     qhp->wq.rq.size, qhp->wq.rq.memsize);

	qhp->sq_sig_all = attr->sq_sig_all;

	pthread_spin_lock(&dev->lock);
	dev->qpid2ptr[qhp->wq.sq.qid] = qhp;
	pthread_spin_unlock(&dev->lock);
	INC_STAT(qp);
	return &qhp->ibv_qp;
err9:
	if (!attr->srq)
		free(qhp->wq.rq.sw_rq);
err8:
	free(qhp->wq.sq.sw_sq);
err7:
	if (!attr->srq)
		munmap((void *)qhp->wq.rq.queue, qhp->wq.rq.memsize);
err6:
	if (!attr->srq)
		munmap(MASKED(qhp->wq.rq.udb), c4iw_page_size);
err5:
	munmap((void *)qhp->wq.sq.queue, qhp->wq.sq.memsize);
err4:
	munmap(MASKED(qhp->wq.sq.udb), c4iw_page_size);
err3:
	(void)ibv_cmd_destroy_qp(&qhp->ibv_qp);
err2:
	free(qhp);
err1:
	return NULL;
}

static struct ibv_qp *create_qp(struct ibv_pd *pd,
				struct ibv_qp_init_attr *attr)
{
	struct ibv_qp *qp = NULL;

	switch (attr->qp_type) {
	case IBV_QPT_RC:
		qp = create_rc_qp(pd, attr);
		break;
	case IBV_QPT_RAW_ETH:
		qp = create_raw_qp(pd, attr);
		break;
	default:
		break;
	}
	return qp;
}

//// Old c4iw_create_qp for reference
/*
struct ibv_qp *c4iw_create_qp(struct ibv_pd *pd,
				     struct ibv_qp_init_attr *attr)
{
	printf("DEBUG c4iw_create_qp: Enter\n");
	struct c4iw_dev *dev = to_c4iw_dev(pd->context->device);

	if (dev->abi_version == 0)
		return create_qp_v0(pd, attr);
	return create_qp(pd, attr);
}
*/
////

struct ibv_qp *c4iw_create_qp(struct ibv_pd *pd,
				     struct ibv_qp_init_attr *attr)
{
	printf("DEBUG c4iw_create_qp: Enter\n");
	struct c4iw_dev *dev = to_c4iw_dev(pd->context->device);

	if (dev->abi_version == 0)
		return create_qp_v0(pd, attr);

	//// Modified logic goes here
	//// create custom cq used for two-sided rdma message splitting
	struct ibv_comp_channel *send_channel = ibv_create_comp_channel(pd->context);
	struct ibv_comp_channel *recv_channel = ibv_create_comp_channel(pd->context);
	struct ibv_comp_channel *channel2 = ibv_create_comp_channel(pd->context);
	struct ibv_cq *split_send_cq = c4iw_create_cq(pd->context, SPLIT_MAX_CQE, send_channel, 0);
	struct ibv_cq *split_recv_cq = c4iw_create_cq(pd->context, SPLIT_MAX_CQE, recv_channel, 0);
	struct ibv_cq *split_cq2 = c4iw_create_cq(pd->context, SPLIT_MAX_CQE, channel2, 0);

	/// create a custom qp for our own use 
	struct ibv_qp_init_attr split_init_attr, split_init_attr2;
	memset(&split_init_attr, 0, sizeof(struct ibv_qp_init_attr));
	split_init_attr.send_cq = split_send_cq;
	split_init_attr.recv_cq = split_recv_cq;
	split_init_attr.cap.max_send_wr  = SPLIT_MAX_SEND_WR;
	split_init_attr.cap.max_recv_wr  = SPLIT_MAX_RECV_WR;
	split_init_attr.cap.max_send_sge = 1;
	split_init_attr.cap.max_recv_sge = 1;
	split_init_attr.cap.max_inline_data = 100;	// probably not going to use inline there
	split_init_attr.qp_type = IBV_QPT_RC;
	split_init_attr.isSmall = 1;
	memcpy(&split_init_attr2, &split_init_attr, sizeof(struct ibv_qp_init_attr));
	split_init_attr2.send_cq = split_cq2;
	split_init_attr2.recv_cq = split_cq2;

	struct ibv_qp 		*qp;
	struct ibv_qp 		*split_qp;
	struct ibv_qp 		*split_qp2;
	split_qp2 = create_qp(pd, &split_init_attr2);
	if (split_qp2 == NULL) {
		printf("Create split qp2 failed. %s\n", strerror(errno));
	}
	printf("DEBUG c4iw_create_qp: split_qp->qpn = %06x\n", split_qp2->qp_num);

	split_qp = create_qp(pd, &split_init_attr);
	if (split_qp == NULL) {
		printf("Create split qp failed. %s\n", strerror(errno));
	}

	printf("DEBUG c4iw_create_qp: split_qp->qpn = %06x\n", split_qp->qp_num);
	//// Now create the user's qp
	qp = create_qp(pd, attr);
	if (qp == NULL) {
		printf("Create user qp failed. %s\n", strerror(errno));
	}
	printf("DEBUG c4iw_create_qp: orig_qp->qpn = %06x\n", qp->qp_num);
	//// store split_qp & split_cq inside the user's qp



	//// return user's qp
	return qp;
}

static void reset_qp(struct c4iw_qp *qhp)
{
	PDBG("%s enter qp %p\n", __func__, qhp);
	qhp->wq.sq.cidx = 0;
	qhp->wq.sq.wq_pidx = qhp->wq.sq.pidx = qhp->wq.sq.in_use = 0;
	qhp->wq.rq.cidx = qhp->wq.rq.pidx = qhp->wq.rq.in_use = 0;
	qhp->wq.sq.oldest_read = NULL;
	memset(qhp->wq.sq.queue, 0, qhp->wq.sq.memsize);
	memset(qhp->wq.rq.queue, 0, qhp->wq.rq.memsize);
}

static int modify_rc_qp(struct ibv_qp *ibqp, struct ibv_qp_attr *attr,
		   ENUM_IBV_QP_ATTR_MASK attr_mask)
{
	printf("DEBUG modify_rc_qp: Enter\n");
	struct ibv_modify_qp cmd;
	struct c4iw_qp *qhp = to_c4iw_qp(ibqp);
	int ret;

	PDBG("%s enter qp %p new state %d\n", __func__, ibqp, attr_mask & IBV_QP_STATE ? attr->qp_state : -1);
	pthread_spin_lock(&qhp->lock);
	if (t4_wq_in_error(&qhp->wq))
		c4iw_flush_qp(qhp);
	ret = ibv_cmd_modify_qp(ibqp, attr, attr_mask, &cmd, sizeof cmd);
	if (!ret && (attr_mask & IBV_QP_STATE) && attr->qp_state == IBV_QPS_RESET)
		reset_qp(qhp);
	pthread_spin_unlock(&qhp->lock);
	return ret;
}

static int modify_raw_qp(struct ibv_qp *ibqp, struct ibv_qp_attr *attr,
		   ENUM_IBV_QP_ATTR_MASK attr_mask)
{
	struct ibv_modify_qp cmd;
	struct c4iw_raw_qp *rqp = to_c4iw_raw_qp(ibqp);
	int ret;

	PDBG("%s enter qp %p new state %d\n", __func__, ibqp,
	     attr_mask & IBV_QP_STATE ? attr->qp_state : -1);
	pthread_spin_lock(&rqp->lock);
	ret = ibv_cmd_modify_qp(ibqp, attr, attr_mask, &cmd, sizeof cmd);
	pthread_spin_unlock(&rqp->lock);
	return ret;
}

int c4iw_modify_qp(struct ibv_qp *ibqp, struct ibv_qp_attr *attr,
		 ENUM_IBV_QP_ATTR_MASK attr_mask)
{
	int ret;

	switch (ibqp->qp_type) {
	case IBV_QPT_RC:
		ret = modify_rc_qp(ibqp, attr, attr_mask);
		break;
	case IBV_QPT_RAW_ETH:
		ret = modify_raw_qp(ibqp, attr, attr_mask);
		break;
	default:
		assert(1);
		ret = EINVAL;
	}
	return ret;
}

static int destroy_raw_qp(struct ibv_qp *ibqp)
{
	int ret;
	struct c4iw_raw_qp *rqp = to_c4iw_raw_qp(ibqp);
	struct c4iw_dev *dev = to_c4iw_dev(ibqp->context->device);

	PDBG("%s enter qp %p\n", __func__, ibqp);
#ifdef notyet
	pthread_spin_lock(&rqp->lock);
	c4iw_flush_raw_qp(rqp);
	pthread_spin_unlock(&rqp->lock);
#endif
	ret = ibv_cmd_destroy_qp(ibqp);
	if (ret)
		return ret;
	if (t4_txq_onchip(&rqp->txq))
		munmap((void *)((unsigned long)
		       rqp->txq.ma_sync & c4iw_page_mask), c4iw_page_size);
	munmap(MASKED(rqp->txq.db), c4iw_page_size);
	if (!rqp->srq) {
		if (!dev_is_t4(dev)) {
			munmap(MASKED(rqp->fl.db), c4iw_page_size);
			munmap(MASKED(rqp->iq.gts), c4iw_page_size);
		}
		munmap(rqp->fl.queue, rqp->fl.memsize);
		munmap(rqp->iq.queue, rqp->iq.memsize);
	}
	munmap(rqp->txq.queue, rqp->txq.memsize);

	pthread_spin_lock(&dev->lock);
	dev->qpid2ptr[rqp->txq.qid] = NULL;
	dev->fid2ptr[rqp->fid] = NULL;
	dev->fid2ptr[rqp->fid + dev->nfids] = NULL;
	pthread_spin_unlock(&dev->lock);

	rqp->rcq->iq = NULL;
	if (!rqp->srq)
		free(rqp->fl.sw_queue);
	free(rqp->txq.sw_queue);
	free(rqp);
	return 0;
}

static int destroy_rc_qp(struct ibv_qp *ibqp)
{
	int ret;
	struct c4iw_qp *qhp = to_c4iw_qp(ibqp);
	struct c4iw_dev *dev = to_c4iw_dev(ibqp->context->device);

	PDBG("%s enter qp %p\n", __func__, ibqp);
	pthread_spin_lock(&qhp->lock);
	c4iw_flush_qp(qhp);
	pthread_spin_unlock(&qhp->lock);

	ret = ibv_cmd_destroy_qp(ibqp);
	if (ret) {
		return ret;
	}
	if (t4_sq_onchip(&qhp->wq)) {
		qhp->wq.sq.ma_sync -= (A_PCIE_MA_SYNC & (c4iw_page_size - 1));
		munmap((void *)qhp->wq.sq.ma_sync, c4iw_page_size);
	}
	munmap(MASKED(qhp->wq.sq.udb), c4iw_page_size);
	munmap(qhp->wq.sq.queue, qhp->wq.sq.memsize);
	if (!qhp->srq) {
		munmap(MASKED(qhp->wq.rq.udb), c4iw_page_size);
		munmap(qhp->wq.rq.queue, qhp->wq.rq.memsize);
	}

	pthread_spin_lock(&dev->lock);
	dev->qpid2ptr[qhp->wq.sq.qid] = NULL;
	pthread_spin_unlock(&dev->lock);

	if (!qhp->srq)
		free(qhp->wq.rq.sw_rq);
	free(qhp->wq.sq.sw_sq);
	free(qhp);
	return 0;
}

int c4iw_destroy_qp(struct ibv_qp *ibqp)
{
	int ret;

	switch (ibqp->qp_type) {
	case IBV_QPT_RC:
		ret = destroy_rc_qp(ibqp);
		break;
	case IBV_QPT_RAW_ETH:
		ret = destroy_raw_qp(ibqp);
		break;
	default:
		assert(1);
		ret = EINVAL;
	}
	return ret;
}

static int query_rc_qp(struct ibv_qp *ibqp, struct ibv_qp_attr *attr,
		       ENUM_IBV_QP_ATTR_MASK attr_mask, struct ibv_qp_init_attr *init_attr)
{
	struct ibv_query_qp cmd;
	struct c4iw_qp *qhp = to_c4iw_qp(ibqp);
	int ret;

	pthread_spin_lock(&qhp->lock);
	if (t4_wq_in_error(&qhp->wq))
		c4iw_flush_qp(qhp);
	ret = ibv_cmd_query_qp(ibqp, attr, attr_mask, init_attr, &cmd, sizeof cmd);
	pthread_spin_unlock(&qhp->lock);
	return ret;
}

int c4iw_query_qp(struct ibv_qp *ibqp, struct ibv_qp_attr *attr,
		  ENUM_IBV_QP_ATTR_MASK attr_mask, struct ibv_qp_init_attr *init_attr)
{
	int ret = 0;
	struct c4iw_raw_qp *rqp = to_c4iw_raw_qp(ibqp);

	switch (ibqp->qp_type) {
	case IBV_QPT_RC:
		ret = query_rc_qp(ibqp, attr, attr_mask, init_attr);
		break;
	case IBV_QPT_RAW_ETH:
		attr->rq_psn = rqp->fid; /* XXX */
		if (ibqp->srq) {
			attr->sq_psn = to_c4iw_raw_srq(ibqp->srq)->iq.qid;
		} else {
			attr->sq_psn = rqp->iq.qid;
		}
		break;
	default:
		assert(1);
		ret = EINVAL;
	}
	return ret;
}

struct ibv_ah *c4iw_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr)
{
	return NULL;
}

int c4iw_destroy_ah(struct ibv_ah *ah)
{
	return ENOSYS;
}

int c4iw_attach_mcast(struct ibv_qp *ibqp, CONST union ibv_gid *gid,
		      uint16_t lid)
{
	struct c4iw_raw_qp *rqp = to_c4iw_raw_qp(ibqp);
	int ret;

	pthread_spin_lock(&rqp->lock);
	ret = ibv_cmd_attach_mcast(ibqp, gid, lid);
	pthread_spin_unlock(&rqp->lock);
	return ret;
}

int c4iw_detach_mcast(struct ibv_qp *ibqp, CONST union ibv_gid *gid,
		      uint16_t lid)
{
	struct c4iw_raw_qp *rqp = to_c4iw_raw_qp(ibqp);
	int ret;

	pthread_spin_lock(&rqp->lock);
	ret = ibv_cmd_detach_mcast(ibqp, gid, lid);
	pthread_spin_unlock(&rqp->lock);
	return ret;
}

void c4iw_async_event(struct ibv_async_event *event)
{
	PDBG("%s type %d obj %p\n", __func__, event->event_type,
	event->element.cq);

	switch (event->event_type) {
	case IBV_EVENT_CQ_ERR:
		break;
	case IBV_EVENT_QP_FATAL:
	case IBV_EVENT_QP_REQ_ERR:
	case IBV_EVENT_QP_ACCESS_ERR:
	case IBV_EVENT_PATH_MIG_ERR: {
		struct c4iw_qp *qhp = to_c4iw_qp(event->element.qp);
		pthread_spin_lock(&qhp->lock);
		c4iw_flush_qp(qhp);
		pthread_spin_unlock(&qhp->lock);
		break;
	}
	case IBV_EVENT_SQ_DRAINED:
	case IBV_EVENT_PATH_MIG:
	case IBV_EVENT_COMM_EST:
	case IBV_EVENT_QP_LAST_WQE_REACHED:
	default:
		break;
	}
}
