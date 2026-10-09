// Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later
package main

import (
	"bufio"
	"encoding/json"
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
