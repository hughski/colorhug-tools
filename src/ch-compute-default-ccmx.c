/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <glib.h>
#include <colord.h>
#include <colorhug.h>

static void
add_to_average (const gchar *filename, CdMat3x3 *ave)
{
	CdIt8 *it8 = NULL;
	CdMat3x3 normalized;
	const CdMat3x3 *tmp;
	gboolean ret;
	GError *error = NULL;
	GFile *file = NULL;

	it8 = cd_it8_new ();
	file = g_file_new_for_path (filename);
	ret = cd_it8_load_from_file (it8, file, &error);
	if (!ret) {
		g_warning ("failed to load %s: %s", filename, error->message);
		g_error_free (error);
		goto out;
	}

	/* get matrix and normalize to red */
	tmp = cd_it8_get_matrix (it8);
	cd_mat33_scalar_multiply (tmp, 1.0f / ((gdouble) tmp->m00), &normalized);
	g_debug ("before=%s, after=%s",
		 cd_mat33_to_string (tmp),
		 cd_mat33_to_string (&normalized));

	/* add to average */
	ave->m00 += normalized.m00;
	ave->m01 += normalized.m01;
	ave->m02 += normalized.m02;
	ave->m10 += normalized.m10;
	ave->m11 += normalized.m11;
	ave->m12 += normalized.m12;
	ave->m20 += normalized.m20;
	ave->m21 += normalized.m21;
	ave->m22 += normalized.m22;
out:
	if (file != NULL)
		g_object_unref (file);
	if (it8 != NULL)
		g_object_unref (it8);
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	CdIt8 *it8 = NULL;
	CdMat3x3 ave;
	CdMat3x3 ave2;
	gboolean ret;
	GError *error = NULL;
	GFile *file = NULL;
	gint i;
	gint retval = 0;

	if (argc < 3) {
		retval = 1;
		g_warning ("need at least two arguments");
		goto out;
	}
	cd_mat33_clear (&ave);
	for (i = 2; i < argc; i++) {
		g_debug ("adding %s", argv[i]);
		add_to_average (argv[i], &ave);
	}
	cd_mat33_scalar_multiply (&ave, 1.0f / ((gdouble) argc - 2.0f), &ave2);

	/* write to output file */
	file = g_file_new_for_path (argv[1]);
	it8 = cd_it8_new_with_kind (CD_IT8_KIND_CCMX);
	cd_it8_set_matrix (it8, &ave2);
	cd_it8_set_originator (it8, "Richard Hughes");
	cd_it8_set_title (it8, "Default");
	cd_it8_add_option (it8, "TYPE_FACTORY");
	ret = cd_it8_save_to_file (it8, file, &error);
	if (!ret) {
		retval = 1;
		g_warning ("failed to write: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (file != NULL)
		g_object_unref (file);
	if (it8 != NULL)
		g_object_unref (it8);
	return retval;
}
