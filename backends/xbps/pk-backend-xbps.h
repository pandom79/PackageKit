/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2021 Domenico Panella <pandom79@gmail.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <pk-backend.h>
#include <pk-bitfield.h>
#include <pk-enum.h>
#include <config.h>
#include <assert.h>
#include <glib.h>
#include <xbps.h>
#include <syslog.h>
#include <pk-shared.h>
#include <regex.h>
#include <pk-package-id.h>
#include <errno.h>

#ifndef __UNCONST
#define __UNCONST(a)    ((void *)(unsigned long)(const void *)(a))
#endif
#define UNUSED          __attribute__((unused))

 /* TYPES */
typedef char            gchar;

typedef struct {
    struct  xbps_handle *xhp;
    gchar   *xbps_sysconfdir;
    gchar   *xbps_confdir;
    gchar   *path_repoconf;
    gchar   *path_repolist;
    gboolean job_cancelled;
} PkBackendXbpsPrivate;

struct search_data {
    bool regex, repo_mode;
    regex_t regexp;
    unsigned int maxcols;
    const char *pat, *prop, *repourl;
    xbps_array_t results;
    char *linebuf;
};

/* FUNCTIONS */

/* Xbps */
int     pk_backend_xbps_init            (PkBackendXbpsPrivate *);
void    pk_backend_xbps_end             (PkBackendXbpsPrivate *);

/* Repositories */
gchar** pk_xbps_list_groups             (GKeyFile *, gchar *, gsize *);
gchar*  pk_xbps_repodesc_by_url         (PkBackendXbpsPrivate *, const gchar *);

/* Packages */
gchar*  pk_xbps_compose_pkgid           (PkBackendXbpsPrivate *, xbps_dictionary_t, gboolean);
void    pk_xbps_show_data               (PkBackendXbpsPrivate *, PkBackendJob *job,
                                         xbps_dictionary_t, PkBitfield filter);
void    pk_backend_xbps_packages_thread (PkBackendJob *, GVariant *, gpointer);
gsize   pk_xbps_map_pkgname             (struct xbps_handle *xhp);

/* Search */
struct search_data *
        pk_xbps_search_pkgpat           (PkBackendXbpsPrivate *, gchar *, gboolean);

int     repo_import_key_cb              (struct xbps_repo *, void *UNUSED, bool *UNUSED);

/* Transaction */
int     exec_transaction                (struct xbps_handle *, bool);
void    pk_xbps_set_transaction_err     (PkBackendJob *, gsize, const gchar *);


