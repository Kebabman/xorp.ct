// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright (c) 2001-2009 XORP, Inc.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License, Version 2, June
// 1991 as published by the Free Software Foundation. Redistribution
// and/or modification of this program under the terms of any other
// version of the GNU General Public License is not permitted.
// 
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. For more details,
// see the GNU General Public License, Version 2, a copy of which can be
// found in the XORP LICENSE.gpl file.
// 
// XORP Inc, 2953 Bunker Hill Lane, Suite 204, Santa Clara, CA 95054, USA;
// http://xorp.net

// $XORP: xorp/rtrmgr/path_segment.hh,v 1.10 2008/10/02 21:58:24 bms Exp $

#ifndef __RTRMGR_PATH_SEGMENT_HH__
#define __RTRMGR_PATH_SEGMENT_HH__

class PathSegment {
public:
    PathSegment(const string& segname, bool is_tag) 
	: _is_tag(is_tag), _segname(segname) {}

    bool is_tag() const { return _is_tag; }
    const string& segname() const { return _segname; }

private:
    bool	_is_tag;
    string	_segname;
};

#endif // __RTRMGR_PATH_SEGMENT_HH__
