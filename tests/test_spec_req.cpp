/*
    Copyright (c) 2007-2013 Contributors as noted in the AUTHORS file

    This file is part of 0MQ.

    0MQ is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    0MQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include "testutil.hpp"

void test_round_robin_out (void *ctx)
{
    void *req = zmq_socket (ctx, ZMQ_REQ);
    assert (req);

    int rc = zmq_bind (req, "inproc://b");
    assert (rc == 0);

    const size_t N = 5;
    void *rep[N];
    for (size_t i = 0; i < N; ++i)
    {
        rep[i] = zmq_socket (ctx, ZMQ_REP);
        assert (rep[i]);

        int timeout = 100;
        rc = zmq_setsockopt (rep[i], ZMQ_RCVTIMEO, &timeout, sizeof(int));
        assert (rc == 0);

        rc = zmq_connect (rep[i], "inproc://b");
        assert (rc == 0);
    }

    // Send N request-replies, and expect every REP it used once in order
    for (size_t i = 0; i < N; ++i)
    {
        s_send_seq (req, "ABC", SEQ_END);
        s_recv_seq (rep[i], "ABC", SEQ_END);
        s_send_seq (rep[i], "DEF", SEQ_END);
        s_recv_seq (req, "DEF", SEQ_END);
    }

    rc = zmq_close (req);
    assert (rc == 0);

    for (size_t i = 0; i < N; ++i)
    {
        rc = zmq_close (rep[i]);
        assert (rc == 0);
    }
}

void test_req_only_listens_to_current_peer (void *ctx)
{
    void *req = zmq_socket (ctx, ZMQ_REQ);
    assert (req);

    int rc = zmq_setsockopt(req, ZMQ_IDENTITY, "A", 2);
    assert (rc == 0);

    rc = zmq_bind (req, "inproc://c");
    assert (rc == 0);

    const size_t N = 3;
    void *router[N];
    for (size_t i = 0; i < N; ++i)
    {
        router[i] = zmq_socket (ctx, ZMQ_ROUTER);
        assert (router[i]);

        int timeout = 100;
        rc = zmq_setsockopt (router[i], ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
        assert (rc == 0);

        int enabled = 1;
        rc = zmq_setsockopt (router[i], ZMQ_ROUTER_MANDATORY, &enabled, sizeof(enabled));
        assert (rc == 0);

        rc = zmq_connect (router[i], "inproc://c");
        assert (rc == 0);
    }

    for (size_t i = 0; i < N; ++i)
    {
        s_send_seq (req, "ABC", SEQ_END);

        // Receive on router i
        s_recv_seq (router[i], "A", 0, "ABC", SEQ_END);

        // Send back replies on all routers
        for (size_t j = 0; j < N; ++j)
        {
            const char *replies[] = { "WRONG", "GOOD" };
            const char *reply = replies[i == j ? 1 : 0];
            s_send_seq (router[j], "A", 0, reply, SEQ_END);
        }

        // Recieve only the good relpy
        s_recv_seq (req, "GOOD", SEQ_END);
    }

    rc = zmq_close (req);
    assert (rc == 0);

    for (size_t i = 0; i < N; ++i)
    {
        rc = zmq_close (router[i]);
        assert (rc == 0);
    }
}

void test_req_message_format (void *ctx)
{
    void *req = zmq_socket (ctx, ZMQ_REQ);
    assert (req);

    void *router = zmq_socket (ctx, ZMQ_ROUTER);
    assert (router);

    int rc = zmq_bind (req, "inproc://a");
    assert (rc == 0);

    rc = zmq_connect (router, "inproc://a");
    assert (rc == 0);

    // Send a multi-part request.
    s_send_seq (req, "ABC", "DEF", SEQ_END);

    zmq_msg_t msg;
    zmq_msg_init (&msg);

    // Receive peer identity
    rc = zmq_msg_recv (&msg, router, 0);
    assert (rc != -1);
    assert (zmq_msg_size (&msg) > 0);
    zmq_msg_t peer_id_msg;
    zmq_msg_init (&peer_id_msg);
    zmq_msg_copy (&peer_id_msg, &msg);

    int more = 0;
    size_t more_size = sizeof(more);
    rc = zmq_getsockopt (router, ZMQ_RCVMORE, &more, &more_size);
    assert (rc == 0);
    assert (more);

    // Receive the rest.
    s_recv_seq (router, 0, "ABC", "DEF", SEQ_END);

    // Send back a single-part reply.
    rc = zmq_msg_send (&peer_id_msg, router, ZMQ_SNDMORE);
    assert (rc != -1);
    s_send_seq (router, 0, "GHI", SEQ_END);

    // Receive reply.
    s_recv_seq (req, "GHI", SEQ_END);

    rc = zmq_msg_close (&msg);
    assert (rc == 0);

    rc = zmq_msg_close (&peer_id_msg);
    assert (rc == 0);

    rc = zmq_close (req);
    assert (rc == 0);

    rc = zmq_close (router);
    assert (rc == 0);
}

void test_block_on_send_no_peers (void *ctx)
{
    void *sc = zmq_socket (ctx, ZMQ_REQ);
    assert (sc);

    int timeout = 100;
    int rc = zmq_setsockopt (sc, ZMQ_SNDTIMEO, &timeout, sizeof(timeout));
    assert (rc == 0);

    rc = zmq_send (sc, 0, 0, ZMQ_DONTWAIT);
    assert (rc == -1);
    assert (errno == EAGAIN);

    rc = zmq_send (sc, 0, 0, 0);
    assert (rc == -1);
    assert (errno == EAGAIN);

    rc = zmq_close (sc);
    assert (rc == 0);
}

int main ()
{
    void *ctx = zmq_ctx_new ();
    assert (ctx);

    // SHALL route outgoing messages to connected peers using a round-robin
    // strategy.
    test_round_robin_out (ctx);

    // The request and reply messages SHALL have this format on the wire:
    // * A delimiter, consisting of an empty frame, added by the REQ socket.
    // * One or more data frames, comprising the message visible to the
    //   application.
    test_req_message_format (ctx);

    // SHALL block on sending, or return a suitable error, when it has no connected peers.
    test_block_on_send_no_peers (ctx);

    // SHALL accept an incoming message only from the last peer that it sent a
    // request to.
    // SHALL discard silently any messages received from other peers.
    test_req_only_listens_to_current_peer (ctx);

    int rc = zmq_ctx_term (ctx);
    assert (rc == 0);

    return 0 ;
}
