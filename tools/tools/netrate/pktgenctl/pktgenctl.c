/*
 * Copyright (c) 2012 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/ethernet.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pktgen/pktgen.h"

#define PKTGEN_DEVPATH	"/dev/pktg0"

#define DEFAULT_PORT	3000

#define INDST_MASK	0x0001
#define INSRC_MASK	0x0002
#define EADDR_MASK	0x0004
#define IFACE_MASK	0x0008
#define DATALEN_MASK	0x0010
#define DURATION_MASK	0x0020

#define MASK_NEEDED	(INDST_MASK | INSRC_MASK | EADDR_MASK | IFACE_MASK)

static void
usage(void)
{
	fprintf(stderr, "pktgenctl "
	    "-d dst_inaddr[:dst_port] [-d dst_inaddr[:dst_port] ...] "
	    "-s src_inaddr[:src_port] "
	    "-e (gw_eaddr|dst_eaddr) -i iface "
	    "[-m data_len] [-l duration]\n");
	exit(1);
}

static int
get_port(char *str)
{
	char *p;

	p = strchr(str, ':');
	if (p == NULL) {
		return DEFAULT_PORT;
	} else {
		*p = '\0';
		++p;
		return atoi(p);
	}
}

int
main(int argc, char *argv[])
{
	struct pktgen_conf conf;
	struct sockaddr *sa;
	struct sockaddr_in *src_sin, *dst_sin;
	struct sockaddr_dl sdl;
	char eaddr_str[32];
	uint32_t arg_mask = 0;
	int fd, c, n, ndst_alloc;

	memset(&conf, 0, sizeof(conf));

	conf.pc_duration = 10;
	conf.pc_datalen = 18;

	sa = &conf.pc_dst_lladdr;
	sa->sa_family = AF_LINK;
	sa->sa_len = ETHER_ADDR_LEN;

	src_sin = &conf.pc_src;
	src_sin->sin_family = AF_INET;

	ndst_alloc = 1;
	conf.pc_dst = calloc(ndst_alloc, sizeof(struct sockaddr_in));
	if (conf.pc_dst == NULL)
		err(1, "calloc(%d dst)", ndst_alloc);

	conf.pc_ndst = 0;
	while ((c = getopt(argc, argv, "d:s:e:i:m:l:")) != -1) {
		switch (c) {
		case 'd':
			if (conf.pc_ndst >= ndst_alloc) {
				void *old = conf.pc_dst;
				int old_ndst_alloc = ndst_alloc;

				ndst_alloc *= 2;
				conf.pc_dst = calloc(ndst_alloc,
				    sizeof(struct sockaddr_in));
				if (conf.pc_dst == NULL)
					err(1, "calloc(%d dst)", ndst_alloc);
				memcpy(conf.pc_dst, old,
				old_ndst_alloc * sizeof(struct sockaddr_in));
				free(old);
			}
			dst_sin = &conf.pc_dst[conf.pc_ndst++];

			dst_sin->sin_family = AF_INET;
			dst_sin->sin_port = get_port(optarg);
			dst_sin->sin_port = htons(dst_sin->sin_port);
			n = inet_pton(AF_INET, optarg, &dst_sin->sin_addr);
			if (n == 0)
				errx(1, "-d: invalid inet address: %s", optarg);
			else if (n < 0)
				err(1, "-d: %s", optarg);
			arg_mask |= INDST_MASK;
			break;

		case 's':
			src_sin->sin_port = get_port(optarg);
			src_sin->sin_port = htons(src_sin->sin_port);
			n = inet_pton(AF_INET, optarg, &src_sin->sin_addr);
			if (n == 0)
				errx(1, "-s: invalid inet address: %s", optarg);
			else if (n < 0)
				err(1, "-s: %s", optarg);
			arg_mask |= INSRC_MASK;
			break;

		case 'e':
			strcpy(eaddr_str, "if0.");
			strlcpy(&eaddr_str[strlen("if0.")], optarg,
			    sizeof(eaddr_str) - strlen("if0."));

			memset(&sdl, 0, sizeof(sdl));
			sdl.sdl_len = sizeof(sdl);
			link_addr(eaddr_str, &sdl);
			bcopy(LLADDR(&sdl), sa->sa_data, ETHER_ADDR_LEN);
			arg_mask |= EADDR_MASK;
			break;

		case 'i':
			strlcpy(conf.pc_ifname, optarg, sizeof(conf.pc_ifname));
			arg_mask |= IFACE_MASK;
			break;

		case 'm':
			conf.pc_datalen = atoi(optarg);
			arg_mask |= DATALEN_MASK;
			break;

		case 'l':
			conf.pc_duration = atoi(optarg);
			arg_mask |= DURATION_MASK;
			break;

		default:
			usage();
		}
	}

	if ((arg_mask & MASK_NEEDED) != MASK_NEEDED)
		usage();

	fd = open(PKTGEN_DEVPATH, O_RDONLY);
	if (fd < 0)
		err(1, "open(" PKTGEN_DEVPATH ")");

	if (ioctl(fd, PKTGENSCONF, &conf) < 0)
		err(1, "ioctl(PKTGENSCONF)");

	if (ioctl(fd, PKTGENSTART) < 0)
		err(1, "ioctl(PKTGENSTART)");

	close(fd);
	exit(0);
}
