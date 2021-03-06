/*****************************************************************************
 * ijksdl_endian.h
 *****************************************************************************
 *
 * copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef IJKCCSDL__IJKCCSDL_ENDIAN_H
#define IJKCCSDL__IJKCCSDL_ENDIAN_H

#define CCSDL_LIL_ENDIAN  1234
#define CCSDL_BIG_ENDIAN  4321

#ifndef CCSDL_BYTEORDER           /* Not defined in CCSDL_config.h? */
#ifdef __linux__
#include <endian.h>
#define CCSDL_BYTEORDER  __BYTE_ORDER
#else /* __linux __ */
#if defined(__hppa__) || \
    defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
    (defined(__MIPS__) && defined(__MISPEB__)) || \
    defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
    defined(__sparc__)
#define CCSDL_BYTEORDER   CCSDL_BIG_ENDIAN
#else
#define CCSDL_BYTEORDER   CCSDL_LIL_ENDIAN
#endif
#endif /* __linux __ */
#endif /* !CCSDL_BYTEORDER */

#endif
