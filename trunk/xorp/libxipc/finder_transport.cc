// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright (c) 2001,2002 International Computer Science Institute
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software")
// to deal in the Software without restriction, subject to the conditions
// listed in the XORP LICENSE file. These conditions include: you must
// preserve this copyright notice, and you cannot mention the copyright
// holders in advertising related to the Software without their permission.
// The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
// notice is a summary of the XORP LICENSE file; the license in that file is
// legally binding.

#ident "$XORP: xorp/libxipc/finder_transport.cc,v 1.1 2002/12/14 23:42:54 hodson Exp $"

#include <vector>

#include "finder_module.h"
#include "libxorp/xorp.h"
#include "libxorp/debug.h"
#include "libxorp/xlog.h"

#include "libxorp/c_format.hh"
#include "libxorp/eventloop.hh"
#include "libxorp/exceptions.hh"
#include "libxorp/ref_ptr.hh"
#include "libxorp/asyncio.hh"

#include "finder_transport.hh"
#include "sockutil.hh"

/* ------------------------------------------------------------------------- */
/* static FinderTransport methods */

void
FinderTransport::no_arrival_callback(const FinderTransport&,
				     const string&)
{
    XLOG_ERROR("FinderTransport with no arrival callback.");
}

void
FinderTransport::no_departure_callback(const FinderTransport&,
				       const FinderMessage::RefPtr&)
{
    XLOG_ERROR("FinderTransport with no departure callback.");
}

void
FinderTransport::no_failure_callback(const FinderTransport&)
{
    XLOG_ERROR("FinderTransport with no failure callback.");
}

/* ------------------------------------------------------------------------- */

/**
 * FinderTcpTransport handles the transportation of FinderMessage's over TCP.
 */
class FinderTcpTransport : public FinderTransport {
public:
    FinderTcpTransport(EventLoop& e, int fd) :
	FinderTransport(), _fd(fd), _reader(e, fd), _read_pos(0), _writer(e, fd)
    {
	reader_reprovision(1480);
    }

    ~FinderTcpTransport()
    {
	if (_fd > 0)
	    close_socket(_fd);
    }
protected:
    // The socket
    int _fd;

    // Asynchronous socket reader
    AsyncFileReader		_reader;

    // Receive state
    vector<char>		_read_data;	// read buffer
    size_t			_read_pos;	// position of nxt read

    // Asynchronous socket writer
    AsyncFileWriter		_writer;

    // Outgoing state
    string			_write_data;

    // Methods
    void push_departures();
    void async_write(AsyncFileWriter::Event	e,
		     const uint8_t*		buffer,
		     size_t			buffer_bytes,
		     size_t			offset);

    void reader_reprovision(size_t bytes_required);
    void async_read(AsyncFileReader::Event	e,
		     const uint8_t*		buffer,
		     size_t			buffer_bytes,
		     size_t			offset);
};

void
FinderTcpTransport::push_departures()
{
    debug_msg("writer running (%d)\n",
	      _writer.running());

    if (_writer.running() == false && departures_waiting() == 1) {
	assert(_write_data.size() == 0);

	const FinderMessage::RefPtr& msg = departure_head();
	debug_msg("Pushing FinderMessage (seqno %d)\n", msg->seqno());
	if (hmac()) {
	    _write_data = msg->render_signed(*hmac());
	} else {
	    _write_data = msg->render();
	}
	if (_writer.buffers_remaining())
	    _writer.flush_buffers();
	_writer.add_buffer((const uint8_t*)_write_data.begin(),
			   _write_data.size(),
			   callback(this, &FinderTcpTransport::async_write));
	_writer.start();
    }
}

void
FinderTcpTransport::async_write(AsyncFileWriter::Event	e,
				const uint8_t*		buffer,
				size_t			/* buffer_bytes */,
				size_t			offset)
{
    assert(buffer == (const uint8_t*)_write_data.c_str());

    debug_msg("async_write Error %d offset %d\n", e, offset);
    switch (e) {
    case AsyncFileWriter::FLUSHING:
	return;
    case AsyncFileWriter::ERROR_CHECK_ERRNO:
    case AsyncFileWriter::END_OF_FILE:
	if (errno == EAGAIN) {
	    // Continue writing, socket just not ready.
	    _writer.resume();
	} else {
	    XLOG_ERROR("FinderTcpTransport::async_write %s", strerror(errno));
	    fail();
	}
	return;
    case AsyncFileWriter::DATA:
	;/* FALL_THROUGH */
    }

    debug_msg("Written %d / %d bytes\n", offset, _write_data.size());
    // If we get this far this is a completion or we're still running.
    assert(offset == _write_data.size() || _writer.running() == true);
    if (offset == _write_data.size()) {
	_write_data.resize(0);
	announce_departure(departure_head());
	push_departures();
    }
}

void
FinderTcpTransport::reader_reprovision(size_t extra_bytes)
{
    assert(extra_bytes >= _read_data.size() - _read_pos);

    size_t resume_pos;	// point where next read will be
    if (_read_pos > extra_bytes) {
	// Free space exists at start of buffer
	// Copy all pending data there.
	copy(_read_data.begin() + _read_pos, _read_data.end(),
	     _read_data.begin());
	resume_pos = _read_data.size() - _read_pos;
	_read_pos = 0;
    } else {
	_read_data.resize(_read_data.size() + extra_bytes);
	resume_pos = _read_pos;
    }
    _reader.flush_buffers();

    debug_msg("Reprovision %d (%d -> %d)\n", _read_pos,
	      _read_data.size(), _read_data.size() + extra_bytes);
    _reader.add_buffer_with_offset((uint8_t*)_read_data.begin(),
				   _read_data.size(),
				   resume_pos,
				   callback(this,
					    &FinderTcpTransport::async_read));
    _reader.start();
}

void
FinderTcpTransport::async_read(AsyncFileReader::Event	ev,
			       const uint8_t*		buffer,
			       size_t			/* buffer_bytes */,
			       size_t			offset)
{
    if (ev == AsyncFileReader::FLUSHING)
	return; // this code pre-dates flushing callbacks.

    assert(buffer == (const uint8_t*)_read_data.begin());
    debug_msg("async_read Event %d pos %d offset %d space %d\n",
	      ev, _read_pos, offset, _read_data.size());
    if (ev != AsyncFileReader::DATA) {
	if (errno == EAGAIN) {
	    // Continue writing, socket just not ready.
	    _reader.resume();
	} else {
	    XLOG_ERROR("FinderTcpTransport::async_read %s", strerror(errno));
	    fail();
	}
	return;
    }

    string r;
    for (;;) {
	try {
	    assert(_read_pos < _read_data.size());
	    ssize_t available = offset - _read_pos;
	    if (available == 0) {
		break;
	    }
	    assert(available > 0);
	    debug_msg("available %d\n", available);
	    r = string(_read_data.begin() + _read_pos, available);

	    ssize_t header_bytes = FinderParser::peek_header_bytes(r);
	    if (header_bytes < 0) {
		size_t max_header_bytes = FinderParser::max_header_bytes();
		if ((size_t)available < max_header_bytes) {
		    reader_reprovision(max_header_bytes);
		} else {
		    XLOG_ERROR("FinderTcpTransport::async_read could not find "
			       "message header");
		    fail();
		}
		debug_msg("peeking header bytes failed.\n");
		break;
	    }

	    size_t payload_bytes = FinderParser::peek_payload_bytes(r);
	    size_t msg_bytes = header_bytes + payload_bytes;

	    debug_msg("Space %d Need %d\n",
		      _read_data.size() - _read_pos, msg_bytes);
	    if (_read_data.size() < _read_pos + msg_bytes) {
		// short of space...resize buffer and push back
		size_t req = msg_bytes - (_read_pos - _read_data.size());
		debug_msg("short of space adding more buffering %d -> %d\n",
			  _read_data.size(), req);
		reader_reprovision(req);
		break;
	    }

	    assert(_read_pos + msg_bytes <= _read_data.size());
	    debug_msg("message bytes %d\n", msg_bytes);
	    string msg(_read_data.begin() + _read_pos, msg_bytes);
	    announce_arrival(msg);
	    _read_pos += msg_bytes;
	    if (_read_pos == _read_data.size()) {
		reader_reprovision(0);
		break;
	    }
	} catch (const BadFinderMessage &bmf) {
	    XLOG_ERROR("%s", bmf.str().c_str());
	    debug_msg("\n\"%s\"\n", r.c_str());
	    fail();
	}
    }
    assert(_reader.buffers_remaining() != 0);
}

/* ------------------------------------------------------------------------- */
/* FinderTransportTcpFactory related */

#include "libxorp/c_format.hh"
#include "sockutil.hh"

FinderTcpServerFactory::FinderTcpServerFactory(EventLoop&		e,
					       int 			port,
					       const ConnectCallback&	cb)
    throw (BadPort) : FinderTransportServerFactory(cb), _event_loop(e)
{
    _fd = create_listening_ip_socket(TCP, port);
    if (_fd < 0)
	xorp_throw(BadPort, c_format("Port %d: %s", port, strerror(errno)));
    _event_loop.add_selector(_fd, SEL_RD,
			     callback(this, &FinderTcpServerFactory::connect));
}

FinderTcpServerFactory::~FinderTcpServerFactory()
{
    _event_loop.remove_selector(_fd, SEL_RD);
    close_socket(_fd);
}

void
FinderTcpServerFactory::connect(int fd, SelectorMask m)
{
    struct sockaddr_in sin;
    socklen_t slen = sizeof(sin);

    assert(fd == _fd);
    assert(SEL_RD == m);

    int sock = accept(_fd, (sockaddr*)&sin, &slen);
    if (sock > 0) {
	int fl = fcntl(sock, F_GETFL);
	fcntl(sock, F_SETFL, fl | O_NONBLOCK);
	FinderTransport::RefPtr rp(new FinderTcpTransport(eventloop(), sock));
	announce_connect(rp);
    } else {
        XLOG_ERROR("accept() from FinderTcpServerFactory::connect() failed: "
		   "%s", strerror(errno));
    }
}

/* ------------------------------------------------------------------------- */
/* FinderTcpClientFactory */

FinderTcpClientFactory::FinderTcpClientFactory(EventLoop&  e,
					       const char* addr, int port)
    throw (BadDest) : _e(e), _addr(addr), _port(port)
{

    in_addr ia;
    if (address_lookup(addr, ia) == false)
	xorp_throw(BadDest, c_format("Bad address: %s", addr));
    if (port > 65536)
	xorp_throw(BadDest, c_format("Bad port: %d", port));
}

bool
FinderTcpClientFactory::connect()
{
    int sock = create_connected_ip_socket(TCP, _addr, _port);
    if (sock < 0)
	return true; // reschedule another attempt

    int fl = fcntl(sock, F_GETFL);
    fcntl(sock, F_SETFL, fl | O_NONBLOCK);
    FinderTransport::RefPtr rp(new FinderTcpTransport(_e, sock));
    _connect_cb->dispatch(rp);

    return false; // succeeded so do not reschedule another attempt
}

void
FinderTcpClientFactory::run(const ConnectCallback& cb)
{
    assert(running() == false);
    _connect_cb = cb;
    _connect_timer = _e.new_periodic(250, callback(this, &FinderTcpClientFactory::connect));
}

bool
FinderTcpClientFactory::running() const
{
    return _connect_timer.scheduled();
}

void
FinderTcpClientFactory::stop()
{
    _connect_timer.unschedule();
}
