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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 */

/** \file ghost/intern/GHOST_ISystem.cpp
 *  \ingroup GHOST
 */


/**
 * Copyright (C) 2001 NaN Technologies B.V.
 * \author	Maarten Gribnau
 * \date	May 7, 2001
 */

#include "GHOST_ISystem.h"

#ifdef WITH_X11
#  include "GHOST_SystemX11.h"
#endif

GHOST_ISystem *GHOST_ISystem::m_system = NULL;


GHOST_TSuccess GHOST_ISystem::createSystem()
{
	GHOST_TSuccess success;
	if (!m_system) {
#ifdef WITH_X11
		m_system = new GHOST_SystemX11();
#endif
		success = m_system != NULL ? GHOST_kSuccess : GHOST_kFailure;
	}
	else {
		success = GHOST_kFailure;
	}
	if (success) {
		success = m_system->init();
	}
	return success;
}

GHOST_TSuccess GHOST_ISystem::disposeSystem()
{
	GHOST_TSuccess success = GHOST_kSuccess;
	if (m_system) {
		delete m_system;
		m_system = NULL;
	}
	else {
		success = GHOST_kFailure;
	}
	return success;
}


GHOST_ISystem *GHOST_ISystem::getSystem()
{
	return m_system;
}
