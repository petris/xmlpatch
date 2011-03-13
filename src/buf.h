/**
 * This is part of an XML patch utility.
 *
 * Copyright (C) 2011 Petr Malat
 *
 * Contact: Petr Malat <oss@malat.biz>
 *
 * This utility is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License 
 * version 3 as published by the Free Software Foundation.
 *
 * This utility is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef BUF_H
#define BUF_H

#define BUF_INC 4096

struct buf_s {
	char *buf;
	unsigned size;
	unsigned data_size;
};

typedef struct buf_s * buf_t;

#define buf_new() (struct buf_s[]){{ .buf = NULL, .size = 0, .data_size = 0 }}
#define buf_free(name) free(name->buf)
#define buf_data(name) ((void*)(name)->buf)
#define buf_size(name) ((unsigned const)(name)->data_size)

static void buf_append(buf_t buf, const void *append, unsigned append_size)
{
	unsigned requested_size = buf->data_size + append_size;

	if (buf->size < requested_size) {
		buf->size *= 2;
		if (buf->size < requested_size) {
			buf->size = (requested_size & ~(BUF_INC - 1)) + BUF_INC;
		}
		buf->buf = realloc(buf->buf, buf->size);
		if (buf->buf == NULL) {
			error(EXIT_FAILURE, errno, "Reading patch file failed");
		}
	}
	memcpy(buf->buf + buf->data_size, append, append_size);
	buf->data_size += append_size;
}

#endif // BUF_H
