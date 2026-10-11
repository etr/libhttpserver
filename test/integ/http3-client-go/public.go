// Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later
package main

import (
	"bytes"
	"context"
	"crypto/tls"
	"encoding/json"
	"errors"
	"fmt"
	"github.com/quic-go/qpack"
	quic "github.com/quic-go/quic-go"
	"github.com/quic-go/quic-go/http3"
	"github.com/quic-go/quic-go/logging"
	"github.com/quic-go/quic-go/quicvarint"
	"io"
	"os"
	"strconv"
	"strings"
	"sync/atomic"
	"time"
)

// Raw request streams exercise trailers with the pinned independent QPACK
// codec; quic-go's RequestStream.SendRequestHeader does not support trailers.
func publicFrame(s *quic.Stream, kind uint64, data []byte) error {
	h := quicvarint.Append(nil, kind)
	h = quicvarint.Append(h, uint64(len(data)))
	if _, err := s.Write(h); err != nil {
		return err
	}
	_, err := s.Write(data)
	return err
}
func publicHeaders(s *quic.Stream, fields []qpack.HeaderField) error {
	var b bytes.Buffer
	e := qpack.NewEncoder(&b)
	for _, f := range fields {
		if err := e.WriteField(f); err != nil {
			return err
		}
	}
	if err := e.Close(); err != nil {
		return err
	}
	return publicFrame(s, 1, b.Bytes())
}
func publicOpen(ctx context.Context, c *quic.Conn, method, path string, body []byte, end, trailers bool) (*quic.Stream, error) {
	s, err := c.OpenStreamSync(ctx)
	if err != nil {
		return nil, err
	}
	if err = s.SetDeadline(time.Now().Add(3 * time.Second)); err != nil {
		return nil, err
	}
	if err = publicHeaders(s, []qpack.HeaderField{{Name: ":method", Value: method}, {Name: ":scheme", Value: "https"}, {Name: ":authority", Value: "a.example"}, {Name: ":path", Value: path}}); err != nil {
		return nil, err
	}
	for at := 0; at < len(body); at += 3 {
		if err = publicFrame(s, 0, body[at:min(at+3, len(body))]); err != nil {
			return nil, err
		}
	}
	if trailers {
		if err = publicHeaders(s, []qpack.HeaderField{{Name: "x-upload-end", Value: "observed"}}); err != nil {
			return nil, err
		}
	}
	if end {
		err = s.Close()
	}
	return s, err
}

type publicResponse struct {
	sections [][]qpack.HeaderField
	body     []byte
}

func publicRead(s *quic.Stream) (publicResponse, error) {
	r := publicResponse{}
	input := quicvarint.NewReader(s)
	for {
		kind, err := quicvarint.Read(input)
		if err == io.EOF {
			return r, nil
		}
		if err != nil {
			return r, err
		}
		size, err := quicvarint.Read(input)
		if err != nil {
			return r, err
		}
		if size > 65536 {
			return r, fmt.Errorf("public frame bound")
		}
		data := make([]byte, int(size))
		if _, err = io.ReadFull(input, data); err != nil {
			return r, err
		}
		if kind == 0 {
			r.body = append(r.body, data...)
			if len(r.body) > 65536 {
				return r, fmt.Errorf("public body bound")
			}
		}
		if kind == 1 {
			fields, err := qpack.NewDecoder(nil).DecodeFull(data)
			if err != nil {
				return r, err
			}
			r.sections = append(r.sections, fields)
		}
	}
}
func (r publicResponse) has(name, value string, trailer bool) bool {
	if len(r.sections) == 0 {
		return false
	}
	section := r.sections[0]
	if trailer {
		section = r.sections[len(r.sections)-1]
	}
	for _, f := range section {
		if f.Name == name && f.Value == value {
			return true
		}
	}
	return false
}
func runPublic(port int, ca string) error {
	roots, err := roots(ca)
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()
	var retries atomic.Int32
	c, err := quic.DialAddr(ctx, fmt.Sprintf("127.0.0.1:%d", port), &tls.Config{RootCAs: roots, ServerName: "a.example", NextProtos: []string{"h3"}, MinVersion: tls.VersionTLS13}, &quic.Config{Versions: []quic.Version{quic.Version1}, HandshakeIdleTimeout: 3 * time.Second, Tracer: func(context.Context, logging.Perspective, quic.ConnectionID) *logging.ConnectionTracer {
		return &logging.ConnectionTracer{ReceivedRetry: func(*logging.Header) { retries.Add(1) }}
	}})
	if err != nil {
		return err
	}
	defer c.CloseWithError(0, "public done")
	t := &http3.Transport{}
	_ = t.NewClientConn(c)
	state := c.ConnectionState()
	if state.TLS.NegotiatedProtocol != "h3" || state.TLS.Version != tls.VersionTLS13 || state.Version != quic.Version1 || state.Used0RTT || len(state.TLS.VerifiedChains) == 0 {
		return fmt.Errorf("public authentication")
	}
	request := func(method, path string, body []byte, trailers bool) (publicResponse, error) {
		s, err := publicOpen(ctx, c, method, path, body, true, trailers)
		if err != nil {
			return publicResponse{}, err
		}
		return publicRead(s)
	}
	rows := []fields{}
	record := func(name string) { rows = append(rows, fields{"case": name, "status": "passed"}) }
	get, err := request("GET", "/hello", nil, false)
	if err != nil {
		return err
	}
	if !get.has(":status", "200", false) || !get.has("x-native-h3", "authenticated-route", false) || !get.has("x-end", "native-fin", true) || string(get.body) != "hello native h3\n" {
		return fmt.Errorf("public GET/trailers")
	}
	record("authenticated_get_trailers")
	rows[len(rows)-1]["body"] = string(get.body)
	unknown, err := request("GET", "/missing", nil, false)
	if err != nil {
		return err
	}
	if !unknown.has(":status", "404", false) {
		return fmt.Errorf("public unknown route")
	}
	record("unknown_route")
	upload := "incremental installed native h3 upload"
	echo, err := request("POST", "/body", []byte(upload), true)
	if err != nil {
		return err
	}
	if string(echo.body) != upload || !echo.has("x-upload-end", "observed", true) {
		return fmt.Errorf("public upload/trailers")
	}
	record("incremental_upload_request_trailers")
	rows[len(rows)-1]["body"] = string(echo.body)
	meta, err := request("GET", "/meta", nil, false)
	if err != nil {
		return err
	}
	if !meta.has(":status", "204", false) || !meta.has("x-protocol", "h3", false) || !meta.has("x-peer", "127.0.0.1", false) {
		return fmt.Errorf("public metadata")
	}
	record("tls_peer_metadata")
	entered := func(index int, minimum int) error {
		for attempt := 0; attempt < 100; attempt++ {
			counts, err := request("GET", "/counts", nil, false)
			if err != nil {
				return err
			}
			values := strings.Fields(string(counts.body))
			if len(values) != 4 {
				return fmt.Errorf("public counts shape")
			}
			value, err := strconv.Atoi(values[index])
			if err != nil {
				return err
			}
			if value >= minimum {
				return nil
			}
		}
		return fmt.Errorf("public handler entry absent")
	}
	held, err := publicOpen(ctx, c, "GET", "/running", nil, true, false)
	if err != nil {
		return err
	}
	if err = entered(2, 1); err != nil {
		return err
	}
	started := time.Now()
	healthy, err := request("GET", "/hello", nil, false)
	if err != nil {
		return err
	}
	if time.Since(started) > time.Second || string(healthy.body) != "hello native h3\n" {
		return fmt.Errorf("public running sibling")
	}
	if _, err = request("GET", "/release", nil, false); err != nil {
		return err
	}
	if _, err = publicRead(held); err != nil {
		return err
	}
	record("parallel_running_handler")
	held, err = publicOpen(ctx, c, "POST", "/body", []byte("first"), false, false)
	if err != nil {
		return err
	}
	healthy, err = request("GET", "/hello", nil, false)
	if err != nil {
		return err
	}
	if string(healthy.body) != "hello native h3\n" {
		return fmt.Errorf("public suspended sibling")
	}
	if err = publicFrame(held, 0, []byte("-last")); err != nil {
		return err
	}
	if err = held.Close(); err != nil {
		return err
	}
	finish, err := publicRead(held)
	if err != nil {
		return err
	}
	if string(finish.body) != "first-last" {
		return fmt.Errorf("public held upload")
	}
	record("parallel_suspended_upload")
	reset, err := publicOpen(ctx, c, "POST", "/cancel", []byte("prefix"), false, false)
	if err != nil {
		return err
	}
	if err = entered(3, 1); err != nil {
		return err
	}
	reset.CancelRead(0x10c)
	reset.CancelWrite(0x10c)
	settled := false
	for at := 0; at < 20; at++ {
		counts, err := request("GET", "/counts", nil, false)
		if err != nil {
			return err
		}
		values := strings.Fields(string(counts.body))
		if len(values) != 4 {
			return fmt.Errorf("public counts")
		}
		seen, _ := strconv.Atoi(values[0])
		done, _ := strconv.Atoi(values[1])
		cancelled, _ := strconv.Atoi(values[3])
		if cancelled == 1 && done == seen-1 {
			settled = true
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	if !settled {
		return fmt.Errorf("public cancellation completion")
	}
	record("reset_stream_local_once")
	rows[len(rows)-1]["cancellation_entries"] = 1
	for at := 0; at < 24; at++ {
		recovered, err := request("GET", "/hello", nil, false)
		if err != nil {
			return err
		}
		if string(recovered.body) != "hello native h3\n" || !recovered.has("x-end", "native-fin", true) {
			return fmt.Errorf("stream capacity recovery payload")
		}
	}
	record("transport_stream_capacity_recovery")
	rows[len(rows)-1]["requests"] = 24
	result := identity()
	result["alpn"] = "h3"
	result["verified"] = true
	result["quic_version"] = 1
	result["tls_version"] = "TLSv1.3"
	result["early_data"] = false
	if retries.Load() != 1 {
		return fmt.Errorf("public Retry not observed")
	}
	result["retry_count"] = retries.Load()
	result["scenarios"] = rows
	return json.NewEncoder(os.Stdout).Encode(result)
}

func runPublicStop(port int, ca string) error {
	roots, err := roots(ca)
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	c, err := quic.DialAddr(ctx, fmt.Sprintf("127.0.0.1:%d", port), &tls.Config{RootCAs: roots, ServerName: "a.example", NextProtos: []string{"h3"}, MinVersion: tls.VersionTLS13}, &quic.Config{Versions: []quic.Version{quic.Version1}})
	if err != nil {
		return err
	}
	defer c.CloseWithError(0, "done")
	state := c.ConnectionState()
	if len(state.TLS.VerifiedChains) == 0 || state.TLS.NegotiatedProtocol != "h3" || state.Used0RTT {
		return fmt.Errorf("stop authentication")
	}
	t := &http3.Transport{}
	_ = t.NewClientConn(c)
	s, err := publicOpen(ctx, c, "POST", "/cancel", []byte("held"), false, false)
	if err != nil {
		return err
	}
	time.Sleep(50 * time.Millisecond)
	fmt.Println("H3_ACTIVE")
	_, err = publicRead(s)
	observed, code := publicStopObservation(err)
	if observed == "" {
		return fmt.Errorf("destruction lacked expected remote cancellation: %w", err)
	}
	result := identity()
	result["status"] = "passed"
	result["case"] = "active_stream_server_destruction"
	result["terminal"] = observed
	result["error_code"] = code
	return json.NewEncoder(os.Stdout).Encode(result)
}

// Only a decoded remote terminal proves server destruction; decoder errors,
// local cancellation and client deadlines cannot satisfy this witness.
func publicStopObservation(err error) (string, uint64) {
	var stream *quic.StreamError
	if errors.As(err, &stream) && stream.Remote && stream.ErrorCode == 0x10c {
		return "remote_stream_cancelled", uint64(stream.ErrorCode)
	}
	var app *quic.ApplicationError
	if errors.As(err, &app) && app.Remote && app.ErrorCode == 0x100 {
		return "remote_connection_closed", uint64(app.ErrorCode)
	}
	var transport *quic.TransportError
	if errors.As(err, &transport) && transport.Remote && transport.ErrorCode == quic.NoError {
		return "remote_transport_closed", uint64(transport.ErrorCode)
	}
	return "", 0
}
