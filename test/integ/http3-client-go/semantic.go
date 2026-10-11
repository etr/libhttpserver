// Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later
package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	quic "github.com/quic-go/quic-go"
	"github.com/quic-go/quic-go/http3"
	"github.com/quic-go/quic-go/logging"
	"io"
	"net/http"
	"sort"
	"strings"
	"sync"
	"time"
)

type wireWitness struct {
	mu      sync.Mutex
	highest map[int64]int64
	credit  map[int64]int64
}

type drainACKWitness struct {
	mu           sync.Mutex
	finPackets   map[logging.PacketNumber]bool
	finACK       chan struct{}
	acknowledged bool
	consumption  context.Context
	responseRead chan struct{}
	consumed     bool
}

func (w *drainACKWitness) expectResponseBeforeACK(ctx context.Context) {
	w.mu.Lock()
	defer w.mu.Unlock()
	w.consumption = ctx
	w.responseRead = make(chan struct{})
}
func (w *drainACKWitness) responseConsumed() {
	w.mu.Lock()
	defer w.mu.Unlock()
	if w.responseRead != nil && !w.consumed {
		w.consumed = true
		close(w.responseRead)
	}
}

// The native H3 stack closes after the last response EOF during GOAWAY.
// A local-only stream holds it open while this observer verifies a real FIN ACK.
func (w *drainACKWitness) receivedPacket(number logging.PacketNumber, frames []logging.Frame) {
	w.mu.Lock()
	defer w.mu.Unlock()
	for _, frame := range frames {
		if stream, ok := frame.(*logging.StreamFrame); ok && stream.StreamID == 0 && stream.Fin {
			if w.finPackets == nil {
				w.finPackets = map[logging.PacketNumber]bool{}
			}
			if len(w.finPackets) < 128 {
				w.finPackets[number] = true
			}
		}
	}
}
func (w *drainACKWitness) sentACK(ack *logging.AckFrame) {
	w.mu.Lock()
	covered := false
	if ack != nil && !w.acknowledged {
		for number := range w.finPackets {
			covered = covered || ack.AcksPacket(number)
		}
	}
	ctx, consumed := w.consumption, w.responseRead
	w.mu.Unlock()
	if !covered {
		return
	}
	// Pinned quic-go invokes this tracer before packet submission. Hold only
	// the ACK containing the accepted FIN until the application has consumed
	// its buffered response; otherwise remote close can discard unread data.
	// The original drain context bounds this client-only scheduling amendment.
	if consumed != nil {
		select {
		case <-consumed:
		case <-ctx.Done():
			return
		}
	}
	w.mu.Lock()
	defer w.mu.Unlock()
	if !w.acknowledged {
		w.acknowledged = true
		if w.finACK != nil {
			close(w.finACK)
		}
	}
}
func (w *drainACKWitness) finAcknowledged() bool {
	w.mu.Lock()
	defer w.mu.Unlock()
	return w.acknowledged
}
func (w *drainACKWitness) waitFINACK(ctx context.Context) error {
	select {
	case <-w.finACK:
		return nil
	case <-ctx.Done():
		return fmt.Errorf("accepted response FIN ACK: %w", ctx.Err())
	}
}

func (w *wireWitness) received(frames []logging.Frame) {
	w.mu.Lock()
	defer w.mu.Unlock()
	for _, frame := range frames {
		if stream, ok := frame.(*logging.StreamFrame); ok {
			id, end := int64(stream.StreamID), int64(stream.Offset+stream.Length)
			if end > w.highest[id] {
				w.highest[id] = end
			}
		}
	}
}
func (w *wireWitness) sent(frames []logging.Frame) {
	w.mu.Lock()
	defer w.mu.Unlock()
	for _, frame := range frames {
		if limit, ok := frame.(*logging.MaxStreamDataFrame); ok {
			w.credit[int64(limit.StreamID)] = int64(limit.MaximumStreamData)
		}
	}
}
func (w *wireWitness) exhausted(id int64) (int64, int64) {
	w.mu.Lock()
	defer w.mu.Unlock()
	credit := int64(4096)
	if w.credit[id] > credit {
		credit = w.credit[id]
	}
	return credit, w.highest[id]
}
func headerFields(headers http.Header) [][]string {
	names := make([]string, 0, len(headers))
	for name := range headers {
		names = append(names, name)
	}
	sort.Strings(names)
	result := [][]string{}
	for _, name := range names {
		for _, value := range headers[name] {
			result = append(result, []string{strings.ToLower(name), value})
		}
	}
	return result
}
func semanticOpen(client *http3.ClientConn, ctx context.Context, port int, method, path string, length int) (*http3.RequestStream, error) {
	stream, err := client.OpenRequestStream(ctx)
	if err != nil {
		return nil, err
	}
	request, err := http.NewRequestWithContext(ctx, method, fmt.Sprintf("https://localhost:%d%s", port, path), nil)
	if err != nil {
		return nil, err
	}
	request.Host = "localhost"
	request.ContentLength = int64(length)
	request.Body = http.NoBody
	request.Header["X-Repeat"] = []string{"a", "b"}
	return stream, stream.SendRequestHeader(request)
}
func semanticResponse(stream *http3.RequestStream, res *http.Response) (fields, error) {
	var err error
	if res == nil {
		res, err = stream.ReadResponse()
		if err != nil {
			return nil, err
		}
	}
	defer res.Body.Close()
	body, err := io.ReadAll(io.LimitReader(res.Body, 524289))
	if err != nil {
		return nil, err
	}
	if len(body) > 524288 {
		return nil, fmt.Errorf("body bound")
	}
	sum := sha256.Sum256(body)
	return fields{"status": res.StatusCode, "stream": int64(stream.StreamID()), "body_hex": hex.EncodeToString(body), "body_bytes": len(body), "body_sha256": hex.EncodeToString(sum[:]), "headers": headerFields(res.Header), "trailers": headerFields(res.Trailer), "fin_count": 1}, nil
}
func semanticUploads(client *http3.ClientConn, ctx context.Context, port int, cases *[]fields, w *wireWitness) error {
	upload := []byte("first\x00second\xffthird\x00")
	for _, name := range []string{"streaming", "slow_producer"} {
		stream, err := semanticOpen(client, ctx, port, "POST", "/items/../semantic?x=1", len(upload))
		if err != nil {
			return err
		}
		if _, err = stream.Write(upload[:6]); err != nil {
			return err
		}
		observed, err := control("wait", fields{"event": "read", "stream": int64(stream.StreamID()), "min_bytes": 6})
		if err != nil {
			return err
		}
		c := fields{"case": name, "connection": observed["connection"], "prefix_bytes": observed["read_bytes"], "before_fin": true}
		if name == "slow_producer" {
			sibling, err := open(client, ctx, port, "GET", "/health", nil)
			if err != nil {
				return err
			}
			result, err := response(sibling)
			if err != nil {
				return err
			}
			witness, err := control("wait", fields{"event": "health", "stream": int64(sibling.StreamID())})
			if err != nil {
				return err
			}
			c["sibling_stream"] = int64(sibling.StreamID())
			c["sibling_status"] = result["status"]
			c["sibling_connection"] = witness["connection"]
		}
		if _, err = stream.Write(upload[6:]); err != nil {
			return err
		}
		if err = stream.Close(); err != nil {
			return err
		}
		result, err := semanticResponse(stream, nil)
		if err != nil {
			return err
		}
		for key, value := range result {
			c[key] = value
		}
		*cases = append(*cases, c)
	}
	stream, err := semanticOpen(client, ctx, port, "GET", "/large", 0)
	if err != nil {
		return err
	}
	if err = stream.Close(); err != nil {
		return err
	}
	res, err := stream.ReadResponse()
	if err != nil {
		return err
	}
	id := int64(stream.StreamID())
	deadline := time.Now().Add(5 * time.Second)
	credit, offset := w.exhausted(id)
	for offset < credit {
		if time.Now().After(deadline) {
			return fmt.Errorf("stream did not exhaust receive credit: %d/%d", offset, credit)
		}
		time.Sleep(time.Millisecond)
		credit, offset = w.exhausted(id)
	}
	sibling, err := open(client, ctx, port, "GET", "/health", nil)
	if err != nil {
		return err
	}
	healthy, err := response(sibling)
	if err != nil {
		return err
	}
	witness, err := control("wait", fields{"event": "health", "stream": int64(sibling.StreamID())})
	if err != nil {
		return err
	}
	snapshot, err := control("snapshot", fields{"connection": witness["connection"], "stream": id})
	if err != nil {
		return err
	}
	result, err := semanticResponse(stream, res)
	if err != nil {
		return err
	}
	delete(result, "body_hex")
	result["case"] = "slow_consumer"
	result["connection"] = snapshot["connection"]
	result["sibling_stream"] = int64(sibling.StreamID())
	result["sibling_status"] = healthy["status"]
	result["sibling_connection"] = witness["connection"]
	result["credit"] = credit
	result["offset"] = offset
	result["withheld"] = true
	result["started"] = snapshot["started"]
	result["completed"] = snapshot["completed"]
	result["resumed"] = true
	*cases = append(*cases, result)
	return nil
}
func semanticDrain(conn *quic.Conn, client *http3.ClientConn, ctx context.Context, port int, cases *[]fields, witness *drainACKWitness) error {
	held, err := open(client, ctx, port, "GET", "/hold", nil)
	if err != nil {
		return err
	}
	observed, err := control("wait", fields{"event": "held", "stream": int64(held.StreamID())})
	if err != nil {
		return err
	}
	late, err := client.OpenRequestStream(ctx)
	if err != nil {
		return err
	}
	// Keep one entirely local native H3 stream tracked until the server closes.
	// Otherwise reading the last EOF makes quic-go close before its FIN ACK.
	localHold, err := client.OpenRequestStream(ctx)
	if err != nil {
		return err
	}
	defer localHold.CancelRead(268)
	defer localHold.CancelWrite(268)
	drainCtx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()
	witness.expectResponseBeforeACK(drainCtx)
	if _, err = control("drain", fields{"connection": observed["connection"], "stream": int64(held.StreamID())}); err != nil {
		return err
	}
	received, err := control("goaway_witness", fields{"connection": observed["connection"], "stream": int64(held.StreamID())})
	if err != nil {
		return err
	}
	req, err := http.NewRequestWithContext(ctx, "GET", fmt.Sprintf("https://localhost:%d/hello", port), nil)
	if err != nil {
		return err
	}
	if err = late.SendRequestHeader(req); err != nil {
		return err
	}
	late.Close()
	_, err = late.ReadResponse()
	var refused *quic.StreamError
	if !errors.As(err, &refused) {
		return fmt.Errorf("late request was not reset: %v", err)
	}
	if _, err = control("release", fields{"connection": observed["connection"], "stream": int64(held.StreamID())}); err != nil {
		return err
	}
	accepted, err := response(held)
	if err != nil {
		return err
	}
	witness.responseConsumed()
	if err = witness.waitFINACK(drainCtx); err != nil {
		return err
	}
	if _, err = control("wait", fields{"event": "drained", "connection": observed["connection"], "stream": 0}); err != nil {
		return err
	}
	select {
	case <-conn.Context().Done():
	case <-drainCtx.Done():
		return drainCtx.Err()
	}
	var closeErr *quic.ApplicationError
	if !errors.As(context.Cause(conn.Context()), &closeErr) || !closeErr.Remote {
		return fmt.Errorf("missing graceful native close: %v", context.Cause(conn.Context()))
	}
	*cases = append(*cases, fields{"case": "goaway", "connection": observed["connection"], "accepted_stream": int64(held.StreamID()), "late_stream": int64(late.StreamID()), "ids": received["ids"], "rejection": uint64(refused.ErrorCode), "accepted_status": accepted["status"], "accepted_body_hex": accepted["body_hex"], "close_code": uint64(closeErr.ErrorCode), "drain_complete": true, "fin_acknowledged": witness.finAcknowledged(), "local_hold_stream": int64(localHold.StreamID())})
	return nil
}
