/* Copyright (C) 2007-2010 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/** \file
 *
 * \author Anoop Saldanha <anoopsaldanha@gmail.com>
 */

#ifndef __DETECT_ENGINE_HHD_H__
#define __DETECT_ENGINE_HHD_H__

#include "app-layer-htp.h"

int DetectEngineRunHttpHeaderMpm(DetectEngineThreadCtx *, Flow *, HtpState *,
                                 uint8_t);
int DetectEngineInspectHttpHeader(DetectEngineCtx *, DetectEngineThreadCtx *,
                                  Signature *, Flow *, uint8_t, void *);
void DetectEngineCleanHHDBuffers(DetectEngineThreadCtx *);
void DetectEngineHttpHeaderRegisterTests(void);

#endif /* __DETECT_ENGINE_HHD_H__ */
