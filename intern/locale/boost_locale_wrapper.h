/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2012, Blender Foundation
 * All rights reserved.
 */

/** \file locale/boost_locale_wrapper.h
 *  \ingroup locale
 *  A thin C wrapper around boost::locale...
 */

#ifndef __BOOST_LOCALE_WRAPPER_H__
#define __BOOST_LOCALE_WRAPPER_H__

#ifdef __cplusplus
extern "C" {
#endif

void bl_locale_init(const char *messages_path, const char *default_domain);
void bl_locale_set(const char *locale);
const char *bl_locale_get(void);
const char *bl_locale_pgettext(const char *msgctxt, const char *msgid);

#ifdef __cplusplus
}
#endif

#endif /* __BOOST_LOCALE_WRAPPER_H__ */
