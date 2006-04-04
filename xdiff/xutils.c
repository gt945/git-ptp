/*
 *  LibXDiff by Davide Libenzi ( File Differential Library )
 *  Copyright (C) 2003	Davide Libenzi
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#include "xinclude.h"



#define XDL_GUESS_NLINES 256




long xdl_bogosqrt(long n) {
	long i;

	/*
	 * Classical integer square root approximation using shifts.
	 */
	for (i = 1; n > 0; n >>= 2)
		i <<= 1;

	return i;
}


int xdl_emit_diffrec(char const *rec, long size, char const *pre, long psize,
		     xdemitcb_t *ecb) {
	mmbuffer_t mb[3];
	int i;

	mb[0].ptr = (char *) pre;
	mb[0].size = psize;
	mb[1].ptr = (char *) rec;
	mb[1].size = size;
	i = 2;

	if (!size || rec[size-1] != '\n') {
		mb[2].ptr = "\n\\ No newline at end of file\n";
		mb[2].size = strlen(mb[2].ptr);
		i = 3;
	}

	if (ecb->outf(ecb->priv, mb, i) < 0) {

		return -1;
	}

	return 0;
}

void *xdl_mmfile_first(mmfile_t *mmf, long *size)
{
	*size = mmf->size;
	return mmf->ptr;
}


void *xdl_mmfile_next(mmfile_t *mmf, long *size)
{
	return NULL;
}


long xdl_mmfile_size(mmfile_t *mmf)
{
	return mmf->size;
}


int xdl_cha_init(chastore_t *cha, long isize, long icount) {

	cha->head = cha->tail = NULL;
	cha->isize = isize;
	cha->nsize = icount * isize;
	cha->ancur = cha->sncur = NULL;
	cha->scurr = 0;

	return 0;
}


void xdl_cha_free(chastore_t *cha) {
	chanode_t *cur, *tmp;

	for (cur = cha->head; (tmp = cur) != NULL;) {
		cur = cur->next;
		xdl_free(tmp);
	}
}


void *xdl_cha_alloc(chastore_t *cha) {
	chanode_t *ancur;
	void *data;

	if (!(ancur = cha->ancur) || ancur->icurr == cha->nsize) {
		if (!(ancur = (chanode_t *) xdl_malloc(sizeof(chanode_t) + cha->nsize))) {

			return NULL;
		}
		ancur->icurr = 0;
		ancur->next = NULL;
		if (cha->tail)
			cha->tail->next = ancur;
		if (!cha->head)
			cha->head = ancur;
		cha->tail = ancur;
		cha->ancur = ancur;
	}

	data = (char *) ancur + sizeof(chanode_t) + ancur->icurr;
	ancur->icurr += cha->isize;

	return data;
}


void *xdl_cha_first(chastore_t *cha) {
	chanode_t *sncur;

	if (!(cha->sncur = sncur = cha->head))
		return NULL;

	cha->scurr = 0;

	return (char *) sncur + sizeof(chanode_t) + cha->scurr;
}


void *xdl_cha_next(chastore_t *cha) {
	chanode_t *sncur;

	if (!(sncur = cha->sncur))
		return NULL;
	cha->scurr += cha->isize;
	if (cha->scurr == sncur->icurr) {
		if (!(sncur = cha->sncur = sncur->next))
			return NULL;
		cha->scurr = 0;
	}

	return (char *) sncur + sizeof(chanode_t) + cha->scurr;
}


long xdl_guess_lines(mmfile_t *mf) {
	long nl = 0, size, tsize = 0;
	char const *data, *cur, *top;

	if ((cur = data = xdl_mmfile_first(mf, &size)) != NULL) {
		for (top = data + size; nl < XDL_GUESS_NLINES;) {
			if (cur >= top) {
				tsize += (long) (cur - data);
				if (!(cur = data = xdl_mmfile_next(mf, &size)))
					break;
				top = data + size;
			}
			nl++;
			if (!(cur = memchr(cur, '\n', top - cur)))
				cur = top;
			else
				cur++;
		}
		tsize += (long) (cur - data);
	}

	if (nl && tsize)
		nl = xdl_mmfile_size(mf) / (tsize / nl);

	return nl + 1;
}


unsigned long xdl_hash_record(char const **data, char const *top) {
	unsigned long ha = 5381;
	char const *ptr = *data;

	for (; ptr < top && *ptr != '\n'; ptr++) {
		ha += (ha << 5);
		ha ^= (unsigned long) *ptr;
	}
	*data = ptr < top ? ptr + 1: ptr;

	return ha;
}


unsigned int xdl_hashbits(unsigned int size) {
	unsigned int val = 1, bits = 0;

	for (; val < size && bits < CHAR_BIT * sizeof(unsigned int); val <<= 1, bits++);
	return bits ? bits: 1;
}


int xdl_num_out(char *out, long val) {
	char *ptr, *str = out;
	char buf[32];

	ptr = buf + sizeof(buf) - 1;
	*ptr = '\0';
	if (val < 0) {
		*--ptr = '-';
		val = -val;
	}
	for (; val && ptr > buf; val /= 10)
		*--ptr = "0123456789"[val % 10];
	if (*ptr)
		for (; *ptr; ptr++, str++)
			*str = *ptr;
	else
		*str++ = '0';
	*str = '\0';

	return str - out;
}


long xdl_atol(char const *str, char const **next) {
	long val, base;
	char const *top;

	for (top = str; XDL_ISDIGIT(*top); top++);
	if (next)
		*next = top;
	for (val = 0, base = 1, top--; top >= str; top--, base *= 10)
		val += base * (long)(*top - '0');
	return val;
}


int xdl_emit_hunk_hdr(long s1, long c1, long s2, long c2,
		      const char *func, long funclen, xdemitcb_t *ecb) {
	int nb = 0;
	mmbuffer_t mb;
	char buf[128];

	memcpy(buf, "@@ -", 4);
	nb += 4;

	nb += xdl_num_out(buf + nb, c1 ? s1: s1 - 1);

	if (c1 != 1) {
		memcpy(buf + nb, ",", 1);
		nb += 1;

		nb += xdl_num_out(buf + nb, c1);
	}

	memcpy(buf + nb, " +", 2);
	nb += 2;

	nb += xdl_num_out(buf + nb, c2 ? s2: s2 - 1);

	if (c2 != 1) {
		memcpy(buf + nb, ",", 1);
		nb += 1;

		nb += xdl_num_out(buf + nb, c2);
	}

	memcpy(buf + nb, " @@", 3);
	nb += 3;
	if (func && funclen) {
		buf[nb++] = ' ';
		if (funclen > sizeof(buf) - nb - 1)
			funclen = sizeof(buf) - nb - 1;
		memcpy(buf + nb, func, funclen);
		nb += funclen;
	}
	buf[nb++] = '\n';

	mb.ptr = buf;
	mb.size = nb;
	if (ecb->outf(ecb->priv, &mb, 1) < 0)
		return -1;

	return 0;
}

