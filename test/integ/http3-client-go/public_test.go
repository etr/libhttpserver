// Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later
package main

import (
	"context"
	"crypto/tls"
	"encoding/pem"
	"errors"
	"io"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"

	quic "github.com/quic-go/quic-go"
)

func TestPublicDestructionRejectsPeerThatKeepsStreamOpen(t *testing.T) {
	fixtures := filepath.Join("..", "..", "data", "tls_credentials")
	cert, err := tls.LoadX509KeyPair(filepath.Join(fixtures, "a.pem"), filepath.Join(fixtures, "a-key.pem"))
	if err != nil {
		t.Fatal(err)
	}
	// The leaf and intermediate are both sent by the public server.
	intermediate, err := os.ReadFile(filepath.Join(fixtures, "intermediate.pem"))
	if err != nil {
		t.Fatal(err)
	}
	block, _ := pem.Decode(intermediate)
	if block == nil {
		t.Fatal("intermediate PEM missing")
	}
	cert.Certificate = append(cert.Certificate, block.Bytes)
	listener, err := quic.ListenAddr("127.0.0.1:0", &tls.Config{Certificates: []tls.Certificate{cert}, NextProtos: []string{"h3"}}, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	settled := make(chan error, 1)
	go func() {
		conn, err := listener.Accept(ctx)
		if err != nil {
			settled <- err
			return
		}
		defer conn.CloseWithError(0, "test finished")
		stream, err := conn.AcceptStream(ctx)
		if err != nil {
			settled <- err
			return
		}
		// No FIN, RESET or close is sent while the witness is reading.
		_, err = io.Copy(io.Discard, stream)
		settled <- err
	}()
	if err := runPublicStop(listener.Addr().(*net.UDPAddr).Port, filepath.Join(fixtures, "root.pem")); err == nil {
		t.Error("deadline-only read counted as remote destruction")
	}
	cancel()
	select {
	case <-settled:
	case <-time.After(time.Second):
		t.Fatal("test peer did not retire")
	}
}

func TestPublicStopRequiresExpectedRemoteTerminal(t *testing.T) {
	for _, err := range []error{nil, io.EOF, io.ErrUnexpectedEOF, context.Canceled, context.DeadlineExceeded,
		os.ErrDeadlineExceeded, errors.New("QPACK decode failure"), &quic.IdleTimeoutError{},
		&quic.StreamError{Remote: false, ErrorCode: 0x10c}, &quic.StreamError{Remote: true, ErrorCode: 0x102},
		&quic.ApplicationError{Remote: false, ErrorCode: 0x100}, &quic.ApplicationError{Remote: true, ErrorCode: 0x102},
		&quic.TransportError{Remote: true, ErrorCode: quic.ProtocolViolation}} {
		if observed, _ := publicStopObservation(err); observed != "" {
			t.Errorf("false terminal for %v", err)
		}
	}
	for _, err := range []error{&quic.StreamError{Remote: true, ErrorCode: 0x10c},
		&quic.ApplicationError{Remote: true, ErrorCode: 0x100}, &quic.TransportError{Remote: true, ErrorCode: quic.NoError}} {
		if observed, _ := publicStopObservation(err); observed == "" {
			t.Errorf("remote terminal missing for %v", err)
		}
	}
}
