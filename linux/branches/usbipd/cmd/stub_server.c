/*
 * $Id$
 *
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <unistd.h>
#include <netdb.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef HAVE_LIBWRAP
#include <tcpd.h>
#endif

#define _GNU_SOURCE
#include <getopt.h>
#include <signal.h>

#include "usbip.h"
#include "usbip_network.h"

#include <glib.h>
#include <sys/time.h>

static const char version[] = PACKAGE_STRING
	" ($Id$)";

/*
 * A basic header followed by other additional headers.
 */
struct usbip_header_basic {
#define USBIP_CMD_SUBMIT	0x0001
#define USBIP_CMD_UNLINK	0x0002
#define USBIP_RET_SUBMIT	0x0003
#define USBIP_RET_UNLINK	0x0004
#define USBIP_RESET_DEV		0xFFFF
	unsigned int command;

	 /* sequencial number which identifies requests.
	  * incremented per connections */
	unsigned int seqnum;

	/* devid is used to specify a remote USB device uniquely instead
	 * of busnum and devnum in Linux. In the case of Linux stub_driver,
	 * this value is ((busnum << 16) | devnum) */
	unsigned int devid;  

#define USBIP_DIR_OUT	0
#define USBIP_DIR_IN 	1
	unsigned int direction;
	unsigned int ep;     /* endpoint number */
} __attribute__ ((packed));

/*
 * An additional header for a CMD_SUBMIT packet.
 */
struct usbip_header_cmd_submit {
	/* these values are basically the same as in a URB. */

	/* the same in a URB. */
	unsigned int transfer_flags;

	/* set the following data size (out),
	 * or expected reading data size (in) */
	int transfer_buffer_length;

	/* it is difficult for usbip to sync frames (reserved only?) */
	int start_frame;

	/* the number of iso descriptors that follows this header */
	int number_of_packets;

	/* the maximum time within which this request works in a host
	 * controller of a server side */
	int interval;

	/* set setup packet data for a CTRL request */
	unsigned char setup[8];
}__attribute__ ((packed));

/*
 * An additional header for a RET_SUBMIT packet.
 */
struct usbip_header_ret_submit {
	int status;
	int actual_length; /* returned data length */
	int start_frame; /* ISO and INT */
	int number_of_packets;  /* ISO only */
	int error_count; /* ISO only */
}__attribute__ ((packed));

/*
 * An additional header for a CMD_UNLINK packet.
 */
struct usbip_header_cmd_unlink {
	unsigned int seqnum; /* URB's seqnum which will be unlinked */
}__attribute__ ((packed));


/*
 * An additional header for a RET_UNLINK packet.
 */
struct usbip_header_ret_unlink {
	int status;
}__attribute__ ((packed));


/* the same as usb_iso_packet_descriptor but packed for pdu */
struct usbip_iso_packet_descriptor {
	unsigned int offset;
	unsigned int length;            /* expected length */
	unsigned int actual_length;
	unsigned int status;
}__attribute__ ((packed));


/*
 * All usbip packets use a common header to keep code simple.
 */
struct usbip_header {
	struct usbip_header_basic base;

	union {
		struct usbip_header_cmd_submit	cmd_submit;
		struct usbip_header_ret_submit	ret_submit;
		struct usbip_header_cmd_unlink	cmd_unlink;
		struct usbip_header_ret_unlink	ret_unlink;
	} u;
}__attribute__ ((packed));

#define USBDEVFS_MAX_LEN 16384

void show_time(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	dbg("\n%lu.%lu: ",tv.tv_sec, tv.tv_usec);
}

static int send_reply_devlist(int sockfd)
{
	int ret;
	struct usbip_exported_device *edev;
	struct op_devlist_reply reply;


	reply.ndev = 0;

	/* how many devices are exported ? */
	dlist_for_each_data(stub_driver->edev_list, edev, struct usbip_exported_device) {
		reply.ndev += 1;
	}

	dbg("%d devices are exported", reply.ndev);

	ret = usbip_send_op_common(sockfd, OP_REP_DEVLIST,  ST_OK);
	if (ret < 0) {
		err("send op_common");
		return ret;
	}

	PACK_OP_DEVLIST_REPLY(1, &reply);

	ret = usbip_send(sockfd, (void *) &reply, sizeof(reply));
	if (ret < 0) {
		err("send op_devlist_reply");
		return ret;
	}

	dlist_for_each_data(stub_driver->edev_list, edev, struct usbip_exported_device) {
		struct usb_device pdu_udev;

		dump_usb_device(&edev->udev);
		memcpy(&pdu_udev, &edev->udev, sizeof(pdu_udev));
		pack_usb_device(1, &pdu_udev);

		ret = usbip_send(sockfd, (void *) &pdu_udev, sizeof(pdu_udev));
		if (ret < 0) {
			err("send pdu_udev");
			return ret;
		}

		for (int i=0; i < edev->udev.bNumInterfaces; i++) {
			struct usb_interface pdu_uinf;

			dump_usb_interface(&edev->uinf[i]);
			memcpy(&pdu_uinf, &edev->uinf[i], sizeof(pdu_uinf));
			pack_usb_interface(1, &pdu_uinf);

			ret = usbip_send(sockfd, (void *) &pdu_uinf, sizeof(pdu_uinf));
			if (ret < 0) {
				err("send pdu_uinf");
				return ret;
			}
		}
	}

	return 0;
}


static int recv_request_devlist(int sockfd)
{
	int ret;
	struct op_devlist_request req;

	bzero(&req, sizeof(req));

	ret = usbip_recv(sockfd, (void *) &req, sizeof(req));
	if (ret < 0) {
		err("recv devlist request");
		return -1;
	}

	ret = send_reply_devlist(sockfd);
	if (ret < 0) {
		err("send devlist reply");
		return -1;
	}

	return 0;
}

static int recv_request_devlist(int sockfd);
static int recv_request_import(int sockfd);

static void correct_endian_basic(struct usbip_header_basic *base, int send)
{
	if (send) {
		base->command	= htonl(base->command);
		base->seqnum	= htonl(base->seqnum);
		base->devid	= htonl(base->devid);
		base->direction	= htonl(base->direction);
		base->ep	= htonl(base->ep);
	} else {
		base->command	= ntohl(base->command);
		base->seqnum	= ntohl(base->seqnum);
		base->devid	= ntohl(base->devid);
		base->direction	= ntohl(base->direction);
		base->ep	= ntohl(base->ep);
	}
}

void cpu_to_be32s(int *a)
{
	*a=htonl((unsigned int)*a);
}

void be32_to_cpus(int *a)
{
	*a=ntohl((unsigned int)*a);
}

static void correct_endian_cmd_submit(struct usbip_header_cmd_submit *pdu, int send)
{
	if (send) {
		pdu->transfer_flags = htonl(pdu->transfer_flags);

		cpu_to_be32s(&pdu->transfer_buffer_length);
		cpu_to_be32s(&pdu->start_frame);
		cpu_to_be32s(&pdu->number_of_packets);
		cpu_to_be32s(&pdu->interval);
	} else {
		pdu->transfer_flags = ntohl(pdu->transfer_flags);

		be32_to_cpus(&pdu->transfer_buffer_length);
		be32_to_cpus(&pdu->start_frame);
		be32_to_cpus(&pdu->number_of_packets);
		be32_to_cpus(&pdu->interval);
	}
}

static void correct_endian_ret_submit(struct usbip_header_ret_submit *pdu, int send)
{
	if (send) {
		cpu_to_be32s(&pdu->status);
		cpu_to_be32s(&pdu->actual_length);
		cpu_to_be32s(&pdu->start_frame);
		cpu_to_be32s(&pdu->error_count);
	} else {
		be32_to_cpus(&pdu->status);
		be32_to_cpus(&pdu->actual_length);
		be32_to_cpus(&pdu->start_frame);
		be32_to_cpus(&pdu->error_count);
	}
}

static void correct_endian_cmd_unlink(struct usbip_header_cmd_unlink *pdu, int send)
{
	if (send)
		pdu->seqnum = htonl(pdu->seqnum);
	else
		pdu->seqnum = ntohl(pdu->seqnum);
}

static void correct_endian_ret_unlink(struct usbip_header_ret_unlink *pdu, int send)
{
	if (send)
		cpu_to_be32s(&pdu->status);
	else
		be32_to_cpus(&pdu->status);
}

void usbip_header_correct_endian(struct usbip_header *pdu, int send)
{
	unsigned int cmd = 0;

	if (send)
		cmd = pdu->base.command;

	correct_endian_basic(&pdu->base, send);

	if (!send)
		cmd = pdu->base.command;

	switch (cmd) {
		case USBIP_RESET_DEV:
			break;
		case USBIP_CMD_SUBMIT:
			correct_endian_cmd_submit(&pdu->u.cmd_submit, send);
			break;
		case USBIP_RET_SUBMIT:
			correct_endian_ret_submit(&pdu->u.ret_submit, send);
			break;

		case USBIP_CMD_UNLINK:
			correct_endian_cmd_unlink(&pdu->u.cmd_unlink, send);
			break;
		case USBIP_RET_UNLINK:
			correct_endian_ret_unlink(&pdu->u.ret_unlink, send);
			break;

		default:
			/* NOTREACHED */
			err("unknown command in pdu header: %d", cmd);
			//BUG();
	}
}

static inline int is_ctrl_ep(unsigned  int ep_num)
{
	return ((ep_num & 0x7F)== 0);
}

static inline int is_out_ep(unsigned  int ep_num)
{
	return (ep_num & 0x80)==0;
}

static inline int is_in_ep(unsigned  int ep_num)
{
	return ep_num & 0x80;
}

static void dump_urb(struct usbdevfs_urb *urb)
{
	dbg("type:%d\n"
		"endpoint:%d\n"
		"stauts:%d\n"
		"flags:%d\n"
		"buffer:%p\n"
		"buffer_length:%d\n"
		"actual_length:%d\n"
		"start_frame:%d\n"
		"number_of_packets:%d\n"
		"error_count:%d\n"
		"signr:%u\n"
		"usercontext:%p\n",
		urb->type,
		urb->endpoint,
		urb->status,
		urb->flags,
		urb->buffer,
		urb->buffer_length,
		urb->actual_length,
		urb->start_frame,
		urb->number_of_packets,
		urb->error_count,
		urb->signr,
		urb->usercontext);
}

static void usbip_dump_header(struct usbip_header *pdu)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	dbg("%lu.%lu: ",tv.tv_sec, tv.tv_usec);
	dbg("BASE: cmd %u seq %u devid %u dir %u ep %u\n",
			pdu->base.command,
			pdu->base.seqnum,
			pdu->base.devid,
			pdu->base.direction,
			pdu->base.ep);

	switch(pdu->base.command) {
		case USBIP_RESET_DEV:
			dbg("CMD_RESET:\n");
		case USBIP_CMD_SUBMIT:
			dbg("CMD_SUBMIT: x_flags %u x_len %u sf %u #p %u iv %u\n",
					pdu->u.cmd_submit.transfer_flags,
					pdu->u.cmd_submit.transfer_buffer_length,
					pdu->u.cmd_submit.start_frame,
					pdu->u.cmd_submit.number_of_packets,
					pdu->u.cmd_submit.interval);
					break;
		case USBIP_CMD_UNLINK:
			dbg("CMD_UNLINK: seq %u\n", pdu->u.cmd_unlink.seqnum);
			break;
		case USBIP_RET_SUBMIT:
			dbg("RET_SUBMIT: st %d al %u sf %d ec %d\n",
					pdu->u.ret_submit.status,
					pdu->u.ret_submit.actual_length,
					pdu->u.ret_submit.start_frame,
					pdu->u.ret_submit.error_count);
			break;
		case USBIP_RET_UNLINK:
			dbg("RET_UNLINK: status %d\n", pdu->u.ret_unlink.status);
			break;
		default:
			/* NOT REACHED */
			dbg("UNKNOWN\n");
	}
}

AsyncURB * find_aurb(struct dlist * dlist, int seqnum, int sub_seqnum)
{
	AsyncURB * aurb;
	dlist_for_each_data(dlist, aurb, AsyncURB){
		if(aurb->seqnum==seqnum&&
				aurb->sub_seqnum==sub_seqnum){
			return aurb;
		}
	}
	return NULL;
}

int delete_aurb(struct dlist * dlist, int seqnum, int sub_seqnum)
{
	AsyncURB *aurb;
	dlist_for_each_data(dlist, aurb, AsyncURB){
		if(aurb->seqnum==seqnum&&
				aurb->sub_seqnum==sub_seqnum){
			dlist_delete_before(dlist);
			return 0;
		}
	}
	return -1;
}

static int valid_request(struct usbip_exported_device *edev, 
			struct usbip_header *pdu)
{
	struct usb_device *dev = &edev->udev;
	
	if(pdu->base.devid!=((dev->busnum<<16)|dev->devnum))
		return 0;
	return 1;
}

static void setup_ret_submit_pdu(struct usbip_header *rpdu, AsyncURB *aurb)
{
	struct usbdevfs_urb *urb=&aurb->urb;

	rpdu->base.seqnum = aurb->seqnum;
	rpdu->base.command = USBIP_RET_SUBMIT;

	rpdu->u.ret_submit.status		= urb->status;
	rpdu->u.ret_submit.actual_length = aurb->ret_len;
	rpdu->u.ret_submit.start_frame	= urb->start_frame;
	rpdu->u.ret_submit.error_count	= urb->error_count;
}

static int stub_send_ret_submit(struct usbip_exported_device *edev)
{
	AsyncURB *aurb, *last_aurb;
	struct usbip_header pdu_header;
	struct usbdevfs_urb *urb;
	int ret, len;
	char buf[100000];
	int is_ctrl;
	while (1){
		ret = ioctl(edev->usbfs_fd, USBDEVFS_REAPURBNDELAY, &aurb);
		urb = &aurb->urb;
	        if (ret < 0) {
			if (errno == EAGAIN)
        	        	return 0;
            		g_error("husb: reap urb failed errno %d %m\n", errno);
        	}
		if(aurb->sub_seqnum){
			dbg("splited urb ret");
			last_aurb = find_aurb(edev->processing_urbs, 
					aurb->seqnum, 0);
			if(last_aurb==NULL){
				dbg("perhaps unlinked urb");
			} else {
				last_aurb->ret_len += urb->actual_length;
			}
			delete_aurb(edev->processing_urbs, aurb->seqnum,
					aurb->sub_seqnum);
			continue;
		}
		is_ctrl=is_ctrl_ep(urb->endpoint);
		memset(&pdu_header, 0, sizeof(pdu_header));

		aurb->ret_len += urb->actual_length;
		setup_ret_submit_pdu(&pdu_header, aurb);
		if(urb->status<0){
			dbg("faint error %d\n", urb->endpoint);
//			ep=urb->endpoint;
//			ret=ioctl(edev->usbfs_fd, USBDEVFS_CLEAR_HALT, 
//					&ep);
//			dbg("clear halt ret %d\n", ret);
		}
		usbip_dump_header(&pdu_header);
		usbip_header_correct_endian(&pdu_header, 1);
		dump_urb(urb);
		memcpy(buf, &pdu_header, sizeof(pdu_header));
		len=sizeof(pdu_header);
		if(is_in_ep(urb->endpoint)){
			memcpy(buf+sizeof(pdu_header), aurb->data+(is_ctrl?8:0),
					aurb->ret_len);
			len+=aurb->ret_len;
		}
		//fixme sndmsg
		ret=usbip_send(edev->client_fd, buf, len);
		if(ret!=len){
			g_error("can't send pdu_header");
		}
		delete_aurb(edev->processing_urbs, aurb->seqnum, 0);
	}
	return 0;
}

static int stub_send_ret_unlink(int fd, int seqnum, int status)
{
	int ret;
	struct usbip_header pdu_header;
	memset(&pdu_header, 0, sizeof(pdu_header));
	pdu_header.base.seqnum = seqnum;
	pdu_header.base.command = USBIP_RET_UNLINK;
	pdu_header.u.ret_unlink.status = status;
	usbip_header_correct_endian(&pdu_header, 1);
	ret = usbip_send(fd, &pdu_header, sizeof(pdu_header));
	if (ret != sizeof(pdu_header))
		g_error("send ret");
	return 0;
}

static int cancel_urb(struct dlist * processing_urbs, unsigned int seqnum, int fd);

static int stub_recv_cmd_unlink(struct usbip_exported_device *edev,
		struct usbip_header *pdu)
{
	int ret;
	show_time();
	ret=cancel_urb(edev->processing_urbs,
			pdu->u.cmd_unlink.seqnum,
			edev->usbfs_fd);
	stub_send_ret_unlink(edev->client_fd, pdu->base.seqnum, -ECONNRESET);
	return 0;
}



unsigned int get_transfer_flag(unsigned  int flag)
{
	//FIXME  now uurb flag = flag in kernel, but it perhaps will change
	return flag &(
	        USBDEVFS_URB_ISO_ASAP|
		USBDEVFS_URB_SHORT_NOT_OK|
		USBDEVFS_URB_NO_FSBR|
		USBDEVFS_URB_ZERO_PACKET|
		USBDEVFS_URB_NO_INTERRUPT);
}

static int cancel_urb(struct dlist * processing_urbs, unsigned int seqnum, int fd)
{
	AsyncURB * aurb;
	int ret=-1;

	dlist_for_each_data(processing_urbs, aurb, AsyncURB) {
		if (seqnum == aurb->seqnum){
			dbg("found seqnum %d, subseqnum %d\n", seqnum,
					aurb->sub_seqnum);
			ret = ioctl(fd, USBDEVFS_DISCARDURB, aurb);
			if(ret <0){
				dbg("discard urb ret %d %m\n", ret);
			} else {
				if(aurb->sub_seqnum==0)
					aurb->sub_seqnum = 0xffff;
				dbg("discard urb success");
				//dlist_delete_before(processing_urbs);
			}
			ret = 0;
		}
	}
	return ret;
}

//split urb 
int submit_urb(int fd, AsyncURB *aurb, struct dlist * processing_urbs)
{
	int all_len, left_len, this_len, ret, sub_seqnum=0;
	AsyncURB *t_aurb;
	struct usbdevfs_urb *urb;
	if(aurb->data_len > USBDEVFS_MAX_LEN && is_ctrl_ep(aurb->urb.endpoint))
		g_error("faint, why so big urb?");
	left_len=all_len=aurb->data_len;
	while(left_len){
		this_len = (left_len>USBDEVFS_MAX_LEN)?
			USBDEVFS_MAX_LEN:left_len;
		left_len-=this_len;
		sub_seqnum++;
		if(left_len==0){
			//the last urb
			aurb->urb.buffer = aurb->data + all_len - this_len;
			aurb->urb.buffer_length = this_len;
			dump_urb(&aurb->urb);
			ret = ioctl(fd, USBDEVFS_SUBMITURB, &aurb->urb);
			if(ret<0){
				err("ioctl last ret %d %m\n", ret);
				goto err;
			}
			dlist_unshift(processing_urbs, (void *)aurb);
			return 0;
		}
		t_aurb=calloc(1, sizeof(*aurb));
		if(NULL==aurb){
			err("malloc\n");
			ret = -1;
			goto err;
		}
		memcpy(t_aurb, aurb, sizeof(*aurb));
		t_aurb->sub_seqnum = sub_seqnum;
		urb=&t_aurb->urb;
		urb->buffer = aurb->data + (all_len-left_len-this_len);
		urb->buffer_length = this_len;
		ret = ioctl(fd, USBDEVFS_SUBMITURB, urb);
		if(ret<0){
			err("ioctl mid ret %d %m\n", ret);
			goto err;
		}
		dlist_unshift(processing_urbs, (void *)t_aurb);
	}
	g_error("never reach here");
err:
	if(sub_seqnum>1)
		cancel_urb(processing_urbs, aurb->seqnum, fd);
	return ret;
}

static void stub_recv_cmd_submit(struct usbip_exported_device *edev,
		struct usbip_header *pdu)
{
	AsyncURB *aurb;
	struct usbdevfs_urb *urb;
	int ret, data_len, is_ctrl;

	is_ctrl = is_ctrl_ep(pdu->base.ep);

	if (pdu->u.cmd_submit.transfer_buffer_length > 0||is_ctrl) {
		data_len = pdu->u.cmd_submit.transfer_buffer_length + 
			(is_ctrl?8:0);
	} else
		data_len = 0;
	aurb=malloc(sizeof(*aurb)+data_len);
	if(NULL==aurb)
		g_error("malloc");
	memset(aurb, 0, sizeof(*aurb));
	aurb->data = (char *)(aurb+1);
	aurb->data_len = data_len;

	aurb->seqnum = pdu->base.seqnum;
	urb=&aurb->urb;
	urb->endpoint =pdu->base.ep;
	if(pdu->base.direction == USBIP_DIR_IN)
		urb->endpoint|=0x80;
	else
		urb->endpoint&=0x7F;

	//FIXME isopipe
	
	if(is_ctrl){
		memcpy(aurb->data, pdu->u.cmd_submit.setup, 8);
	}
	if(pdu->base.direction==USBIP_DIR_OUT && 
		pdu->u.cmd_submit.transfer_buffer_length > 0){
		ret=usbip_recv(edev->client_fd, aurb->data + 
					(is_ctrl?8:0), 
					pdu->u.cmd_submit.transfer_buffer_length);
		if(ret!=pdu->u.cmd_submit.transfer_buffer_length)
			g_error("recv pdu data");
	}

	urb->flags = get_transfer_flag(pdu->u.cmd_submit.transfer_flags);
	urb->start_frame = pdu->u.cmd_submit.start_frame;
	urb->number_of_packets = pdu->u.cmd_submit.number_of_packets;

	/* no need to submit an intercepted request, but harmless? */
	//FIXME
	//tweak_special_requests(pdu->);
	if(is_ctrl)
		urb->type = USBDEVFS_URB_TYPE_CONTROL;
	else
		urb->type = USBDEVFS_URB_TYPE_BULK;

	dump_urb(urb);
	ret = submit_urb(edev->usbfs_fd, aurb, edev->processing_urbs);
	if(ret<0){
		dbg("submit ret %d %d %m\n",ret, errno);
		g_error("submit urb");
	}
	/* urb is now ready to submit */
	return;
}

static void reset_dev(struct usbip_exported_device *edev, struct usbip_header *req)
{
	int ret;
	struct usbip_header reply;
	ret = ioctl(edev->usbfs_fd, USBDEVFS_RESET);
	memcpy(&reply, req, sizeof(reply));
	usbip_header_correct_endian(&reply, 1);
	ret = usbip_send(edev->client_fd, &reply, sizeof(reply));
	if (ret != sizeof(reply))
		g_error("send ret");
	return;
}

static int recv_client_pdu(struct usbip_exported_device *edev,int sockfd)
{
	int ret;
	struct usbip_header pdu;

	memset(&pdu, 0, sizeof(pdu));
	ret = usbip_recv(sockfd, &pdu, sizeof(pdu));
	if (ret !=sizeof(pdu))
		g_error("recv a header, %d", ret);

	usbip_header_correct_endian(&pdu, 0);
	dbg("recv header %d\n",ret);
	usbip_dump_header(&pdu);
	if (!valid_request(edev, &pdu)) {
		g_error("recv invalid request\n");
		return 0;
	}
	switch (pdu.base.command) {
		case USBIP_RESET_DEV:
			reset_dev(edev, &pdu);
			break;
		case USBIP_CMD_UNLINK:
			stub_recv_cmd_unlink(edev, &pdu);
			break;

		case USBIP_CMD_SUBMIT:
			stub_recv_cmd_submit(edev, &pdu);
			break;

		default:
			/* NOTREACHED */
			err("unknown pdu\n");
			return -1;
	}
	return 0;
}

void un_imported_dev(struct usbip_exported_device *edev)
{

	AsyncURB * aurb;
	g_source_remove(edev->client_gio_id);
	g_source_remove(edev->usbfs_gio_id);
	close(edev->client_fd);
	edev->client_fd=-1;
	edev->status = SDEV_ST_AVAILABLE;

	/* Clear all of submited urb */
	dlist_for_each_data(edev->processing_urbs, aurb, AsyncURB) {
		ioctl(edev->usbfs_fd, USBDEVFS_DISCARDURB, aurb);
	}
	while(0==ioctl(edev->usbfs_fd, USBDEVFS_REAPURBNDELAY, &aurb));
	dlist_destroy(edev->processing_urbs);
	edev->processing_urbs = dlist_new(sizeof(AsyncURB));
        if(!edev->processing_urbs)
		g_error("malloc");
	/*  we perhaps should reset device
	ioctl(edev->usbfs_fd, USBDEVFS_RESET);
	*/
}

gboolean process_client_pdu(GIOChannel *gio, GIOCondition condition, gpointer data)
{
	int ret;
	int client_fd;
	struct usbip_exported_device *edev
			= (struct usbip_exported_device *) data;

	if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)){
		un_imported_dev(edev);
		return 0;
	}

	if (condition & G_IO_IN) {
		client_fd = g_io_channel_unix_get_fd(gio);
		if(client_fd != edev->client_fd)
			g_error("fd corrupt?");
		ret = recv_client_pdu(edev, client_fd);
		if (ret < 0)
			err("process recieved pdu");
	}
	return TRUE;
}

gboolean process_device_urb(GIOChannel *gio, GIOCondition condition, gpointer data)
{
	int fd;

	if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL))
		g_error("unknown condition 2");


	if (condition & G_IO_OUT) {
		struct usbip_exported_device *edev 
			= (struct usbip_exported_device *) data;
		fd = g_io_channel_unix_get_fd(gio);
		if(fd != edev->usbfs_fd)
			g_error("fd corrupt?");
		stub_send_ret_submit(edev);
	}
	return TRUE;
}

static int recv_request_export(int sockfd)
{
	int ret;
	struct op_export_request req;
	struct op_common reply;
	struct usbip_exported_device *edev;
	int found = 0;
	struct sockaddr sa;
	socklen_t len = sizeof(sa);

	bzero(&sa, sizeof(sa));
	ret = getpeername(sockfd,(struct sockaddr *)&sa,  &len);
	if(ret<0){
		g_error("can't getpeername");
	}
	if((sa.sa_family ==AF_INET6 
	  &&!memcmp(&((struct sockaddr_in6 *)&sa)->sin6_addr, &in6addr_loopback
		  ,sizeof(in6addr_loopback)))
	 ){
		dbg("%d\n",sa.sa_family);
		dbg(" connect from localhost/tcp");
//		return -1;
	}
	bzero(&req, sizeof(req));
	bzero(&reply, sizeof(reply));

	ret = usbip_recv(sockfd, (void *) &req, sizeof(req));
	if (ret < 0) {
		err("recv export request");
		return -1;
	}
	PACK_OP_EXPORT_REQUEST(0, &req);

	dlist_for_each_data(stub_driver->edev_list, edev, struct usbip_exported_device) {
		if (!strncmp(req.udev.busid, edev->udev.busid, SYSFS_BUS_ID_SIZE)) {
			dbg("found requested device %s", req.udev.busid);
			found = 1;
			break;
		}
	}
	if (!found) {
		ret = export_device(req.udev.busid);
	} else {
		ret = 0;
	}
	ret = usbip_send_op_common(sockfd, OP_REP_EXPORT, (ret==0 ? ST_OK : ST_NA));
	if (ret < 0) {
		err("send import reply");
		return -1;
	}
	return 0;
}



static int recv_request_import(int sockfd)
{
	int ret;
	struct op_import_request req;
	struct op_common reply;
	struct usbip_exported_device *edev;
	int found = 0;
	int error = 0;
	GIOChannel *gio;

	bzero(&req, sizeof(req));
	bzero(&reply, sizeof(reply));

	ret = usbip_recv(sockfd, (void *) &req, sizeof(req));
	if (ret < 0) {
		err("recv import request");
		return -1;
	}

	PACK_OP_IMPORT_REQUEST(0, &req);

	dlist_for_each_data(stub_driver->edev_list, edev, struct usbip_exported_device) {
		if (!strncmp(req.busid, edev->udev.busid, SYSFS_BUS_ID_SIZE)) {
			dbg("found requested device %s", req.busid);
			found = 1;
			break;
		}
	}

	if (found) {
		/* should set TCP_NODELAY for usbip */
		usbip_set_nodelay(sockfd);

		/* export_device needs a TCP/IP socket descriptor */
		ret = usbip_stub_export_device(edev);
		if (ret < 0)
			error = 1;
		edev->status = SDEV_ST_USED;
		edev->client_fd = sockfd;
		gio = g_io_channel_unix_new(sockfd);
		edev->client_gio_id = g_io_add_watch(gio,
				(G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL),
                                process_client_pdu, edev);
		g_io_channel_unref(gio);
		gio = g_io_channel_unix_new(edev->usbfs_fd);
		edev->usbfs_gio_id = g_io_add_watch(gio,
				(G_IO_OUT | G_IO_ERR | G_IO_HUP | G_IO_NVAL),
                                process_device_urb, edev);
		g_io_channel_unref(gio);

	} else {
		info("not found requested device %s", req.busid);
		error = 1;
	}
	ret = usbip_send_op_common(sockfd, OP_REP_IMPORT, (!error ? ST_OK : ST_NA));
	if (ret < 0) {
		err("send import reply");
		return -1;
	}

	if (!error) {
		struct usb_device pdu_udev;

		memcpy(&pdu_udev, &edev->udev, sizeof(pdu_udev));
		pack_usb_device(1, &pdu_udev);

		ret = usbip_send(sockfd, (void *) &pdu_udev, sizeof(pdu_udev));
		if (ret < 0) {
			err("send devinfo");
			return -1;
		}
	}

	return 0;
}

static int recv_pdu(int sockfd)
{
	int ret;
	uint16_t code = OP_UNSPEC;


	ret = usbip_recv_op_common(sockfd, &code);
	if (ret < 0) {
		err("recv op_common, %d", ret);
		return ret;
	}

	switch(code) {
		case OP_REQ_DEVLIST:
			ret = recv_request_devlist(sockfd);
			close(sockfd);
			break;

		case OP_REQ_IMPORT:
			ret = recv_request_import(sockfd);
			break;

		case OP_REQ_EXPORT:
			ret = recv_request_export(sockfd);
			close(sockfd);
			break;

		case OP_REQ_DEVINFO:
		case OP_REQ_CRYPKEY:

		default:
			close(sockfd);
			err("unknown op_code, %d", code);
			ret = -1;
	}


	return ret;
}




static void log_addrinfo(struct addrinfo *ai)
{
	int ret;
	char hbuf[NI_MAXHOST];
	char sbuf[NI_MAXSERV];

	ret = getnameinfo(ai->ai_addr, ai->ai_addrlen, hbuf, sizeof(hbuf),
			sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
	if (ret)
		err("getnameinfo, %s", gai_strerror(ret));

	info("listen at [%s]:%s", hbuf, sbuf);
}

static struct addrinfo *my_getaddrinfo(char *host, int ai_family)
{
	int ret;
	struct addrinfo hints, *ai_head;

	bzero(&hints, sizeof(hints));

	hints.ai_family   = ai_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_PASSIVE;

	ret = getaddrinfo(host, USBIP_PORT_STRING, &hints, &ai_head);
	if (ret) {
		err("%s: %s", USBIP_PORT_STRING, gai_strerror(ret));
		return NULL;
	}

	return ai_head;
}

#define MAXSOCK 20
static int listen_all_addrinfo(struct addrinfo *ai_head, int lsock[])
{
	struct addrinfo *ai;
	int n = 0;		/* number of sockets */

	for (ai = ai_head; ai && n < MAXSOCK; ai = ai->ai_next) {
		int ret;

		lsock[n] = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (lsock[n] < 0)
			continue;

		usbip_set_reuseaddr(lsock[n]);
		usbip_set_nodelay(lsock[n]);

		if (lsock[n] >= FD_SETSIZE) {
			close(lsock[n]);
			lsock[n] = -1;
			continue;
		}

		ret = bind(lsock[n], ai->ai_addr, ai->ai_addrlen);
		if (ret < 0) {
			close(lsock[n]);
			lsock[n] = -1;
			continue;
		}

		ret = listen(lsock[n], SOMAXCONN);
		if (ret < 0) {
			close(lsock[n]);
			lsock[n] = -1;
			continue;
		}

		log_addrinfo(ai);

		/* next if succeed */
		n++;
	}

	if (n == 0) {
		err("no socket to listen to");
		return -1;
	}

	dbg("listen %d address%s", n, (n==1)?"":"es");

	return n;
}

#ifdef HAVE_LIBWRAP
static int tcpd_auth(int csock)
{
	int ret;
	struct request_info request;

	request_init(&request, RQ_DAEMON, "usbipd", RQ_FILE, csock, 0);

	fromhost(&request);

	ret = hosts_access(&request);
	if (!ret)
		return -1;

	return 0;
}
#endif

static int my_accept(int lsock)
{
	int csock;
	struct sockaddr_storage ss;
	socklen_t len = sizeof(ss);
	char host[NI_MAXHOST], port[NI_MAXSERV];
	int ret;

	bzero(&ss, sizeof(ss));

	csock = accept(lsock, (struct sockaddr *) &ss, &len);
	if (csock < 0) {
		err("accept");
		return -1;
	}

	ret = getnameinfo((struct sockaddr *) &ss, len,
			host, sizeof(host), port, sizeof(port),
			(NI_NUMERICHOST | NI_NUMERICSERV));
	if (ret)
		err("getnameinfo, %s", gai_strerror(ret));

#ifdef HAVE_LIBWRAP
	ret = tcpd_auth(csock);
	if (ret < 0) {
		info("deny access from %s", host);
		close(csock);
		return -1;
	}
#endif

	info("connected from %s:%s", host, port);

	return csock;
}


GMainLoop *main_loop;

static void signal_handler(int i)
{
	dbg("signal catched, code %d", i);

	if (main_loop)
		g_main_loop_quit(main_loop);
}

static void set_signal(void)
{
	struct sigaction act;

	bzero(&act, sizeof(act));
	act.sa_handler = signal_handler;
	sigemptyset(&act.sa_mask);
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
}

gboolean process_comming_request(GIOChannel *gio, GIOCondition condition, gpointer data)
{
	int ret;

	if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL))
		g_error("unknown condition 3");


	if (condition & G_IO_IN) {
		int lsock;
		int csock;

		lsock = g_io_channel_unix_get_fd(gio);

		csock = my_accept(lsock);
		if (csock < 0)
			return TRUE;

		ret = recv_pdu(csock);
		if (ret < 0)
			err("process recieved pdu");
	}

	return TRUE;
}


static void do_standalone_mode(gboolean daemonize)
{
	int ret;
	int lsock[MAXSOCK];
	struct addrinfo *ai_head;
	int n;



	ret = usbip_names_init(USBIDS_FILE);
	if (ret)
		err("open usb.ids");

	ret = usbip_stub_driver_open();
	if (ret < 0)
		g_error("driver open failed");

	if (daemonize) {
		if (daemon(0,0) < 0)
			g_error("daemonizing failed: %s", g_strerror(errno));

		usbip_use_syslog = 1;
	}

	set_signal();

	ai_head = my_getaddrinfo(NULL, PF_UNSPEC);
	if (!ai_head)
		return;

	n = listen_all_addrinfo(ai_head, lsock);
	if (n <= 0)
		g_error("no socket to listen to");

	for (int i = 0; i < n; i++) {
		GIOChannel *gio;

		gio = g_io_channel_unix_new(lsock[i]);
		g_io_add_watch(gio, (G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL),
				process_comming_request, NULL);
	}


	info("usbipd start (%s)", version);


	main_loop = g_main_loop_new(FALSE, FALSE);
	g_main_loop_run(main_loop);

	info("shutdown");

	freeaddrinfo(ai_head);
	usbip_names_free();
	usbip_stub_driver_close();

	return;
}


static const char help_message[] = "\
Usage: usbipd [options]				\n\
	-D, --daemon				\n\
		Run as a daemon process.	\n\
						\n\
	-d, --debug				\n\
		Print debugging information.	\n\
						\n\
	-v, --version				\n\
		Show version.			\n\
						\n\
	-h, --help 				\n\
		Print this help.		\n";

static void show_help(void)
{
	printf("%s", help_message);
}

static const struct option longopts[] = {
	{"daemon",	no_argument,	NULL, 'D'},
	{"debug",	no_argument,	NULL, 'd'},
	{"version",	no_argument,	NULL, 'v'},
	{"help",	no_argument,	NULL, 'h'},
	{NULL,		0,		NULL,  0}
};

int main(int argc, char *argv[])
{
	gboolean daemonize = FALSE;

	enum {
		cmd_standalone_mode = 1,
		cmd_help,
		cmd_version
	} cmd = cmd_standalone_mode;


	usbip_use_stderr = 1;
	usbip_use_syslog = 0;

	if (geteuid() != 0)
		g_warning("running non-root?");

	for (;;) {
		int c;
		int index = 0;

		c = getopt_long(argc, argv, "vhdD", longopts, &index);

		if (c == -1)
			break;

		switch (c) {
			case 'd':
				usbip_use_debug = 1;
				continue;
			case 'v':
				cmd = cmd_version;
				break;
			case 'h':
				cmd = cmd_help;
				break;
			case 'D':
				daemonize = TRUE;
				break;
			case '?':
				show_help();
				exit(EXIT_FAILURE);
			default:
				err("getopt");
		}
	}

	switch (cmd) {
		case cmd_standalone_mode:
			do_standalone_mode(daemonize);
			break;
		case cmd_version:
			printf("%s\n", version);
			break;
		case cmd_help:
			show_help();
			break;
		default:
			info("unknown cmd");
			show_help();
	}

	return 0;
}