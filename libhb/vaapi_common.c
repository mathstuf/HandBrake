/* vaapi_common.c
 *
 * Copyright (c) 2003-2020 HandBrake Team
 * This file is part of the HandBrake source code.
 * Homepage: <http://handbrake.fr/>.
 * It may be used under the terms of the GNU General Public License v2.
 * For full terms see the file COPYING file or visit http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "handbrake/hbffmpeg.h"
#include "handbrake/handbrake.h"

int hb_vaapi_h264_available()
{
    #if HB_PROJECT_FEATURE_VAAPI
        return 1;
    #else
        return 0;
    #endif
}

int hb_vaapi_h265_available()
{
    #if HB_PROJECT_FEATURE_VAAPI
        return 1;
    #else
        return 0;
    #endif
}
