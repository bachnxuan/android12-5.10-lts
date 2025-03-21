// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/seqlock.h>
#include <linux/irqflags.h>
#include "engine_request.h"

/*
 * module control
 */
#define TODO
MODULE_DESCRIPTION("Stand Alone Engine Request");
MODULE_AUTHOR("MM3SW5");
MODULE_LICENSE("GPL");

unsigned int mfb_egn_debug;
module_param(mfb_egn_debug, uint, 0644);
MODULE_PARM_DESC(mfb_egn_debug, " activates debug info");

#define MyTag "[STALN]"
#define LOG_VRB(format, args...)				 \
	pr_debug(MyTag "[%s] " format, __func__, ##args)

#define LOG_DBG(format, args...)				 \
	do {							 \
		if (mfb_egn_debug >= 2)				 \
			pr_debug(MyTag "[%s] " format, __func__, ##args); \
	} while (0)

#define LOG_INF(format, args...)				 \
	do {							 \
		if (mfb_egn_debug >= 1)				 \
			pr_debug(MyTag "[%s] " format, __func__, ##args);\
	} while (0)

#define LOG_WRN(format, args...)				 \
	do {							 \
		if (mfb_egn_debug >= 0)				 \
			pr_debug(MyTag "[%s] " format, __func__, ##args); \
	} while (0)

#define LOG_ERR(format, args...)				 \
	do {							 \
		if (mfb_egn_debug >= 0)				 \
			pr_debug(MyTag "[%s] " format, __func__, ##args); \
	} while (0)


/*
 * Single ring ctl init
 */
signed int mfb_init_ring_ctl(struct ring_ctrl *rctl)
{
	if (!rctl)
		return -1;

	rctl->wcnt = 0;
	rctl->rcnt = 0;
	rctl->icnt = 0;
	rctl->gcnt = 0;
	rctl->size = 0;

	return 0;
}

signed int mfb_set_ring_size(struct ring_ctrl *rctl, unsigned int size)
{
	if (!rctl)
		return -1;

	rctl->size = size;

	return 0;
}

signed int mfb_init_frame(struct frame *frame)
{
	if (!frame)
		return -1;

	frame->state = FRAME_STATUS_EMPTY;

	return 0;
}

/*
 * single request init
 */
signed int mfb_init_request(struct request_mfb *req)
{
	int f;

	if (!req)
		return -1;

	req->state = REQUEST_STATE_EMPTY;
	mfb_init_ring_ctl(&req->fctl);
	mfb_set_ring_size(&req->fctl, MFB_MAX_FRAMES_PER_REQUEST);

	for (f = 0; f < MFB_MAX_FRAMES_PER_REQUEST; f++)
		mfb_init_frame(&req->frames[f]);

	req->pending_run = false;

	return 0;
}

/*
 * per-frame data init
 */
signed int mfb_set_frame_data(struct frame *f, void *engine)
{
	if (!f) {
		LOG_ERR("NULL frame(%p)", (void *)f);
		return -1;
	}

	f->data = engine;

	return 0;
}

/*
 * TOP : per-sub-engine
 * Size Limitaion: eng_reqs : [MFB_MAX_REQUEST_SIZE_PER_ENGINE]
 *	data : [MFB_MAX_REQUEST_SIZE_PER_ENGINE][MFB_MAX_FRAMES_PER_REQUEST]
 */
signed int mfb_register_requests(struct engine_requests *eng, size_t size)
{
	int f, r, d;
	char *_data;
	size_t len;

	if (!eng)
		return -1;

	mfb_init_ring_ctl(&eng->req_ctl);
	mfb_set_ring_size(&eng->req_ctl, MFB_MAX_REQUEST_SIZE_PER_ENGINE);

	/* TODO: KE Risk : array out of bound */
	len = (size * MFB_MAX_FRAMES_PER_REQUEST) *
			MFB_MAX_REQUEST_SIZE_PER_ENGINE;
	_data = vmalloc(len);

	if (!_data) {
		LOG_ERR("[%s] vmalloc failed", __func__);
		return -1;
	}

	memset(_data, 0x0, len);
	LOG_INF("[%s]Engine struct total size is %zu", __func__, len);

	for (r = 0; r < MFB_MAX_REQUEST_SIZE_PER_ENGINE; r++) {
		mfb_init_request(&eng->reqs[r]);

		for (f = 0; f < MFB_MAX_FRAMES_PER_REQUEST; f++) {
			d = (r * MFB_MAX_FRAMES_PER_REQUEST + f) * size;
			mfb_set_frame_data(&eng->reqs[r].frames[f], &_data[d]);
		}
	}

	/* seqlock_init(&eng->seqlock); */
	local_irq_disable();
	write_seqlock(&eng->seqlock);
	eng->req_running = false;
	write_sequnlock(&eng->seqlock);
	local_irq_enable();

	/* init and inc for 1st request_handler use */
	init_completion(&eng->req_handler_done);
	complete(&eng->req_handler_done);

	return 0;
}
EXPORT_SYMBOL(mfb_register_requests);

signed int mfb_unregister_requests(struct engine_requests *eng)
{
	int f, r;

	if (!eng)
		return -1;

	vfree(eng->reqs[0].frames[0].data);

	for (r = 0; r < MFB_MAX_REQUEST_SIZE_PER_ENGINE; r++) {
		mfb_init_request(&eng->reqs[r]);

		for (f = 0; f < MFB_MAX_FRAMES_PER_REQUEST; f++)
			mfb_set_frame_data(&eng->reqs[r].frames[f], NULL);
	}

	local_irq_disable();
	write_seqlock(&eng->seqlock);
	eng->req_running = false;
	write_sequnlock(&eng->seqlock);
	local_irq_enable();

	return 0;
}
EXPORT_SYMBOL(mfb_unregister_requests);


int mfb_set_engine_ops(struct engine_requests *eng,
	const struct engine_ops *ops)
{
	if (!eng || !ops)
		return -1;

	eng->ops = ops;

	return 0;
}
EXPORT_SYMBOL(mfb_set_engine_ops);

bool mfb_request_running(struct engine_requests *eng)
{
	unsigned int seq;
	seqlock_t *lock;
	bool running;

	lock = &eng->seqlock;
	do {
		seq = read_seqbegin(lock);
		running = eng->req_running;
	} while (read_seqretry(lock, seq));

	LOG_DBG("[%s] engine running request(%d)\n", __func__, running);

	return running;
}
EXPORT_SYMBOL(mfb_request_running);

/*TODO: called in ENQUE_REQ */
signed int mfb_enque_request(struct engine_requests *eng, unsigned int fcnt,
						void *req, pid_t pid)
{
	unsigned int r;
	unsigned int f;
	unsigned int enqnum = 0;

	if (!eng)
		return -1;

	r = eng->req_ctl.wcnt;
	/* FIFO when wcnt starts from 0 */
	f = eng->reqs[r].fctl.wcnt;

	if (eng->reqs[r].state != REQUEST_STATE_EMPTY) {
		LOG_ERR("No empty requests available.");
		goto ERROR;
	}

	if (!eng->ops->req_enque_cb || !req) {
		LOG_ERR("NULL req_enque_cb or req");
		goto ERROR;
	}

	if (eng->ops->req_enque_cb(eng->reqs[r].frames, req)) {
		LOG_ERR("Failed to enque request, check cb");
		goto ERROR;
	}

	LOG_DBG("request(%d) enqued with %d frames", r, fcnt);
	for (f = 0; f < fcnt; f++)
		eng->reqs[r].frames[f].state = FRAME_STATUS_ENQUE;
	eng->reqs[r].state = REQUEST_STATE_PENDING;

	eng->reqs[r].pid = pid;

	eng->reqs[r].fctl.wcnt = fcnt;
	eng->reqs[r].fctl.size = fcnt;

	eng->req_ctl.wcnt = (r + 1) % MFB_MAX_REQUEST_SIZE_PER_ENGINE;

#if REQUEST_REGULATION
	/*
	 * pending request count for request base regulation
	 */
	for (r = 0; r < MFB_MAX_REQUEST_SIZE_PER_ENGINE; r++)
		if (eng->reqs[r].state == REQUEST_STATE_PENDING)
			enqnum++;
#else
	/*
	 * running frame count for frame base regulation
	 */
	for (r = 0; r < MFB_MAX_REQUEST_SIZE_PER_ENGINE; r++)
		for (f = 0; f < MFB_MAX_FRAMES_PER_REQUEST; f++)
			if (eng->reqs[r].frames[f].state ==
							FRAME_STATUS_RUNNING)
				enqnum++;
#endif
	return enqnum;
ERROR:
	return -1;
}
EXPORT_SYMBOL(mfb_enque_request);

/* ConfigWMFERequest / ConfigOCCRequest abstraction
 * TODO: locking should be here NOT camera_owe.c
 */
signed int mfb_request_handler(struct engine_requests *eng, spinlock_t *lock)
{
	unsigned int f, fn;
	unsigned int r;
	enum REQUEST_STATE_ENUM rstate;
	enum FRAME_STATUS_ENUM fstate;
	unsigned long flags;
	signed int ret = -1;

	if (!eng)
		return -1;

	LOG_DBG("[%s]waits for completion(%d).\n", __func__,
						eng->req_handler_done.done);
	wait_for_completion(&eng->req_handler_done);
	reinit_completion(&eng->req_handler_done);
	LOG_DBG("[%s]previous request completed, reset(%d).\n", __func__,
						eng->req_handler_done.done);

	spin_lock_irqsave(lock, flags);

	/*
	 * ioctl calls  enque_request() request wcnt inc
	 * if request_handler is NOT called after enque_request() in serial,
	 * wcnt may inc many times maore than 1
	 */
	r = eng->req_ctl.gcnt;
	LOG_DBG("[%s] processing request(%d)\n", __func__, r);

	rstate = eng->reqs[r].state;
#if REQUEST_REGULATION
	(void) fn;
	if (rstate != REQUEST_STATE_PENDING) {
		LOG_DBG("[%s]No pending request(%d), state:%d\n", __func__,
								r, rstate);
		write_seqlock(&eng->seqlock);
		eng->req_running = false;
		write_sequnlock(&eng->seqlock);

		spin_unlock_irqrestore(lock, flags);

		complete_all(&eng->req_handler_done);
		LOG_DBG("[%s]complete(%d)\n", __func__,
						eng->req_handler_done.done);

		return 0;
	}
	/* running request contains all running frames */
	eng->reqs[r].state = REQUEST_STATE_RUNNING;

	for (f = 0; f < MFB_MAX_FRAMES_PER_REQUEST; f++) {
		fstate = eng->reqs[r].frames[f].state;
		if (fstate == FRAME_STATUS_ENQUE) {
			LOG_DBG("[%s]Processing request(%d) of frame(%d)\n",
							__func__,  r, f);

			spin_unlock_irqrestore(lock, flags);
			ret = eng->ops->frame_handler(&eng->reqs[r].frames[f]);
			spin_lock_irqsave(lock, flags);
			if (ret)
				LOG_WRN("[%s]failed:frame %d of request %d",
							__func__, f, r);

			eng->reqs[r].frames[f].state = FRAME_STATUS_RUNNING;
		}
	}

	eng->req_ctl.gcnt = (r + 1) % MFB_MAX_REQUEST_SIZE_PER_ENGINE;
#else
	/*
	 * TODO: to prevent IRQ timing issue due to multi-frame requests,
	 * frame-based reguest handling should be used instead.
	 */
	if (eng->reqs[r].pending_run == false) {
		if (rstate != REQUEST_STATE_PENDING) {
			LOG_DBG("[%s]No pending request(%d), state:%d\n",
							__func__, r, rstate);
			write_seqlock(&eng->seqlock);
			eng->req_running = false;
			write_sequnlock(&eng->seqlock);

			spin_unlock_irqrestore(lock, flags);

			complete_all(&eng->req_handler_done);
			LOG_DBG("[%s]complete(%d)\n", __func__,
						eng->req_handler_done.done);

			return 0;
		}
		eng->reqs[r].pending_run = true;
		eng->reqs[r].state = REQUEST_STATE_RUNNING;
	} else
		if (rstate != REQUEST_STATE_RUNNING) {
			LOG_WRN(
			"[%s]No pending_run request(%d), state:%d\n", __func__,
								r, rstate);
			write_seqlock(&eng->seqlock);
			eng->req_running = false;
			write_sequnlock(&eng->seqlock);

			spin_unlock_irqrestore(lock, flags);

			complete_all(&eng->req_handler_done);
			LOG_DBG(
			"[%s]complete(%d)\n", __func__,
						eng->req_handler_done.done);
			return 0;
		}


	/* pending requets can have running frames */
	f = eng->reqs[r].fctl.gcnt;
	LOG_DBG("[%s]Iterating request(%d) of frame(%d)\n",
						__func__,  r, f);

	fstate = eng->reqs[r].frames[f].state;
	if (fstate == FRAME_STATUS_ENQUE) {
		LOG_DBG("[%s]Processing request(%d) of frame(%d)\n",
						__func__,  r, f);
		write_seqlock(&eng->seqlock);
		eng->req_running = true;
		write_sequnlock(&eng->seqlock);

		eng->reqs[r].frames[f].state = FRAME_STATUS_RUNNING;
		spin_unlock_irqrestore(lock, flags);
		ret = eng->ops->frame_handler(&eng->reqs[r].frames[f]);
			spin_lock_irqsave(lock, flags);
		if (ret)
			LOG_WRN("[%s]failed:frame %d of request %d",
						__func__, f, r);

	} else {
		spin_unlock_irqrestore(lock, flags);
		LOG_WRN("[%s]already running frame %d of request %d",
						__func__, f, r);
		complete_all(&eng->req_handler_done);
		LOG_DBG("[%s]complete(%d)\n", __func__,
						eng->req_handler_done.done);

		return 1;
	}

	fn = (f + 1) % MFB_MAX_FRAMES_PER_REQUEST;
	fstate = eng->reqs[r].frames[fn].state;
	if (fstate == FRAME_STATUS_EMPTY || fn == 0) {
		eng->reqs[r].pending_run = false;
	eng->req_ctl.gcnt = (r + 1) % MFB_MAX_REQUEST_SIZE_PER_ENGINE;
	} else
		eng->reqs[r].fctl.gcnt = fn;

#endif

	spin_unlock_irqrestore(lock, flags);

	complete_all(&eng->req_handler_done);
	LOG_DBG("complete_all done = %d\n", eng->req_handler_done.done);

	return 1;

}
EXPORT_SYMBOL(mfb_request_handler);


int mfb_update_request(struct engine_requests *eng, pid_t *pid)
{
	unsigned int i, f, n;
	int req_jobs = -1;

	if (!eng)
		return -1;

	/* TODO: request ring */
	for (i = eng->req_ctl.icnt; i < MFB_MAX_REQUEST_SIZE_PER_ENGINE; i++) {
		/* find 1st running request */
		if (eng->reqs[i].state != REQUEST_STATE_RUNNING)
			continue;

		/* find 1st running frame, f. */
		for (f = 0; f < MFB_MAX_FRAMES_PER_REQUEST; f++) {
			if (eng->reqs[i].frames[f].state ==
							FRAME_STATUS_RUNNING)
				break;
		}
		/* TODO: no running frame in a running request */
		if (f == MFB_MAX_FRAMES_PER_REQUEST) {
			LOG_ERR(
			"[%s]No running frames in a running request(%d).",
								__func__, i);
			break;
		}

		eng->reqs[i].frames[f].state = FRAME_STATUS_FINISHED;
		LOG_INF("[%s]request %d of frame %d finished.\n",
							__func__, i, f);
		/*TODO: to obtain statistics */
		if (!eng->ops->req_feedback_cb) {
			LOG_DBG("NULL req_feedback_cb");
			goto NO_FEEDBACK;
		}

		if (eng->ops->req_feedback_cb(&eng->reqs[i].frames[f])) {
			LOG_ERR("Failed to feedback statistics, check cb");
			goto NO_FEEDBACK;
		}
NO_FEEDBACK:
		n = f + 1;
		if ((n == MFB_MAX_FRAMES_PER_REQUEST) ||
			((n < MFB_MAX_FRAMES_PER_REQUEST) &&
			(eng->reqs[i].frames[n].state == FRAME_STATUS_EMPTY))) {

			req_jobs = 0;
			(*pid) = eng->reqs[i].pid;

			eng->reqs[i].state = REQUEST_STATE_FINISHED;
			eng->req_ctl.icnt = (eng->req_ctl.icnt + 1) %
						MFB_MAX_REQUEST_SIZE_PER_ENGINE;
		} else {
			LOG_INF(
			"[%s]more frames left of request(%d/%d).", __func__,
							i, eng->req_ctl.icnt);
		}
		break;
	}

	if (i == MFB_MAX_REQUEST_SIZE_PER_ENGINE)
		LOG_ERR("no pending request was found(%d).\n", i);

	return req_jobs;
}
EXPORT_SYMBOL(mfb_update_request);

/*TODO: called in DEQUE_REQ */
signed int mfb_deque_request(
	struct engine_requests *eng, unsigned int *fcnt, void *req)
{
	unsigned int r;
	unsigned int f;

	if (!eng)
		return -1;

	r = eng->req_ctl.rcnt;
	/* FIFO when rcnt starts from 0 */
	f = eng->reqs[r].fctl.rcnt;

	if (eng->reqs[r].state != REQUEST_STATE_FINISHED) {
		LOG_ERR("[%s]Request(%d) NOT finished", __func__, r);
		goto ERROR;
	}

	*fcnt = eng->reqs[r].fctl.size;
	LOG_DBG("[%s]deque request(%d) has %d frames", __func__, r, *fcnt);

	if (!eng->ops->req_deque_cb || !req) {
		LOG_ERR("[%s]NULL req_deque_cb/req", __func__);
		goto ERROR;
	}

	if (eng->ops->req_deque_cb(eng->reqs[r].frames, req)) {
		LOG_ERR("[%s]Failed to deque, check req_deque_cb", __func__);
		goto ERROR;
	}

	for (f = 0; f < *fcnt; f++)
		eng->reqs[r].frames[f].state = FRAME_STATUS_EMPTY;
	eng->reqs[r].state = REQUEST_STATE_EMPTY;
	eng->reqs[r].pid = 0;

	eng->reqs[r].fctl.wcnt = 0;
	eng->reqs[r].fctl.rcnt = 0;
	eng->reqs[r].fctl.icnt = 0;
	eng->reqs[r].fctl.gcnt = 0;
	eng->reqs[r].fctl.size = 0;

	eng->req_ctl.rcnt = (r + 1) % MFB_MAX_REQUEST_SIZE_PER_ENGINE;

	return 0;
ERROR:
	return -1;
}
EXPORT_SYMBOL(mfb_deque_request);

signed int mfb_request_dump(struct engine_requests *eng)
{
	unsigned int r;
	unsigned int f;

	LOG_ERR("[%s] +\n", __func__);

	if (!eng) {
		LOG_ERR("[%s]can't dump NULL engine", __func__);
		return -1;
	}

	LOG_ERR("req_ctl:wc:%d, gc:%d, ic:%d, rc:%d, data:%p\n",
				eng->req_ctl.wcnt,
				eng->req_ctl.gcnt,
				eng->req_ctl.icnt,
				eng->req_ctl.rcnt,
				eng->reqs[0].frames[0].data);

	for (r = 0; r < MFB_MAX_REQUEST_SIZE_PER_ENGINE; r++) {
		LOG_ERR(
		"R[%d].sta:%d, pid:0x%08X, wc:%d, gc:%d, ic:%d, rc:%d\n",
					r,
					eng->reqs[r].state,
					eng->reqs[r].pid,
					eng->reqs[r].fctl.wcnt,
					eng->reqs[r].fctl.gcnt,
					eng->reqs[r].fctl.icnt,
					eng->reqs[r].fctl.rcnt);

		for (f = 0; f < MFB_MAX_FRAMES_PER_REQUEST; f += 2) {
			LOG_ERR("F[%d].state:%d, F[%d].state:%d\n",
			     f, eng->reqs[r].frames[f].state,
			     f + 1, eng->reqs[r].frames[f + 1].state);
		}
	}

	LOG_ERR("[%s] -\n", __func__);

	return 0;
}
EXPORT_SYMBOL(mfb_request_dump);

#ifndef TODO
static int __init egnreq_init(void)
{
	int ret = 0;

	LOG_INF("engine_request module loaded");
	return ret;
}

static void __exit egnreq_exit(void)
{
	LOG_INF("engine_request module unloaded");

}

module_init(egnreq_init);
module_exit(egnreq_exit);
#endif
