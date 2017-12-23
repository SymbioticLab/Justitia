/*
 * Copyright (c) 2007 Cisco, Inc.  All rights reserved.
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
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
/* Added for reg_mr mmap munmap system calls */
#include <sys/mman.h>
#include <sys/time.h>
#include <sched.h>
#include <glob.h>
#include "mlx4.h"
#include "mlx4-abi.h"
#include "mlx4_exp.h"
#include "wqe.h"

#define SHARED_MR_PROC_DIR_NAME "/proc/driver/mlx4_ib/mrs"
#define FPATH_MAX 128

/* isolation */
#include "verbs_pacer.h"
/* end */

int __mlx4_query_device(uint64_t raw_fw_ver,
			struct ibv_device_attr *attr)
{
	unsigned major, minor, sub_minor;

	major     = (raw_fw_ver >> 32) & 0xffff;
	minor     = (raw_fw_ver >> 16) & 0xffff;
	sub_minor = raw_fw_ver & 0xffff;

	snprintf(attr->fw_ver, sizeof attr->fw_ver,
		 "%d.%d.%03d", major, minor, sub_minor);

	return 0;
}

int mlx4_query_device(struct ibv_context *context, struct ibv_device_attr *attr)
{
	struct ibv_query_device cmd;
	uint64_t raw_fw_ver;
	int ret;

	read_init_vars(to_mctx(context));
	ret = ibv_cmd_query_device(context, attr, &raw_fw_ver, &cmd,
				   sizeof(cmd));
	if (ret)
		return ret;

	return __mlx4_query_device(raw_fw_ver, attr);
}

#define READL(ptr) (*((uint32_t *)(ptr)))

static int mlx4_read_clock(struct ibv_context *context, uint64_t *cycles)
{
	unsigned int clockhi, clocklo, clockhi1;
	int i;
	struct mlx4_context *ctx = to_mctx(context);

	if (ctx->hca_core_clock == NULL)
		return -EOPNOTSUPP;

	for (i = 0; i < 10; i++) {
		clockhi = ntohl(READL(ctx->hca_core_clock));
		clocklo = ntohl(READL(ctx->hca_core_clock + 4));
		clockhi1 = ntohl(READL(ctx->hca_core_clock));
		if (clockhi == clockhi1)
			break;
	}

	if (clocklo == 0)
		clockhi++;

	*cycles = (uint64_t) clockhi << 32 | (uint64_t) clocklo;

	return 0;
}
int mlx4_query_values(struct ibv_context *context, int q_values,
		      struct ibv_exp_values *values)
{
	struct mlx4_context *ctx = to_mctx(context);
	uint64_t cycles;
	int err;
	uint32_t comp_mask = values->comp_mask;

	values->comp_mask = 0;

	if (q_values & (IBV_EXP_VALUES_HW_CLOCK | IBV_EXP_VALUES_HW_CLOCK_NS)) {
		err = mlx4_read_clock(context, &cycles);
		if (!err) {
			if (comp_mask & IBV_EXP_VALUES_HW_CLOCK) {
				values->hwclock = cycles;
				values->comp_mask |= IBV_EXP_VALUES_HW_CLOCK;
			}
			if (q_values & IBV_EXP_VALUES_HW_CLOCK_NS) {
				if (comp_mask & IBV_EXP_VALUES_HW_CLOCK_NS) {
					values->hwclock_ns =
						((uint64_t)values->hwclock *
						 ctx->core_clk.mult)
						>> ctx->core_clk.shift;
					values->comp_mask |= IBV_EXP_VALUES_HW_CLOCK_NS;
				}
			}
		}
	}
	return 0;
}

int mlx4_query_port(struct ibv_context *context, uint8_t port,
		     struct ibv_port_attr *attr)
{
	struct ibv_query_port cmd;
	int err;

	read_init_vars(to_mctx(context));
	err = ibv_cmd_query_port(context, port, attr, &cmd, sizeof(cmd));
	if (!err && port <= MLX4_PORTS_NUM && port > 0) {
		struct mlx4_context *mctx = to_mctx(context);
		if (!mctx->port_query_cache[port - 1].valid) {
			mctx->port_query_cache[port - 1].link_layer =
				attr->link_layer;
			mctx->port_query_cache[port - 1].caps =
				attr->port_cap_flags;
			mctx->port_query_cache[port - 1].valid = 1;
		}
	}

	return err;
}

struct ibv_pd *mlx4_alloc_pd(struct ibv_context *context)
{
	struct ibv_alloc_pd       cmd;
	struct mlx4_alloc_pd_resp resp;
	struct mlx4_pd		 *pd;

	read_init_vars(to_mctx(context));
	pd = malloc(sizeof *pd);
	if (!pd)
		return NULL;

	if (ibv_cmd_alloc_pd(context, &pd->ibv_pd, &cmd, sizeof cmd,
			     &resp.ibv_resp, sizeof resp)) {
		free(pd);
		return NULL;
	}

	pd->pdn = resp.pdn;

	//// store ibv_context pointer to later create custom cq into 
	//pd->context = context;
	// Actually no need, ibv_pd already contains ibv_context
	////
	return &pd->ibv_pd;
}

int mlx4_free_pd(struct ibv_pd *pd)
{
	int ret;

	//printf("calling free pd?\n");
	//// cleanup split_fc_mr
	if (mlx4_dereg_mr(to_mpd(pd)->split_fc_mr)) {
		printf("error dereg split_fc_mr.\n");
	}

	ret = ibv_cmd_dealloc_pd(pd);
	//printf("DEBUG free pd: ret = %d\n", ret);
	if (ret)
		return ret;

	free(to_mpd(pd));
	return 0;
}


static void mlx4_free_mr(struct mlx4_mr *mlx4_mr)
{
	/*  mr address was allocated in speical mode - freed accordingly */
	if (mlx4_mr->allocation_flags & IBV_EXP_ACCESS_ALLOCATE_MR ||
		mlx4_mr->shared_mr)
		mlx4_free_buf(&(mlx4_mr->buf));

	/* Finally we free the structure itself  */
	free(mlx4_mr);
}


static void *mlx4_get_contiguous_alloc_fallback(struct mlx4_buf *buf,
	struct ibv_pd *pd, size_t length)
{

	/* We allocate as fallback mode non contiguous pages*/
	if (mlx4_alloc_buf(
			buf,
			align(length, to_mdev(pd->context->device)->page_size),
			to_mdev(pd->context->device)->page_size))
		return NULL;

	return buf->buf;
}


/*  We'll call mmap on mlx4_ib module to achieve this task */
static void *mlx4_get_contiguous_alloc(struct mlx4_buf *mlx4_buf,
						struct ibv_pd *pd,
						size_t length,
						void *contig_addr)
{
	size_t alloc_length;
	int page_size;
	int mr_no_allocator = 0;
	int mr_force_contig_pages = 0;
	enum mlx4_alloc_type alloc_type;

	mlx4_get_alloc_type(pd->context, MLX4_MR_PREFIX, &alloc_type,
			    MLX4_ALLOC_TYPE_ALL);

	if (alloc_type == MLX4_ALLOC_TYPE_CONTIG)
		mr_force_contig_pages = 1;
	else if (alloc_type == MLX4_ALLOC_TYPE_ANON)
		mr_no_allocator = 1;

	/* For benchmarking purposes we apply an option to turn off continuous
	     allocator based on environment variable
	*/
	if (mr_no_allocator)
		return mlx4_get_contiguous_alloc_fallback(mlx4_buf, pd,
				length);

	page_size = to_mdev(pd->context->device)->page_size;
	alloc_length = (contig_addr ? length : align(length, page_size));
	if (!(mlx4_alloc_buf_contig(to_mctx(pd->context),
				mlx4_buf, alloc_length,
				page_size, MLX4_MR_PREFIX, contig_addr)))
		return contig_addr ? contig_addr : mlx4_buf->buf;

	if (mr_force_contig_pages || contig_addr)
		return NULL;

	return mlx4_get_contiguous_alloc_fallback(mlx4_buf,
						pd, length);

}

static int mlx4_get_shared_mr_name(char *in_pattern, char *file_name)
{
	glob_t results;
	int ret;

	ret = glob(in_pattern, 0, NULL, &results);

	if (ret) {
		if (mlx4_trace)
			/* might be some legacy kernel with old mode */
			fprintf(stderr, "mlx4_get_shared_mr_name: glob failed for %s, ret=%d, errno=%d\n",
				in_pattern, ret, errno);
		return ret;
	}

	if (results.gl_pathc > 1) {
		int i;
		int duplicate_name = 1;

		/* we encountered an issue where glob retuned same name twice, we suspect it to be
		  * an issue with glob/procfs. When there is more than one entry check whether all entries
		  * are the same in that case API succeeded and we use first entry name.
		*/
		for (i = 1; i < results.gl_pathc; i++) {
			if (strcmp(results.gl_pathv[0], results.gl_pathv[i])) {
				duplicate_name = 0;
				break;
			}
		}

		if (!duplicate_name) {
			fprintf(stderr, "mlx4_get_shared_mr_name failed for %s, unexpected %lu paths were found\n",
				in_pattern, (unsigned long)(results.gl_pathc));
			for (i = 0; i < results.gl_pathc; i++)
				fprintf(stderr, "mlx4_get_shared_mr_name: path#%d=%s\n", i,
					results.gl_pathv[i]);
			globfree(&results);
			return -EINVAL;
		}
	}

	strncpy(file_name, results.gl_pathv[0], FPATH_MAX);
	file_name[FPATH_MAX - 1] = '\0';
	globfree(&results);
	return 0;
}

struct ibv_mr *mlx4_reg_shared_mr(struct ibv_exp_reg_shared_mr_in *in)
{
	struct ibv_context *context;
	size_t total_size;
	int page_size;
	char shared_mr_file_name[FPATH_MAX];
	char shared_mr_pattern[FPATH_MAX];
	int fd;
	struct stat buffer;
	int status;
	struct ibv_mr *ibv_mr;
	uint64_t shared_flags;
	struct mlx4_mr *mlx4_mr = NULL;
	void *addr = in->addr;
	uint64_t access = in->exp_access;
	struct ibv_exp_reg_mr_in rmr_in;
	int flags;
	int ret;
	int is_writeable_mr = !!(access & (IBV_EXP_ACCESS_REMOTE_WRITE |
			IBV_EXP_ACCESS_LOCAL_WRITE | IBV_EXP_ACCESS_REMOTE_ATOMIC));

	context = in->pd->context;
	page_size = to_mdev(context->device)->page_size;
	sprintf(shared_mr_pattern, "%s/%X.*",
		SHARED_MR_PROC_DIR_NAME, in->mr_handle);

	ret = mlx4_get_shared_mr_name(shared_mr_pattern, shared_mr_file_name);
	if (ret)
		/* For compatability issue trying with legacy name */
		sprintf(shared_mr_file_name, "%s/%X",
			SHARED_MR_PROC_DIR_NAME, in->mr_handle);

	flags = is_writeable_mr ? O_RDWR : O_RDONLY;
	fd = open(shared_mr_file_name, flags);
	if (fd < 0) {
		int counter = 10;
		/* retrying for 1 second before reporting an error */
		while (fd < 0 && counter > 0) {
			usleep(100000);
			printf("DDDDDD RETRIED\n");
			counter--;
			fd = open(shared_mr_file_name, flags);
		}

		if (fd < 0) {
			fprintf(stderr, "mlx4_reg_shared_mr failed open %s errno=%d\n",
				shared_mr_file_name, errno);
			return NULL;
		}
	}

	status = fstat(fd, &buffer);
	if (status) {
		fprintf(stderr,
			"mlx4_reg_shared_mr lstat has failed , errno=%d\n",
			errno);
		goto error;
	}

	total_size = align(buffer.st_size, page_size);

	/* set protection based on access flags input address may be NULL
	     or other recommended address by the application.
	*/
	addr = mmap(addr , total_size,
		    is_writeable_mr ? (PROT_WRITE | PROT_READ) :
				PROT_READ, MAP_SHARED,
				fd,
				0);

	/* On a failure  MAP_FAILED (that is, (void *) -1) is returned*/
	if (addr == MAP_FAILED) {
		fprintf(stderr,
			"mlx4_reg_shared_mr mmap has failed , errno=%d\n",
			errno);
		goto error;
	}

	if (ibv_dontfork_range(addr, total_size)) {
		fprintf(stderr,
			"mlx4_reg_shared_mr dontfork has failed , errno=%d\n",
			errno);
		goto err_unmap;
	}

	if (access & IBV_EXP_ACCESS_NO_RDMA) {
		mlx4_mr = calloc(1, sizeof *mlx4_mr);
		if (!mlx4_mr)
			goto err_dofork;

		mlx4_mr->allocation_flags |= IBV_EXP_ACCESS_NO_RDMA;
		ibv_mr = &(mlx4_mr->ibv_mr);
		ibv_mr->context = in->pd->context;

	} else {
		/* Make sure that  shared access flags are off before
		     calling to reg_mr, otherwise new mr will be shared as well.
		*/
		shared_flags = IBV_EXP_ACCESS_SHARED_MR_USER_READ |
				IBV_EXP_ACCESS_SHARED_MR_USER_WRITE |
				IBV_EXP_ACCESS_SHARED_MR_GROUP_READ |
				IBV_EXP_ACCESS_SHARED_MR_GROUP_WRITE |
				IBV_EXP_ACCESS_SHARED_MR_OTHER_READ |
				IBV_EXP_ACCESS_SHARED_MR_OTHER_WRITE;

		access &= ~shared_flags;
		rmr_in.pd = in->pd;
		rmr_in.addr = addr;
		rmr_in.length = total_size;
		rmr_in.exp_access = access;
		rmr_in.comp_mask = 0;

		ibv_mr = mlx4_exp_reg_mr(&rmr_in);
		if (!ibv_mr)
			goto err_dofork;
	}

	/* file should be closed - not required any more */
	close(fd);

	ibv_mr->length = total_size;
	ibv_mr->addr = addr;
	mlx4_mr = to_mmr(ibv_mr);
	/* We mark this MR as shared one to be handled correctly via dereg_mr*/
	mlx4_mr->shared_mr = 1;
	/* We hook addr & length also internally for further
	     use via dreg_mr.
	*/
	mlx4_mr->buf.buf = addr;
	mlx4_mr->buf.length = total_size;
	return ibv_mr;

err_dofork:
	ibv_dofork_range(addr, total_size);
err_unmap:
	munmap(addr, total_size);
error:
	close(fd);
	return NULL;
}

int mlx4_exp_dereg_mr(struct ibv_mr *mr, struct ibv_exp_dereg_out *out)
{
	struct mlx4_mr *mlx4_mr = to_mmr(mr);

	out->need_dofork = (mlx4_mr->allocation_flags & IBV_EXP_ACCESS_ALLOCATE_MR ||
			    mlx4_mr->shared_mr) ? 0 : 1;

	return mlx4_dereg_mr(mr);
}

struct ibv_xrcd *mlx4_open_xrcd(struct ibv_context *context,
				struct ibv_xrcd_init_attr *attr)
{
	struct ibv_open_xrcd cmd;
	struct ibv_open_xrcd_resp resp;
	struct verbs_xrcd *xrcd;
	int ret;

	xrcd = calloc(1, sizeof *xrcd);
	if (!xrcd)
		return NULL;

	ret = ibv_cmd_open_xrcd(context, xrcd, sizeof(*xrcd), attr,
				&cmd, sizeof cmd, &resp, sizeof resp);
	if (ret)
		goto err;

	return &xrcd->xrcd;

err:
	free(xrcd);
	return NULL;
}

int mlx4_close_xrcd(struct ibv_xrcd *ib_xrcd)
{
	struct verbs_xrcd *xrcd = container_of(ib_xrcd, struct verbs_xrcd, xrcd);
	int ret;

	ret = ibv_cmd_close_xrcd(xrcd);
	if (!ret)
		free(xrcd);

	return ret;
}

struct ibv_mr *mlx4_exp_reg_mr(struct ibv_exp_reg_mr_in *in)
{

	struct mlx4_mr *mlx4_mr;
	struct ibv_reg_mr cmd;
	int ret;
	int cmd_access;
	int is_contig;

	if ((in->comp_mask > IBV_EXP_REG_MR_RESERVED - 1) ||
	    (in->exp_access > IBV_EXP_ACCESS_RESERVED - 1)) {
		errno = EINVAL;
		return NULL;
	}

	mlx4_mr = calloc(1, sizeof *mlx4_mr);
	if (!mlx4_mr)
		return NULL;

	VALGRIND_MAKE_MEM_DEFINED(&in->create_flags, sizeof(in->create_flags));
	is_contig = ((in->exp_access & IBV_EXP_ACCESS_ALLOCATE_MR) && !in->addr) ||
		    ((in->comp_mask & IBV_EXP_REG_MR_CREATE_FLAGS) &&
		     (in->create_flags & IBV_EXP_REG_MR_CREATE_CONTIG));
	/* Here we check whether contigous pages are required and
	    should be allocated internally.
	*/
	if (is_contig) {
		in->addr = mlx4_get_contiguous_alloc(&mlx4_mr->buf, in->pd,
						     in->length, in->addr);
		if (!in->addr) {
			free(mlx4_mr);
			return NULL;
		}

		mlx4_mr->allocation_flags |= IBV_EXP_ACCESS_ALLOCATE_MR;
		/* Hooking the addr on returned pointer for
		     further use by application.
		*/
		mlx4_mr->ibv_mr.addr = in->addr;
	}

	cmd_access = (in->exp_access & (IBV_EXP_START_FLAG - 1)) |
		     (((in->exp_access & (IBV_EXP_ACCESS_RESERVED - 1)) >> IBV_EXP_START_FLAG_LOC) << IBV_EXP_ACCESS_FLAGS_SHIFT);
#ifdef IBV_CMD_REG_MR_HAS_RESP_PARAMS
	{
		struct ibv_reg_mr_resp resp;

		ret = ibv_cmd_reg_mr(in->pd, in->addr, in->length,
				     (uintptr_t) in->addr, cmd_access,
				     &(mlx4_mr->ibv_mr),
				     &cmd, sizeof(cmd),
				     &resp, sizeof(resp));
	}
#else
	ret = ibv_cmd_reg_mr(in->pd, in->addr, in->length,
			     (uintptr_t) in->addr, cmd_access,
			     &(mlx4_mr->ibv_mr),
			     &cmd, sizeof(cmd));
#endif
	if (ret) {
		mlx4_free_mr(mlx4_mr);
		return NULL;
	}

	return &(mlx4_mr->ibv_mr);
}

struct ibv_mr *mlx4_reg_mr(struct ibv_pd *pd, void *addr,
			   size_t length, int access)
{
	struct ibv_exp_reg_mr_in in;

	in.pd = pd;
	in.addr = addr;
	in.length = length;
	in.exp_access = access;
	in.comp_mask = 0;

	return mlx4_exp_reg_mr(&in);
}

int mlx4_rereg_mr(struct ibv_mr *mr,
		  int flags,
		  struct ibv_pd *pd, void *addr,
		  size_t length, int access)
{
	struct ibv_rereg_mr cmd;
	struct ibv_rereg_mr_resp resp;

	if (flags & IBV_REREG_MR_KEEP_VALID)
		return ENOTSUP;

	return ibv_cmd_rereg_mr(mr, flags, addr, length,
				(uintptr_t)addr,
				access, pd,
				&cmd, sizeof(cmd),
				&resp, sizeof(resp));
}

int mlx4_dereg_mr(struct ibv_mr *mr)
{
	int ret;
	struct mlx4_mr *mlx4_mr = to_mmr(mr);

	if (mlx4_mr->allocation_flags & IBV_EXP_ACCESS_NO_RDMA)
		goto free_mr;

	ret = ibv_cmd_dereg_mr(mr);
	if (ret)
		return ret;
free_mr:
	mlx4_free_mr(mlx4_mr);
	return 0;
}

struct ibv_mw *mlx4_alloc_mw(struct ibv_pd *pd, enum ibv_mw_type type)
{
	struct ibv_mw *mw;
	struct ibv_alloc_mw cmd;
	struct ibv_alloc_mw_resp resp;
	int ret;

	mw = calloc(1, sizeof(*mw));
	if (!mw)
		return NULL;

	ret = ibv_cmd_alloc_mw(pd, type, mw, &cmd, sizeof(cmd),
			     &resp, sizeof(resp));

	if (ret) {
		free(mw);
		return NULL;
	}

	return mw;
}

int mlx4_dealloc_mw(struct ibv_mw *mw)
{
	int ret;
	struct ibv_dealloc_mw cmd;

	ret = ibv_cmd_dealloc_mw(mw, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	free(mw);
	return 0;
}

int mlx4_bind_mw(struct ibv_qp *qp, struct ibv_mw *mw,
		 struct ibv_mw_bind *mw_bind)
{
	struct ibv_send_wr *bad_wr = NULL;
	struct ibv_send_wr wr = { };
	int ret;


	wr.opcode = IBV_WR_BIND_MW;
	wr.next = NULL;

	wr.wr_id = mw_bind->wr_id;
	wr.send_flags = mw_bind->send_flags;

	wr.bind_mw.mw = mw;
	wr.bind_mw.rkey = ibv_inc_rkey(mw->rkey);
	wr.bind_mw.bind_info = mw_bind->bind_info;

	ret = mlx4_post_send(qp, &wr, &bad_wr);

	if (ret)
		return ret;

	/* updating the mw with the latest rkey. */
	mw->rkey = wr.bind_mw.rkey;

	return 0;
}

int align_queue_size(int req)
{
	int nent;

	for (nent = 1; nent < req; nent <<= 1)
		; /* nothing */

	return nent;
}

static struct ibv_cq *create_cq(struct ibv_context *context,
				int cqe,
				struct ibv_comp_channel *channel,
				int comp_vector,
				struct ibv_exp_cq_init_attr *attr)
{
	struct mlx4_create_cq		cmd;
	struct mlx4_exp_create_cq	cmd_e;
	struct mlx4_create_cq_resp	resp;
	struct mlx4_cq			*cq;
	int				ret;
	struct mlx4_context		*mctx = to_mctx(context);
	int				thread_safe;

	/* Sanity check CQ size before proceeding */
	if (cqe > 0x3fffff)
		return NULL;

	cq = calloc(1, sizeof(*cq));
	if (!cq)
		return NULL;

	cq->cons_index = 0;
	cq->wait_index = 0;
	cq->wait_count = 0;

	thread_safe = !mlx4_single_threaded;
	if (attr && (attr->comp_mask & IBV_EXP_CQ_INIT_ATTR_RES_DOMAIN)) {
		if (!attr->res_domain) {
			errno = EINVAL;
			goto err;
		}
		thread_safe = (to_mres_domain(attr->res_domain)->attr.thread_model == IBV_EXP_THREAD_SAFE);
	}

	if (mlx4_lock_init(&cq->lock, thread_safe, mlx4_get_locktype()))
		goto err;

	cq->model_flags = thread_safe ? MLX4_CQ_MODEL_FLAG_THREAD_SAFE : 0;

	cqe = align_queue_size(cqe + 1);

	if (mlx4_alloc_cq_buf(to_mctx(context), &cq->buf, cqe, mctx->cqe_size))
		goto err;

	cq->cqe_size = mctx->cqe_size;
	cq->set_ci_db  = mlx4_alloc_db(to_mctx(context), MLX4_DB_TYPE_CQ);
	if (!cq->set_ci_db)
		goto err_buf;

	cq->arm_db     = cq->set_ci_db + 1;
	*cq->arm_db    = 0;
	cq->arm_sn     = 1;
	*cq->set_ci_db = 0;

	if (NULL != attr) {
		cmd_e.buf_addr = (uintptr_t) cq->buf.buf;
		cmd_e.db_addr  = (uintptr_t) cq->set_ci_db;
	} else {
		cmd.buf_addr = (uintptr_t) cq->buf.buf;
		cmd.db_addr  = (uintptr_t) cq->set_ci_db;
	}
	if (NULL != attr) {
		ret = ibv_exp_cmd_create_cq(context, cqe - 1, channel,
					    comp_vector, &cq->ibv_cq,
					    &cmd_e.ibv_cmd,
					    sizeof(cmd_e.ibv_cmd),
					    sizeof(cmd_e) - sizeof(cmd_e.ibv_cmd),
					    &resp.ibv_resp,
					    sizeof(resp.ibv_resp),
					    sizeof(resp) - sizeof(resp.ibv_resp),
					    attr);
	} else {
		ret = ibv_cmd_create_cq(context, cqe - 1, channel, comp_vector,
					&cq->ibv_cq, &cmd.ibv_cmd, sizeof cmd,
					&resp.ibv_resp, sizeof(resp));
	}
	if (ret)
		goto err_db;

	cq->cqn = resp.cqn;
	cq->stall_next_poll	= 0;
	cq->stall_enable	= mctx->stall_enable;
	if (NULL != attr && attr->comp_mask) {
		if (cmd_e.ibv_cmd.comp_mask & IBV_EXP_CREATE_CQ_CAP_FLAGS) {
			cq->creation_flags  = attr->flags;
		}
	}

	cq->pattern = MLX4_CQ_PATTERN;
	////TODO: look into this later
	//cq->num_chunks_to_recv = 0;
	//cq->current_cqe = NULL;
	//cq->current_qpn = 0;
	//// store channel pointer
	/*
	if (channel != NULL) {
		cq->comp_channel = channel;
	}	
	*/
	////
	return &cq->ibv_cq;

err_db:
	mlx4_free_db(to_mctx(context), MLX4_DB_TYPE_CQ, cq->set_ci_db);

err_buf:
	if (cq->buf.hmem != NULL)
		mlx4_free_buf_huge(to_mctx(context), &cq->buf);
	else
		mlx4_free_buf(&cq->buf);
err:
	free(cq);

	return NULL;
}

struct ibv_cq *mlx4_create_cq(struct ibv_context *context, int cqe,
			      struct ibv_comp_channel *channel,
			      int comp_vector)
{
	read_init_vars(to_mctx(context));
	return create_cq(context, cqe, channel, comp_vector, NULL);
}

struct ibv_cq *mlx4_create_cq_ex(struct ibv_context *context,
				    int cqe,
				    struct ibv_comp_channel *channel,
				    int comp_vector,
				    struct ibv_exp_cq_init_attr *attr)
{
	return create_cq(context, cqe, channel, comp_vector, attr);
}

int mlx4_modify_cq(struct ibv_cq *cq,
		   struct ibv_exp_cq_attr *attr,
		   int attr_mask)
{
	struct ibv_exp_modify_cq cmd;
	return ibv_exp_cmd_modify_cq(cq, attr, attr_mask, &cmd, sizeof(cmd));
}

int mlx4_resize_cq(struct ibv_cq *ibcq, int cqe)
{
	struct mlx4_cq *cq = to_mcq(ibcq);
	struct mlx4_resize_cq cmd;
	struct mlx4_buf buf;
	int old_cqe, outst_cqe, ret;

	/* Sanity check CQ size before proceeding */
	if (cqe > 0x3fffff)
		return EINVAL;

	mlx4_lock(&cq->lock);

	cqe = align_queue_size(cqe + 1);
	if (cqe == ibcq->cqe + 1) {
		ret = 0;
		goto out;
	}

	/* Can't be smaller then the number of outstanding CQEs */
	outst_cqe = mlx4_get_outstanding_cqes(cq);
	if (cqe < outst_cqe + 1) {
		ret = 0;
		goto out;
	}

	ret = mlx4_alloc_cq_buf(to_mctx(ibcq->context), &buf, cqe,
					cq->cqe_size);
	if (ret)
		goto out;

	old_cqe = ibcq->cqe;
	cmd.buf_addr = (uintptr_t) buf.buf;

#ifdef IBV_CMD_RESIZE_CQ_HAS_RESP_PARAMS
	{
		struct ibv_resize_cq_resp resp;
		ret = ibv_cmd_resize_cq(ibcq, cqe - 1, &cmd.ibv_cmd, sizeof cmd,
					&resp, sizeof resp);
	}
#else
	ret = ibv_cmd_resize_cq(ibcq, cqe - 1, &cmd.ibv_cmd, sizeof cmd);
#endif
	if (ret) {
		if (cq->buf.hmem != NULL)
			mlx4_free_buf_huge(to_mctx(ibcq->context), &buf);
		else
			mlx4_free_buf(&buf);
		goto out;
	}

	mlx4_cq_resize_copy_cqes(cq, buf.buf, old_cqe);

	if (cq->buf.hmem != NULL)
		mlx4_free_buf_huge(to_mctx(ibcq->context), &cq->buf);
	else
		mlx4_free_buf(&cq->buf);

	cq->buf = buf;
	mlx4_update_cons_index(cq);
out:
	mlx4_unlock(&cq->lock);
	return ret;
}

int mlx4_destroy_cq(struct ibv_cq *cq)
{
	int ret;

	ret = ibv_cmd_destroy_cq(cq);
	if (ret)
		return ret;

	mlx4_free_db(to_mctx(cq->context), MLX4_DB_TYPE_CQ, to_mcq(cq)->set_ci_db);
	if (to_mcq(cq)->buf.hmem != NULL)
		mlx4_free_buf_huge(to_mctx(cq->context), &to_mcq(cq)->buf);
	else
		mlx4_free_buf(&to_mcq(cq)->buf);
	free(to_mcq(cq));

	return 0;
}

void *mlx4_get_legacy_xrc(struct ibv_srq *srq)
{
	struct mlx4_srq	*msrq = to_msrq(srq);

	return msrq->ibv_srq_legacy;
}

void mlx4_set_legacy_xrc(struct ibv_srq *srq, void *legacy_xrc_srq)
{
	struct mlx4_srq	*msrq = to_msrq(srq);

	msrq->ibv_srq_legacy = legacy_xrc_srq;
	return;
}

struct ibv_srq *mlx4_create_srq(struct ibv_pd *pd,
				struct ibv_srq_init_attr *attr)
{
	struct mlx4_create_srq      cmd;
	struct mlx4_create_srq_resp resp;
	struct mlx4_srq		   *srq;
	int			    ret;

	/* Sanity check SRQ size before proceeding */
	if (attr->attr.max_wr > 1 << 16 || attr->attr.max_sge > 64)
		return NULL;

	srq = calloc(1, sizeof *srq);
	if (!srq)
		return NULL;

	if (mlx4_spinlock_init(&srq->lock, !mlx4_single_threaded))
		goto err;

	srq->max     = align_queue_size(attr->attr.max_wr + 1);
	srq->max_gs  = attr->attr.max_sge;
	srq->counter = 0;
	srq->ext_srq = 0;

	if (mlx4_alloc_srq_buf(pd, &attr->attr, srq))
		goto err;

	srq->db = mlx4_alloc_db(to_mctx(pd->context), MLX4_DB_TYPE_RQ);
	if (!srq->db)
		goto err_free;

	*srq->db = 0;

	cmd.buf_addr = (uintptr_t) srq->buf.buf;
	cmd.db_addr  = (uintptr_t) srq->db;

	ret = ibv_cmd_create_srq(pd, &srq->verbs_srq.srq, attr,
				 &cmd.ibv_cmd, sizeof cmd,
				 &resp.ibv_resp, sizeof resp);
	if (ret)
		goto err_db;

	return &srq->verbs_srq.srq;

err_db:
	mlx4_free_db(to_mctx(pd->context), MLX4_DB_TYPE_RQ, srq->db);

err_free:
	free(srq->wrid);
	mlx4_free_buf(&srq->buf);

err:
	free(srq);

	return NULL;
}

struct ibv_srq *mlx4_create_srq_ex(struct ibv_context *context,
				   struct ibv_srq_init_attr_ex *attr_ex)
{
	if (!(attr_ex->comp_mask & IBV_SRQ_INIT_ATTR_TYPE) ||
	    (attr_ex->srq_type == IBV_SRQT_BASIC))
		return mlx4_create_srq(attr_ex->pd, (struct ibv_srq_init_attr *) attr_ex);
	else if (attr_ex->srq_type == IBV_SRQT_XRC)
		return mlx4_create_xrc_srq(context, attr_ex);

	return NULL;
}

int mlx4_modify_srq(struct ibv_srq *srq,
		     struct ibv_srq_attr *attr,
		     int attr_mask)
{
	struct ibv_modify_srq cmd;

	if (srq->handle == LEGACY_XRC_SRQ_HANDLE)
		srq = (struct ibv_srq *)(((struct ibv_srq_legacy *) srq)->ibv_srq);

	return ibv_cmd_modify_srq(srq, attr, attr_mask, &cmd, sizeof cmd);
}

int mlx4_query_srq(struct ibv_srq *srq,
		    struct ibv_srq_attr *attr)
{
	struct ibv_query_srq cmd;

	if (srq->handle == LEGACY_XRC_SRQ_HANDLE)
		srq = (struct ibv_srq *)(((struct ibv_srq_legacy *) srq)->ibv_srq);

	return ibv_cmd_query_srq(srq, attr, &cmd, sizeof cmd);
}

int mlx4_destroy_srq(struct ibv_srq *srq)
{
	int ret;
	struct ibv_srq *legacy_srq = NULL;

	if (srq->handle == LEGACY_XRC_SRQ_HANDLE) {
		legacy_srq = srq;
		srq = (struct ibv_srq *)(((struct ibv_srq_legacy *) srq)->ibv_srq);
	}

	if (to_msrq(srq)->ext_srq) {
		ret =  mlx4_destroy_xrc_srq(srq);
		if (ret)
			return ret;

		if (legacy_srq)
			free(legacy_srq);

		return 0;
	}

	ret = ibv_cmd_destroy_srq(srq);
	if (ret)
		return ret;

	mlx4_free_db(to_mctx(srq->context), MLX4_DB_TYPE_RQ, to_msrq(srq)->db);
	mlx4_free_buf(&to_msrq(srq)->buf);
	free(to_msrq(srq)->wrid);
	free(to_msrq(srq));

	return 0;
}

struct ibv_qp *mlx4_create_qp_ex(struct ibv_context *context,
				 struct ibv_qp_init_attr_ex *attr)
{
	read_init_vars(to_mctx(context));
	return mlx4_exp_create_qp(context, (struct ibv_exp_qp_init_attr *)attr);
}

//// original mlx4_create_qp is here
struct ibv_qp *__mlx4_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *attr)
{
	struct ibv_exp_qp_init_attr attr_exp;
	struct ibv_qp *qp;
	/* We should copy below only the shared fields excluding the xrc_domain field.
	  * Otherwise we may have an ABI issue with applications that were compiled
	  * without the xrc_domain field. The xrc_domain any way has no affect in
	  * the sender side, no need to copy in/out.
	*/
	int init_attr_base_size = offsetof(struct ibv_qp_init_attr, xrc_domain);

	/* copying only shared fields */
	memcpy(&attr_exp, attr, init_attr_base_size);
	attr_exp.comp_mask = IBV_EXP_QP_INIT_ATTR_PD;
	attr_exp.pd = pd;
	qp = mlx4_exp_create_qp(pd->context, &attr_exp);
	if (qp)
		memcpy(attr, &attr_exp, init_attr_base_size);
	return qp;
}
////

struct ibv_qp *mlx4_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *attr)
{
	//// create custom cq used for two-sided rdma message splitting
	// assume user created send_cq
	//printf("DEBUG mlx4_create_qp: creating split_cq\n");
	//struct mlx4_cq *send_cq = to_mcq(attr->send_cq);
	////struct ibv_comp_channel *channel = send_cq->comp_channel;
	////struct ibv_cq *split_cq = create_cq(pd->context, 1, channel, 0, NULL);
	struct ibv_comp_channel *channel = ibv_create_comp_channel(pd->context);
	////struct ibv_cq *split_cq = create_cq(pd->context, 1, channel, 0, NULL);
	struct ibv_cq *split_cq = mlx4_create_cq(pd->context, SPLIT_MAX_CQE, channel, 0);
	//// arm split_cq for completion events
	mlx4_arm_cq(split_cq, 0);

	/// create a custom qp for our own use 
	struct ibv_qp_init_attr split_init_attr;
	memset(&split_init_attr, 0, sizeof(struct ibv_qp_init_attr));
	split_init_attr.send_cq = split_cq;
	split_init_attr.recv_cq = split_cq;
	split_init_attr.cap.max_send_wr  = SPLIT_MAX_SEND_WR;
	split_init_attr.cap.max_recv_wr  = SPLIT_MAX_RECV_WR;
	split_init_attr.cap.max_send_sge = 1;
	split_init_attr.cap.max_recv_sge = 1;
	split_init_attr.cap.max_inline_data = 100;	// probably not going to use inline there
	split_init_attr.qp_type = IBV_QPT_RC;

	struct ibv_qp 		*qp;
	struct ibv_qp 		*split_qp;
	split_qp = __mlx4_create_qp(pd, &split_init_attr);
	printf("DEBUG mlx4_create_qp: split_qp->qpn = %06x\n", split_qp->qp_num);
	//// Now create the user's qp
	qp = __mlx4_create_qp(pd, attr);
	printf("DEBUG mlx4_create_qp: orig_qp->qpn = %06x\n", qp->qp_num);
	//// store split_qp & split_cq inside the user's qp
	if (qp) {
		struct mlx4_qp *mqp = to_mqp(qp);
		mqp->split_qp = split_qp;
		mqp->split_cq = split_cq;
		mqp->split_comp_channel = channel;
		//// register mr for two-sided splitting header message
		//// register size * 2 since in split qpn exchange we need mr for send & recv at the same time
		mqp->split_fc_mr = mlx4_reg_mr(pd, &mqp->split_fc_msg, 2 * sizeof(struct Split_FC_message), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
		//// add in pd for later deletion
		to_mpd(pd)->split_fc_mr = mqp->split_fc_mr;
		////

	}
	////

	/* isolation */
	int fd_shm;
	if ((fd_shm = shm_open(SHARED_MEM_NAME, O_RDWR, 0600)) == -1){
		printf("@@@Pacer's shared memory is not found. Pacer won't be used.\n");
		flow = NULL;
	} else {
		atexit(set_inactive_on_exit);
		flow = mmap(NULL, MAX_FLOWS * sizeof(struct flow_info), PROT_WRITE | PROT_READ,
			MAP_SHARED, fd_shm, 0);
		contact_pacer();
		flow += slot;
		printf("@@@At slot %d.\n", slot);
	}
	/* end */

	return qp;

}

struct ibv_qp *mlx4_open_qp(struct ibv_context *context, struct ibv_qp_open_attr *attr)
{
	struct ibv_open_qp cmd;
	struct ibv_create_qp_resp resp;
	struct mlx4_qp *qp;
	int ret;

	qp = calloc(1, sizeof *qp);
	if (!qp)
		return NULL;

	ret = ibv_cmd_open_qp(context, &qp->verbs_qp, sizeof(qp->verbs_qp), attr,
			      &cmd, sizeof cmd, &resp, sizeof resp);
	if (ret)
		goto err;

	return &qp->verbs_qp.qp;

err:
	free(qp);
	return NULL;
}

int mlx4_query_qp(struct ibv_qp *ibqp, struct ibv_qp_attr *attr,
		   int attr_mask,
		   struct ibv_qp_init_attr *init_attr)
{
	struct ibv_query_qp cmd;
	struct mlx4_qp *qp = to_mqp(ibqp);
	int ret;

	ret = ibv_cmd_query_qp(ibqp, attr, attr_mask, init_attr, &cmd, sizeof cmd);
	if (ret)
		return ret;

	init_attr->cap.max_send_wr     = qp->sq.max_post;
	init_attr->cap.max_send_sge    = qp->sq.max_gs;
	init_attr->cap.max_inline_data = qp->max_inline_data;

	attr->cap = init_attr->cap;

	return 0;
}

//// Original mlx4_modify_qp
int __mlx4_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		    int attr_mask)
{
	struct ibv_modify_qp cmd;
	int ret;

	if (attr_mask & IBV_QP_PORT) {
		ret = update_port_data(qp, attr->port_num);
		if (ret)
			return ret;
	}

	if (qp->state == IBV_QPS_RESET &&
	    attr_mask & IBV_QP_STATE   &&
	    attr->qp_state == IBV_QPS_INIT) {
		mlx4_qp_init_sq_ownership(to_mqp(qp));
	}

	ret = ibv_cmd_modify_qp(qp, attr, attr_mask, &cmd, sizeof cmd);

	if (!ret		       &&
	    (attr_mask & IBV_QP_STATE) &&
	    attr->qp_state == IBV_QPS_RESET) {
		if (qp->recv_cq)
			mlx4_cq_clean(to_mcq(qp->recv_cq), qp->qp_num,
				      qp->srq ? to_msrq(qp->srq) : NULL);
		if (qp->send_cq && qp->send_cq != qp->recv_cq)
			mlx4_cq_clean(to_mcq(qp->send_cq), qp->qp_num, NULL);

		mlx4_init_qp_indices(to_mqp(qp));
		if (to_mqp(qp)->rq.wqe_cnt)
			*to_mqp(qp)->db = 0;
	}

	return ret;
}

//// Modify to change the qp state of the custom qp
// A more general design: AT INIT to RTR transition, we move user's QP to RTR and then to RTS using our own psn to exchange split qp's qpn.
// Then we reset user's QP, and move it all the way to RTR using cached user's info.
// This design is to handle cases where <1> the user neven transitions QP on the receiver side to RTS. <2> random qpns at qp creation
// (1) Cache user's parameters when modify qp state so that we can later reproduce it from RESET to RTR
// TOOD: (2) We also cache all recv qps user post to the recv queue at INIT state. (It is allowed to post rr at INIT state, and reset clean everything.)
// (3) At INIT -> RTR transition, always transition to RTR and then to RTS using our own special rq_psn and sq_psn
// (4) after split qp qpn exchange, reset user's qp and move again to RTR
int mlx4_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		    int attr_mask)
{
	struct ibv_modify_qp cmd;
	int ret;
	//// do the same state transition for custom qp.
	struct mlx4_qp *mqp = to_mqp(qp);
	////

	if (attr_mask & IBV_QP_PORT) {
		////printf("DEBUG MLX4_MODIFY_QP: actually enter here\n");
		ret = update_port_data(qp, attr->port_num);
		if (ret)
			return ret;
		//// do same for custom_qp
		update_port_data(mqp->split_qp, attr->port_num);
		////
	}

	if (qp->state == IBV_QPS_RESET &&
	    attr_mask & IBV_QP_STATE   &&
	    attr->qp_state == IBV_QPS_INIT) {
		mlx4_qp_init_sq_ownership(to_mqp(qp));
		//// do same for custom_qp
		mlx4_qp_init_sq_ownership(to_mqp(mqp->split_qp));
		////
	}

	if (MANUAL_SPLIT_QPN_DIFF) {

		//// modify original user's qp state
		ret = ibv_cmd_modify_qp(qp, attr, attr_mask, &cmd, sizeof cmd);

		//// if manually set the qpn difference for split qp
		// Essentially we are looking at the user QP's INIT -> RTR transition when hacking dest_qp_num
		// We also directly modify split qp to RTS instead of wait users modify their qp to RTS
		// because some users might never do so.
		if (qp->state == IBV_QPS_INIT) {
			if (__mlx4_modify_qp(mqp->split_qp, attr, attr_mask)) {
				fprintf(stderr, "Failed to modify SPLIT QP to INIT State\n");
				goto err;
			}
			printf("<<<<MODIFY SPLIT QP to INIT>>>>\n");
		} else if (qp->state == IBV_QPS_RTR && mqp->split_qp->state == IBV_QPS_INIT) {
			////attr->dest_qp_num -= SPLIT_QP_NUM_DIFF;
			//attr->dest_qp_num -= 1;	// for old benchmark
			//attr->dest_qp_num -= 2; // for new benchmark
			struct ibv_qp_attr split_attr;
			memcpy(&split_attr, attr, sizeof(struct ibv_qp_attr));
			int split_mask = attr_mask;
			split_attr.dest_qp_num -= SPLIT_QP_NUM_DIFF;
			split_attr.rq_psn = 10083;
			//printf("DEBUG MODIFY QP: now attr->dest_qp_num is %06x\n", attr->dest_qp_num);
			//// pre-post a RR for later use
			//printf("DEBUG MODIFY QP: post a RR to split_qp\n");
			struct ibv_sge sge;
			struct ibv_recv_wr wr;
			struct ibv_recv_wr *bad_wr;
			memset(&sge, 0, sizeof(sge));
			sge.addr = (uintptr_t)&mqp->split_fc_msg;
			sge.length = sizeof(struct Split_FC_message);
			sge.lkey = mqp->split_fc_mr->lkey;
			//printf("DDDDD0: sge.addr = %" PRIu64 "\n", sge.addr);
			//printf("DDDDD0: sge.length = %d \n", sge.length);
			//printf("DDDDD0: sge.lkey = %" PRIu32 "\n", sge.lkey);

			memset(&wr, 0, sizeof(wr));
			wr.wr_id = 0;
			wr.next = NULL;
			wr.sg_list = &sge;
			wr.num_sge = 1;

			if (__mlx4_post_recv(mqp->split_qp, &wr, &bad_wr)) {
				fprintf(stderr, "Failed to post the initial RR to split qp.\n");
				goto err;
			}

			if (__mlx4_modify_qp(mqp->split_qp, &split_attr, split_mask)) {
				fprintf(stderr, "Failed to modify SPLIT QP to RTR State\n");
				goto err;
			}
			printf("<<<<MODIFY SPLIT QP to RTR>>>>\n");
			// Modify split qp to RTS
			split_attr.qp_state	    = IBV_QPS_RTS;
			split_attr.sq_psn	    = 10083;
			split_attr.timeout	    = 14;
			split_attr.retry_cnt	= 7;
			split_attr.rnr_retry	= 7;
			split_attr.max_rd_atomic  = 1;
			int rts_mask = 	IBV_QP_STATE              |
							IBV_QP_TIMEOUT            |
							IBV_QP_RETRY_CNT          |
							IBV_QP_RNR_RETRY          |
							IBV_QP_SQ_PSN             |
							IBV_QP_MAX_QP_RD_ATOMIC;

			if (__mlx4_modify_qp(mqp->split_qp, &split_attr, rts_mask)) {
				fprintf(stderr, "Failed to modify SPLIT QP to RTS State\n");
				goto err;
			}
			printf("<<<<MODIFY SPLIT QP to RTS>>>>\n");
		}
	} else {	// a general solution for split qp qpn exchange
		if (qp->state == IBV_QPS_RESET && attr->qp_state == IBV_QPS_INIT) {
			//// USER QP RESET->INIT transition.
			printf("<<<<USER QP: RESET -> INIT.>>>>\n");
			//// Caching user qp's qp_attr.
			mqp->user_qp_attr_init = (struct ibv_qp_attr *)malloc(sizeof(struct ibv_qp_attr));
			memcpy(mqp->user_qp_attr_init, attr, sizeof(struct ibv_qp_attr));
			mqp->user_qp_mask_init = attr_mask;
		} else if (qp->state == IBV_QPS_INIT && attr->qp_state == IBV_QPS_RTR) {
			//// USER QP INIT -> RTR transition.
			printf("<<<<USER QP: INIT -> RTR.>>>>\n");
			//// Caching user qp's qp_attr.
			mqp->user_qp_attr_rtr = (struct ibv_qp_attr *)malloc(sizeof(struct ibv_qp_attr));
			memcpy(mqp->user_qp_attr_rtr, attr, sizeof(struct ibv_qp_attr));
			mqp->user_qp_mask_rtr = attr_mask;

			//// Move user qp to RTR
			uint32_t orig_rq_psn = attr->rq_psn;
			mqp->user_qp_attr_rtr->rq_psn = 18300;
			ret = __mlx4_modify_qp(qp, mqp->user_qp_attr_rtr, attr_mask);
			if (ret) {
				fprintf(stderr, "Failed to manually modify USER QP to RTR State.\n");
				goto err;
			}
			mqp->user_qp_attr_rtr->rq_psn = orig_rq_psn;

			//// TODO: turn off the flag for caching RRs

			//// Move user qp to RTS
			struct ibv_qp_attr temp_rts_attr;	
			memset(&temp_rts_attr, 0, sizeof(struct ibv_qp_attr));
			temp_rts_attr.qp_state	    = IBV_QPS_RTS;
			temp_rts_attr.sq_psn	    = 18300;
			temp_rts_attr.timeout	    = 14;
			temp_rts_attr.retry_cnt		= 7;
			temp_rts_attr.rnr_retry		= 7;
			temp_rts_attr.max_rd_atomic  = 1;
			int rts_mask = 	IBV_QP_STATE              |
							IBV_QP_TIMEOUT            |
							IBV_QP_RETRY_CNT          |
							IBV_QP_RNR_RETRY          |
							IBV_QP_SQ_PSN             |
							IBV_QP_MAX_QP_RD_ATOMIC;

			ret = __mlx4_modify_qp(qp, &temp_rts_attr, rts_mask);
			if (ret) {
				fprintf(stderr, "Failed to manually modify USER QP to RTS State\n");
				goto err;
			}

			//// Exchange split qp qpn using USER QP
			// post RR for ex message
			struct ibv_sge rsge;
			struct ibv_recv_wr rwr;
			struct ibv_recv_wr *bad_rwr;
			memset(&rsge, 0, sizeof(rsge));
			rsge.addr = (uintptr_t)&mqp->split_fc_msg[1];
			rsge.length = sizeof(struct Split_FC_message);
			rsge.lkey = mqp->split_fc_mr->lkey;

			memset(&rwr, 0, sizeof(rwr));
			rwr.wr_id = 0;
			rwr.next = NULL;
			rwr.sg_list = &rsge;
			rwr.num_sge = 1;

			ret = __mlx4_post_recv(qp, &rwr, &bad_rwr);
			if (ret) {
				fprintf(stderr, "Failed to post exchange RR to user qp.\n");
				goto err;
			}

			// Set split qp's sq_psn for exchange
			uint32_t split_qp_sq_psn = lrand48() & 0xffffff;
			mqp->split_fc_msg[0].type = EXCHANGE;
			mqp->split_fc_msg[0].msg.split_qp_exchange.qp_num = mqp->split_qp->qp_num;
			mqp->split_fc_msg[0].msg.split_qp_exchange.sq_psn = split_qp_sq_psn;

			// Post SR for ex message
			struct ibv_sge ssge;
			struct ibv_send_wr swr;
			struct ibv_send_wr *bad_swr;
				
			memset(&ssge, 0, sizeof(ssge));
			ssge.addr	  = (uintptr_t)&mqp->split_fc_msg[0];
			ssge.length = sizeof(struct Split_FC_message);
			ssge.lkey	  = mqp->split_fc_mr->lkey;
				
			memset(&swr, 0, sizeof(swr));
			swr.wr_id      = 0;
			swr.sg_list    = &ssge;
			swr.num_sge    = 1;
			swr.opcode     = IBV_WR_SEND;
			swr.send_flags = 0;			// Unsignaled

			ret = __mlx4_post_send(qp, &swr, &bad_swr);
			if (ret != 0) {
				fprintf(stderr, "Failed to post exchange SR to user qp.\n");
				goto err;
			}

			// Recv exchange message
			int ne = 0;
			struct ibv_wc wc;
			do {
				ne = mlx4_poll_ibv_cq(qp->recv_cq, 1, &wc);
			} while (ne == 0);
			if (mqp->split_fc_msg[1].type != EXCHANGE) {
				ret = 1;
				fprintf(stderr, "Wrong message when exchange split qp info\n");
				goto err;
			}
			uint32_t remote_split_qpn = mqp->split_fc_msg[1].msg.split_qp_exchange.qp_num;
			uint32_t remote_split_sq_psn = mqp->split_fc_msg[1].msg.split_qp_exchange.sq_psn; 

			// Reset user QP and move to RTR
			int reset_mask = IBV_QP_STATE;
			struct ibv_qp_attr temp_reset_attr;
			memset(&temp_reset_attr, 0, sizeof(struct ibv_qp_attr));
			temp_reset_attr.qp_state = IBV_QPS_RESET;
			ret = __mlx4_modify_qp(qp, &temp_reset_attr, reset_mask);
			if (ret) {
				fprintf(stderr, "Failed to modify USER QP to RESET State.\n");
				goto err;
			}

			ret = __mlx4_modify_qp(qp, mqp->user_qp_attr_init, mqp->user_qp_mask_init);
			if (ret) {
				fprintf(stderr, "Failed to modify USER QP to INIT State.\n");
				goto err;
			}

			ret = __mlx4_modify_qp(qp, mqp->user_qp_attr_rtr, mqp->user_qp_mask_rtr);
			if (ret) {
				fprintf(stderr, "Failed to modify USER QP to RTR State.\n");
				goto err;
			}
			printf("<<<<USER QP back to RTR now.\n");
			////TODO: repost cached RRs to UESR QP

			// Move split_qp qp state
			if (__mlx4_modify_qp(mqp->split_qp, mqp->user_qp_attr_init, mqp->user_qp_mask_init)) {
				fprintf(stderr, "Failed to modify SPLIT QP to INIT State\n");
				goto err;
			}
			printf("<<<<MODIFY SPLIT QP to INIT>>>>\n");	

			uint32_t orig_dest_qpn = mqp->user_qp_attr_rtr->dest_qp_num;
			orig_rq_psn = attr->rq_psn;
			mqp->user_qp_attr_rtr->dest_qp_num = remote_split_qpn;
			mqp->user_qp_attr_rtr->rq_psn = remote_split_sq_psn;
			if (__mlx4_modify_qp(mqp->split_qp, mqp->user_qp_attr_rtr, mqp->user_qp_mask_rtr)) {
				fprintf(stderr, "Failed to modify SPLIT QP to INIT State\n");
				goto err;
			}
			mqp->user_qp_attr_rtr->dest_qp_num = orig_dest_qpn;
			mqp->user_qp_attr_rtr->rq_psn = orig_rq_psn;
			printf("<<<<MODIFY SPLIT QP to RTR>>>>\n");	

			temp_rts_attr.sq_psn = split_qp_sq_psn;
			if (__mlx4_modify_qp(mqp->split_qp, &temp_rts_attr, rts_mask)) {
				fprintf(stderr, "Failed to modify SPLIT QP to RTS State\n");
				goto err;
			}
			printf("<<<<MODIFY SPLIT QP to RTS>>>>\n");
						
		}
	}



	/*
	else if (qp->state == IBV_QPS_RTS) {
		struct ibv_qp_attr split_attr;
		memcpy(&split_attr, attr, sizeof(struct ibv_qp_attr));

		split_attr.dest_qp_num -= SPLIT_QP_NUM_DIFF;
		split_attr.qp_state	    = IBV_QPS_RTS;
		//split_attr.sq_psn	    = lrand48() & 0xffffff;
		//split_attr.sq_psn	    = 4410;
		split_attr.timeout	    = 14;
		split_attr.retry_cnt	= 7;
		split_attr.rnr_retry	= 7;
		split_attr.max_rd_atomic  = 1;
		
		int rts_mask = 	IBV_QP_STATE              |
						IBV_QP_TIMEOUT            |
						IBV_QP_RETRY_CNT          |
						IBV_QP_RNR_RETRY          |
						IBV_QP_SQ_PSN             |
						IBV_QP_MAX_QP_RD_ATOMIC;

		if (rts_mask != attr_mask) {
			printf("PUPU1\n");
		}
		if (split_attr.timeout != attr->timeout) {
			printf("PUPU2\n");
		}
		if (split_attr.qp_state != attr->qp_state) {
			printf("PUPU3\n");
		}
		if (split_attr.qp_state != attr->qp_state) {
			printf("PUPU3\n");
		}
		

		if (__mlx4_modify_qp(mqp->split_qp, &split_attr, rts_mask)) {
		//if (__mlx4_modify_qp(mqp->split_qp, &split_attr, attr_mask)) {
			fprintf(stderr, "Failed to modify SPLIT QP to RTS State\n");
			goto err;
		}
		printf("<<<<MODIFY SPLIT QP to RTS>>>>\n");
	}
	*/

	if (!ret		       &&
	    (attr_mask & IBV_QP_STATE) &&
	    attr->qp_state == IBV_QPS_RESET) {
		////printf("DEBUG MLX4_MODIFY_QP: also enter here\n");
		if (qp->recv_cq)
			mlx4_cq_clean(to_mcq(qp->recv_cq), qp->qp_num,
				      qp->srq ? to_msrq(qp->srq) : NULL);
		if (qp->send_cq && qp->send_cq != qp->recv_cq)
			mlx4_cq_clean(to_mcq(qp->send_cq), qp->qp_num, NULL);

		mlx4_init_qp_indices(to_mqp(qp));
		if (to_mqp(qp)->rq.wqe_cnt)
			*to_mqp(qp)->db = 0;

		//// do same for custom_qp
		qp = mqp->split_qp;
		if (qp->recv_cq)
			mlx4_cq_clean(to_mcq(qp->recv_cq), qp->qp_num,
				      qp->srq ? to_msrq(qp->srq) : NULL);
		if (qp->send_cq && qp->send_cq != qp->recv_cq)
			mlx4_cq_clean(to_mcq(qp->send_cq), qp->qp_num, NULL);

		mlx4_init_qp_indices(to_mqp(qp));
		if (to_mqp(qp)->rq.wqe_cnt)
			*to_mqp(qp)->db = 0;
		////
	}

err:
	return ret;
}

static void mlx4_lock_cqs(struct ibv_qp *qp)
{
	struct mlx4_cq *send_cq = to_mcq(qp->send_cq);
	struct mlx4_cq *recv_cq = to_mcq(qp->recv_cq);

	if (!qp->send_cq || !qp->recv_cq) {
		if (qp->send_cq)
			mlx4_lock(&send_cq->lock);
		else if (qp->recv_cq)
			mlx4_lock(&recv_cq->lock);
	} else if (send_cq == recv_cq) {
		mlx4_lock(&send_cq->lock);
	} else if (send_cq->cqn < recv_cq->cqn) {
		mlx4_lock(&send_cq->lock);
		mlx4_lock(&recv_cq->lock);
	} else {
		mlx4_lock(&recv_cq->lock);
		mlx4_lock(&send_cq->lock);
	}
}

static void mlx4_unlock_cqs(struct ibv_qp *qp)
{
	struct mlx4_cq *send_cq = to_mcq(qp->send_cq);
	struct mlx4_cq *recv_cq = to_mcq(qp->recv_cq);


	if (!qp->send_cq || !qp->recv_cq) {
		if (qp->send_cq)
			mlx4_unlock(&send_cq->lock);
		else if (qp->recv_cq)
			mlx4_unlock(&recv_cq->lock);
	} else if (send_cq == recv_cq) {
		mlx4_unlock(&send_cq->lock);
	} else if (send_cq->cqn < recv_cq->cqn) {
		mlx4_unlock(&recv_cq->lock);
		mlx4_unlock(&send_cq->lock);
	} else {
		mlx4_unlock(&send_cq->lock);
		mlx4_unlock(&recv_cq->lock);
	}
}

//// also do cleanup for split_qp
int mlx4_destroy_qp(struct ibv_qp *ibqp)
{
	struct mlx4_qp *qp = to_mqp(ibqp);
	int ret;

	pthread_mutex_lock(&to_mctx(ibqp->context)->qp_table_mutex);
	ret = ibv_cmd_destroy_qp(ibqp);
	if (ret) {
		pthread_mutex_unlock(&to_mctx(ibqp->context)->qp_table_mutex);
		return ret;
	}

	mlx4_lock_cqs(ibqp);
	if (qp->split_qp->recv_cq) {
		__mlx4_cq_clean(to_mcq(qp->split_qp->recv_cq), qp->split_qp->qp_num, NULL);
	}
	if (qp->split_qp->send_cq && qp->split_qp->send_cq != qp->split_qp->recv_cq) {
		__mlx4_cq_clean(to_mcq(qp->split_qp->send_cq), qp->split_qp->qp_num, NULL);
	}
	if (ibqp->recv_cq) {
		__mlx4_cq_clean(to_mcq(ibqp->recv_cq), ibqp->qp_num,
				ibqp->srq ? to_msrq(ibqp->srq) : NULL);
	}
	if (ibqp->send_cq && ibqp->send_cq != ibqp->recv_cq) {
		__mlx4_cq_clean(to_mcq(ibqp->send_cq), ibqp->qp_num, NULL);
	}

	if (qp->sq.wqe_cnt || qp->rq.wqe_cnt) {
		mlx4_clear_qp(to_mctx(qp->split_qp->context), qp->split_qp->qp_num);
		mlx4_clear_qp(to_mctx(ibqp->context), ibqp->qp_num);
	}

	mlx4_unlock_cqs(ibqp);
	pthread_mutex_unlock(&to_mctx(ibqp->context)->qp_table_mutex);

	/*
	 * Use the qp->bf to check if the QP is using dedicated BF.
	 * If so, update the dedicated BF database.
	 */
	if (qp->bf && (&qp->bf->cmn != &(to_mctx(ibqp->context)->bfs.cmn_bf))) {
		struct mlx4_bfs_data *bfs = &to_mctx(ibqp->context)->bfs;
		//printf("USING BF?");
		int idx = &(qp->bf->dedic) - bfs->dedic_bf;

		if (0 <= idx && idx < (MLX4_MAX_BFS_IN_PAGE - 1)) {
			mlx4_spin_lock(&bfs->dedic_bf_lock);
			bfs->dedic_bf_used[idx] = 0;
			bfs->dedic_bf_free++;
			mlx4_spin_unlock(&bfs->dedic_bf_lock);
		}
	}

	if (qp->rq.wqe_cnt) {
		mlx4_free_db(to_mctx(qp->split_qp->context), MLX4_DB_TYPE_RQ, to_mqp(qp->split_qp)->db);
		mlx4_free_db(to_mctx(ibqp->context), MLX4_DB_TYPE_RQ, qp->db);
	}

	mlx4_dealloc_qp_buf(qp->split_qp->context, to_mqp(qp->split_qp));
	mlx4_dealloc_qp_buf(ibqp->context, qp);

	free(to_mqp(qp->split_qp));
	free(qp);

	return 0;
}

struct ibv_ah *mlx4_create_ah_common(struct ibv_pd *pd,
				     struct ibv_ah_attr *attr,
				     uint8_t link_layer)
{
	struct mlx4_ah *ah;

	if (unlikely(!attr->dlid) &&
	    (link_layer != IBV_LINK_LAYER_ETHERNET)) {
		errno = EINVAL;
		return NULL;
	}

	ah = malloc(sizeof *ah);
	if (!ah)
		return NULL;

	memset(&ah->av, 0, sizeof ah->av);

	ah->av.port_pd   = htonl(to_mpd(pd)->pdn | (attr->port_num << 24));

	if (link_layer != IBV_LINK_LAYER_ETHERNET) {
		ah->av.g_slid = attr->src_path_bits;
		ah->av.dlid   = htons(attr->dlid);
		ah->av.sl_tclass_flowlabel = htonl(attr->sl << 28);
	} else {
		ah->vlan = ((attr->sl & 7) << 13);
		ah->av.sl_tclass_flowlabel = htonl(attr->sl << 29);
	}

	if (attr->static_rate) {
		ah->av.stat_rate = attr->static_rate + MLX4_STAT_RATE_OFFSET;
		/* XXX check rate cap? */
	}
	if (attr->is_global) {
		ah->av.g_slid   |= 0x80;
		ah->av.gid_index = attr->grh.sgid_index;
		if (attr->grh.hop_limit < 2)
			ah->av.hop_limit = 0xff;
		else
			ah->av.hop_limit = attr->grh.hop_limit;
		ah->av.sl_tclass_flowlabel |=
			htonl((attr->grh.traffic_class << 20) |
				    attr->grh.flow_label);
		memcpy(ah->av.dgid, attr->grh.dgid.raw, 16);
	}

	return &ah->ibv_ah;
}

struct ibv_ah *mlx4_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr)
{
	struct ibv_ah *ah;
	struct ibv_exp_port_attr port_attr;
	struct ibv_port_attr port_attr_legacy;
	uint8_t			link_layer;

	port_attr.comp_mask = IBV_EXP_QUERY_PORT_ATTR_MASK1;
	port_attr.mask1 = IBV_EXP_QUERY_PORT_LINK_LAYER;

	if (ibv_exp_query_port(pd->context, attr->port_num, &port_attr)) {
		if (ibv_query_port(pd->context, attr->port_num, &port_attr_legacy))
			return NULL;

		link_layer = port_attr_legacy.link_layer;
	} else {
		link_layer = port_attr.link_layer;
	}

	ah = mlx4_create_ah_common(pd, attr, link_layer);

	return ah;
}

int mlx4_destroy_ah(struct ibv_ah *ah)
{
	free(to_mah(ah));

	return 0;
}
