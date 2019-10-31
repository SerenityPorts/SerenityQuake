/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <time.h>
#include <string.h>

#include "common.h"

const char *build_version = stringify(TYR_VERSION);
const int64_t build_version_timestamp = TYR_VERSION_TIME;

const char *
Build_DateString()
{
    static char buffer[26];
    //time_t timestamp = build_version_timestamp;
    //struct tm gmtimestamp;

//#ifdef _WIN32
    //gmtime_s(&gmtimestamp, &timestamp);
//#else
    //gmtime_r(&timestamp, &gmtimestamp);
//#endif
    //strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &gmtimestamp);

    strcpy(buffer, "SERENITYQUAKE\0");

    return buffer;
}
