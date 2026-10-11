// Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later
package main

import (
	"bytes"
	"context"
	"crypto/tls"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"time"

	"github.com/quic-go/qpack"
	quic "github.com/quic-go/quic-go"
	"github.com/quic-go/quic-go/http3"
	"github.com/quic-go/quic-go/quicvarint"
)

func publicWSFrame(opcode byte, payload []byte, fin bool) []byte {
	head := opcode
	if fin {
		head |= 0x80
	}
	out := []byte{head, 0x80 | byte(len(payload))}
	if len(payload) >= 126 {
		out = []byte{head, 0xfe, byte(len(payload) >> 8), byte(len(payload))}
	}
	mask := []byte{0x13, 0x37, 0x42, 0x19}
	out = append(out, mask...)
	for at, value := range payload {
		out = append(out, value^mask[at%4])
	}
	return out
}

func publicH3Frame(s *quic.Stream) (uint64, []byte, error) {
	reader := quicvarint.NewReader(s)
	kind, err := quicvarint.Read(reader)
	if err != nil {
		return 0, nil, err
	}
	size, err := quicvarint.Read(reader)
	if err != nil {
		return 0, nil, err
	}
	if size > 65536 {
		return 0, nil, fmt.Errorf("public H3 frame bound")
	}
	payload := make([]byte, size)
	_, err = io.ReadFull(reader, payload)
	return kind, payload, err
}

type publicWSReader struct {
	stream *quic.Stream
	data   []byte
}

func (r *publicWSReader) Read(into []byte) (int, error) {
	for len(r.data) == 0 {
		kind, payload, err := publicH3Frame(r.stream)
		if err != nil {
			return 0, err
		}
		if kind != 0 {
			return 0, fmt.Errorf("unexpected WebSocket tunnel frame: %d", kind)
		}
		r.data = payload
	}
	count := copy(into, r.data)
	r.data = r.data[count:]
	return count, nil
}
func publicWSExpect(reader io.Reader, opcode byte, payload []byte) error {
	head := make([]byte, 2)
	if _, err := io.ReadFull(reader, head); err != nil {
		return err
	}
	if head[0] != 0x80|opcode || head[1]&0x80 != 0 {
		return fmt.Errorf("WebSocket FIN/RSV/opcode/mask")
	}
	size := int(head[1])
	if size == 126 {
		extended := make([]byte, 2)
		if _, err := io.ReadFull(reader, extended); err != nil {
			return err
		}
		size = int(binary.BigEndian.Uint16(extended))
		if size < 126 {
			return fmt.Errorf("nonminimal WebSocket length")
		}
	}
	if size > 8192 || size != len(payload) {
		return fmt.Errorf("WebSocket payload size")
	}
	data := make([]byte, size)
	if _, err := io.ReadFull(reader, data); err != nil {
		return err
	}
	if !bytes.Equal(data, payload) {
		return fmt.Errorf("WebSocket payload differs")
	}
	return nil
}
func runPublicWebSocket(port int, ca string) error {
	roots, err := roots(ca)
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	conn, err := quic.DialAddr(ctx, fmt.Sprintf("127.0.0.1:%d", port), &tls.Config{RootCAs: roots, ServerName: "a.example", NextProtos: []string{"h3"}, MinVersion: tls.VersionTLS13}, &quic.Config{Versions: []quic.Version{quic.Version1}})
	if err != nil {
		return err
	}
	defer conn.CloseWithError(0, "public WS done")
	tlsState := conn.ConnectionState()
	if tlsState.Version != quic.Version1 || tlsState.Used0RTT || tlsState.TLS.NegotiatedProtocol != "h3" || len(tlsState.TLS.VerifiedChains) == 0 {
		return fmt.Errorf("WS authentication")
	}
	transport := &http3.Transport{AdditionalSettings: map[uint64]uint64{8: 1}}
	client := transport.NewClientConn(conn)
	select {
	case <-client.ReceivedSettings():
	case <-ctx.Done():
		return ctx.Err()
	}
	if !client.Settings().EnableExtendedConnect {
		return fmt.Errorf("WS SETTINGS absent")
	}
	stream, err := conn.OpenStreamSync(ctx)
	if err != nil {
		return err
	}
	stream.SetDeadline(time.Now().Add(5 * time.Second))
	err = publicHeaders(stream, []qpack.HeaderField{{Name: ":method", Value: "CONNECT"}, {Name: ":protocol", Value: "websocket"}, {Name: ":scheme", Value: "https"}, {Name: ":authority", Value: "a.example"}, {Name: ":path", Value: "/ws/echo"}, {Name: "sec-websocket-version", Value: "13"}, {Name: "sec-websocket-protocol", Value: "other, harness.v1"}, {Name: "sec-websocket-extensions", Value: "permessage-deflate"}})
	if err != nil {
		return err
	}
	kind, data, err := publicH3Frame(stream)
	if err != nil {
		return err
	}
	if kind != 1 {
		return fmt.Errorf("WS opening HEADERS absent")
	}
	headers, err := qpack.NewDecoder(nil).DecodeFull(data)
	if err != nil {
		return err
	}
	fields := map[string]string{}
	for _, field := range headers {
		if _, exists := fields[field.Name]; exists {
			return fmt.Errorf("duplicate WS opening field")
		}
		fields[field.Name] = field.Value
	}
	if fields[":status"] != "200" || fields["sec-websocket-protocol"] != "harness.v1" {
		return fmt.Errorf("WS opening status/subprotocol")
	}
	for _, name := range []string{"upgrade", "connection", "sec-websocket-accept", "sec-websocket-extensions"} {
		if _, exists := fields[name]; exists {
			return fmt.Errorf("WS opening forbidden field")
		}
	}
	reader := &publicWSReader{stream: stream}
	send := func(opcode byte, payload []byte, fin bool) error {
		return publicFrame(stream, 0, publicWSFrame(opcode, payload, fin))
	}
	for _, message := range []struct {
		opcode  byte
		payload []byte
	}{{1, []byte("public text")}, {2, []byte{0, 255, 'b', 'i', 'n', 'a', 'r', 'y'}}} {
		if err = send(message.opcode, message.payload, true); err != nil {
			return err
		}
		if err = publicWSExpect(reader, message.opcode, message.payload); err != nil {
			return err
		}
	}
	fragmented := []byte("fragment € done")
	prefix := len("fragment ") + 1
	wire := append(publicWSFrame(1, fragmented[:prefix], false), publicWSFrame(9, []byte("ping"), true)...)
	wire = append(wire, publicWSFrame(0, fragmented[prefix:], true)...)
	if err = publicFrame(stream, 0, wire); err != nil {
		return err
	}
	if err = publicWSExpect(reader, 10, []byte("ping")); err != nil {
		return err
	}
	if err = publicWSExpect(reader, 1, fragmented); err != nil {
		return err
	}
	wire = publicWSFrame(2, []byte("segmented"), true)
	for _, part := range [][]byte{wire[:1], wire[1:3], wire[3:7], wire[7:]} {
		if err = publicFrame(stream, 0, part); err != nil {
			return err
		}
	}
	if err = publicWSExpect(reader, 2, []byte("segmented")); err != nil {
		return err
	}
	wire = append(publicWSFrame(1, []byte("one"), true), publicWSFrame(2, []byte("two"), true)...)
	if err = publicFrame(stream, 0, wire); err != nil {
		return err
	}
	if err = publicWSExpect(reader, 1, []byte("one")); err != nil {
		return err
	}
	if err = publicWSExpect(reader, 2, []byte("two")); err != nil {
		return err
	}
	sibling, err := publicOpen(ctx, conn, "GET", "/hello", nil, true, false)
	if err != nil {
		return err
	}
	healthy, err := publicRead(sibling)
	if err != nil {
		return err
	}
	if string(healthy.body) != "hello native h3\n" {
		return fmt.Errorf("WS HTTP sibling")
	}
	if err = send(8, []byte{3, 232}, true); err != nil {
		return err
	}
	stream.Close()
	if err = publicWSExpect(reader, 8, []byte{3, 232}); err != nil {
		return err
	}
	if _, err = reader.Read(make([]byte, 1)); err != io.EOF {
		return fmt.Errorf("WS FIN absent: %v", err)
	}
	receipt := identity()
	for key, value := range fieldsMapForPublicWS() {
		receipt[key] = value
	}
	return json.NewEncoder(os.Stdout).Encode(receipt)
}
func fieldsMapForPublicWS() fields {
	return fields{"case": "rfc9220_wire_corpus", "status": "passed", "text": "public text", "binary_hex": "00ff62696e617279", "fragmented_hex": "667261676d656e7420e282ac20646f6e65", "close_code": 1000, "terminal": "FIN", "settings_connect": 1, "subprotocol": "harness.v1", "sibling_body": "hello native h3\n"}
}
