// Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later
package main

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"github.com/quic-go/quic-go/logging"
	"strings"
	"testing"
)

func TestControlPreserves62BitGOAWAY(t *testing.T) {
	old := input
	defer func() { input = old }()
	input = bufio.NewScanner(strings.NewReader("{\"ids\":[4611686018427387900,4]}\n"))
	values, err := control("probe", fields{})
	if err != nil {
		t.Fatal(err)
	}
	wire, err := json.Marshal(values)
	if err != nil {
		t.Fatal(err)
	}
	if string(wire) != "{\"ids\":[4611686018427387900,4]}" {
		t.Fatalf("received GOAWAY lost precision: %s", wire)
	}
}

func TestDrainRequiresOwnedFINACKBeforeEOF(t *testing.T) {
	observer := &drainACKWitness{}
	ack := func(first, last logging.PacketNumber) *logging.AckFrame {
		return &logging.AckFrame{AckRanges: []logging.AckRange{{Smallest: first, Largest: last}}}
	}
	observer.receivedPacket(32, []logging.Frame{&logging.StreamFrame{StreamID: 4, Fin: true}})
	observer.sentACK(ack(0, 32))
	if observer.finAcknowledged() {
		t.Fatal("sibling FIN retired accepted response")
	}
	observer.receivedPacket(40, []logging.Frame{&logging.StreamFrame{StreamID: 0, Fin: true}})
	observer.sentACK(ack(41, 50))
	observer.sentACK(ack(0, 39))
	if observer.finAcknowledged() {
		t.Fatal("ACK gap retired accepted FIN")
	}
	observer.receivedPacket(52, []logging.Frame{&logging.StreamFrame{StreamID: 0, Fin: true}})
	observer.sentACK(ack(52, 52))
	if !observer.finAcknowledged() {
		t.Fatal("retransmitted accepted FIN ACK not observed")
	}
	observer.sentACK(nil)
	observer.sentACK(ack(0, 39))
	if !observer.finAcknowledged() {
		t.Fatal("terminal FIN ACK witness changed")
	}
}

func TestDrainACKWaitIsBoundedAndTerminal(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	pending := &drainACKWitness{finACK: make(chan struct{})}
	if !errors.Is(pending.waitFINACK(ctx), context.Canceled) {
		t.Fatal("missing FIN ACK did not honor cancellation")
	}
	completed := &drainACKWitness{finACK: make(chan struct{})}
	completed.receivedPacket(7, []logging.Frame{&logging.StreamFrame{StreamID: 0, Fin: true}})
	ack := &logging.AckFrame{AckRanges: []logging.AckRange{{Smallest: 7, Largest: 7}}}
	completed.sentACK(ack)
	completed.sentACK(ack)
	if err := completed.waitFINACK(context.Background()); err != nil {
		t.Fatal(err)
	}
}
